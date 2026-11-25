/* Impossible SDK: portable freestanding GIF87a/GIF89a decoder. No floats, no OS calls. */
#include "gif.h"

static void gif_zero(void *dst, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = 0u;
}

static void gif_copy(uint8_t *dst, const uint8_t *src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

static uint16_t rd_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static bool bytes_eq(const uint8_t *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != (uint8_t)b[i]) return false;
    }
    return true;
}

static void write_pixel(uint8_t *dst, im_pixel_format fmt, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (fmt == IM_PIXEL_BGRA8) {
        dst[0] = b;
        dst[1] = g;
        dst[2] = r;
        dst[3] = a;
    } else {
        dst[0] = r;
        dst[1] = g;
        dst[2] = b;
        dst[3] = a;
    }
}

/* Caller-allocated LZW scratch state (zero static mutable state, ~20 KiB). */
typedef struct gif_lzw {
    uint16_t prefix[4096];
    uint8_t suffix[4096];
    uint8_t stack[4096];
} gif_lzw;

typedef struct gif_gce {
    bool valid;
    uint8_t disposal;
    bool user_input;
    bool has_trans;
    uint8_t trans_index;
    uint32_t delay_ms;
} gif_gce;

im_image_error im_gif_probe(const uint8_t *data, size_t len,
                            const im_image_limits *limits,
                            im_gif_info *out_info) {
    if (out_info) gif_zero(out_info, sizeof(*out_info));
    if (!data || !out_info) return IM_IMAGE_BAD_ARG;
    if (len == 0u) return IM_IMAGE_TRUNCATED;

    static const char sig87[7] = "GIF87a";
    static const char sig89[7] = "GIF89a";
    bool match87 = true;
    bool match89 = true;
    size_t check_n = len < 6u ? len : 6u;
    for (size_t i = 0; i < check_n; i++) {
        if (data[i] != (uint8_t)sig87[i]) match87 = false;
        if (data[i] != (uint8_t)sig89[i]) match89 = false;
    }
    if (!match87 && !match89) return IM_IMAGE_INVALID;
    if (len < 13u) return IM_IMAGE_TRUNCATED;

    uint32_t width = (uint32_t)rd_le16(data + 6u);
    uint32_t height = (uint32_t)rd_le16(data + 8u);
    uint8_t packed = data[10];
    uint8_t bg_index = data[11];

    if (width == 0u || height == 0u) return IM_IMAGE_INVALID;

    size_t byte_size = 0u;
    im_image_error lim_err = im_image_check_limits(width, height, limits, &byte_size);
    if (lim_err != IM_IMAGE_OK) return lim_err;

    bool has_gct = (packed & 0x80u) != 0u;
    uint16_t gct_size = has_gct ? (uint16_t)(1u << ((packed & 0x07u) + 1u)) : 0u;

    out_info->width = width;
    out_info->height = height;
    out_info->has_gct = has_gct;
    out_info->gct_size = gct_size;
    out_info->bg_index = bg_index;
    out_info->is_gif89a = match89;
    out_info->byte_size = byte_size;
    return IM_IMAGE_OK;
}

static im_image_error skip_subblocks(const uint8_t *data, size_t len, size_t *pos_io) {
    size_t pos = *pos_io;
    for (;;) {
        if (pos >= len) return IM_IMAGE_TRUNCATED;
        uint8_t sb_len = data[pos++];
        if (sb_len == 0u) {
            *pos_io = pos;
            return IM_IMAGE_OK;
        }
        if (len - pos < (size_t)sb_len) return IM_IMAGE_TRUNCATED;
        pos += (size_t)sb_len;
    }
}

/* Decode one LZW image stream directly onto canvas (when canvas != NULL), strictly validating
   every code, palette index, sub-block boundary, EOI code, and exact pixel count fw * fh. */
static im_image_error decode_lzw_frame(const uint8_t *data, size_t len, size_t *pos_io,
                                       gif_lzw *lzw,
                                       uint32_t screen_w, uint16_t left, uint16_t top,
                                       uint16_t fw, uint16_t fh, bool interlaced,
                                       const uint8_t *pal, uint16_t pal_count,
                                       bool has_trans, uint8_t trans_index,
                                       im_pixel_format fmt, uint8_t *canvas) {
    size_t pos = *pos_io;
    if (pos >= len) return IM_IMAGE_TRUNCATED;
    uint8_t min_cs = data[pos++];
    if (min_cs < 2u || min_cs > 8u) return IM_IMAGE_INVALID;

    uint16_t clear_code = (uint16_t)(1u << min_cs);
    uint16_t eoi_code = (uint16_t)(clear_code + 1u);
    uint16_t first_free = (uint16_t)(eoi_code + 1u);
    uint16_t next_code = first_free;
    uint32_t code_size = (uint32_t)min_cs + 1u;
    uint16_t code_mask = (uint16_t)((1u << code_size) - 1u);
    int32_t prev_code = -1;
    uint8_t first_char = 0u;

    uint32_t bitbuf = 0u;
    uint32_t bitcnt = 0u;
    uint8_t sb_rem = 0u;

    uint32_t total_pixels = (uint32_t)fw * (uint32_t)fh;
    uint32_t px_written = 0u;
    uint32_t cur_x = 0u;
    uint32_t cur_y = 0u;
    int interlace_pass = 0;
    static const uint32_t il_y0[4] = {0u, 4u, 2u, 1u};
    static const uint32_t il_dy[4] = {8u, 8u, 4u, 2u};
    if (interlaced) {
        while (interlace_pass < 4 && il_y0[interlace_pass] >= (uint32_t)fh) interlace_pass++;
        cur_y = (interlace_pass < 4) ? il_y0[interlace_pass] : (uint32_t)fh;
    }

    size_t stride = (size_t)screen_w * 4u;

    for (;;) {
        while (bitcnt < code_size) {
            if (sb_rem == 0u) {
                if (pos >= len) return IM_IMAGE_TRUNCATED;
                sb_rem = data[pos++];
                if (sb_rem == 0u) return IM_IMAGE_INVALID;
                if (len - pos < (size_t)sb_rem) return IM_IMAGE_TRUNCATED;
            }
            bitbuf |= ((uint32_t)data[pos++] << bitcnt);
            bitcnt += 8u;
            sb_rem--;
        }

        uint16_t code = (uint16_t)(bitbuf & code_mask);
        bitbuf >>= code_size;
        bitcnt -= code_size;

        if (code == clear_code) {
            next_code = first_free;
            code_size = (uint32_t)min_cs + 1u;
            code_mask = (uint16_t)((1u << code_size) - 1u);
            prev_code = -1;
            continue;
        }
        if (code == eoi_code) {
            break;
        }

        uint32_t sp = 0u;
        uint16_t cur = code;
        if (cur >= next_code) {
            if (cur != next_code || prev_code < 0) return IM_IMAGE_INVALID;
            lzw->stack[sp++] = first_char;
            cur = (uint16_t)prev_code;
        }

        while (cur >= clear_code) {
            if (sp >= 4096u || cur >= next_code) return IM_IMAGE_INVALID;
            lzw->stack[sp++] = lzw->suffix[cur];
            cur = lzw->prefix[cur];
        }
        first_char = (uint8_t)cur;
        if (sp >= 4096u) return IM_IMAGE_INVALID;
        lzw->stack[sp++] = first_char;

        if (prev_code >= 0 && next_code < 4096u) {
            lzw->prefix[next_code] = (uint16_t)prev_code;
            lzw->suffix[next_code] = first_char;
            next_code++;
            if (next_code > code_mask && code_size < 12u) {
                code_size++;
                code_mask = (uint16_t)((1u << code_size) - 1u);
            }
        }
        prev_code = (int32_t)code;

        if (px_written + sp > total_pixels) return IM_IMAGE_INVALID;
        while (sp > 0u) {
            uint8_t idx = lzw->stack[--sp];
            if (pal && (uint16_t)idx >= pal_count) return IM_IMAGE_INVALID;
            if (canvas && !(has_trans && idx == trans_index)) {
                uint32_t dy = (uint32_t)top + cur_y;
                uint32_t dx = (uint32_t)left + cur_x;
                uint8_t r = pal ? pal[(size_t)idx * 3u + 0u] : idx;
                uint8_t g = pal ? pal[(size_t)idx * 3u + 1u] : idx;
                uint8_t b = pal ? pal[(size_t)idx * 3u + 2u] : idx;
                write_pixel(canvas + (size_t)dy * stride + (size_t)dx * 4u, fmt, r, g, b, 255u);
            }
            px_written++;
            cur_x++;
            if (cur_x == (uint32_t)fw) {
                cur_x = 0u;
                if (!interlaced) {
                    cur_y++;
                } else {
                    cur_y += il_dy[interlace_pass];
                    while (cur_y >= (uint32_t)fh && interlace_pass < 4) {
                        interlace_pass++;
                        if (interlace_pass < 4) cur_y = il_y0[interlace_pass];
                    }
                }
            }
        }
    }

    if (px_written != total_pixels) return IM_IMAGE_INVALID;
    /* Allow only residual bits in the last consumed byte; reject surplus bytes in the current
       sub-block or any further non-empty sub-block before the 0x00 block terminator. */
    if (sb_rem != 0u) return IM_IMAGE_INVALID;
    if (pos >= len) return IM_IMAGE_TRUNCATED;
    if (data[pos] != 0u) return IM_IMAGE_INVALID;
    pos++;

    *pos_io = pos;
    return IM_IMAGE_OK;
}

static void clear_rect_transparent(uint8_t *canvas, uint32_t screen_w,
                                   uint16_t left, uint16_t top, uint16_t fw, uint16_t fh) {
    size_t stride = (size_t)screen_w * 4u;
    size_t row_bytes = (size_t)fw * 4u;
    for (uint32_t y = 0u; y < (uint32_t)fh; y++) {
        gif_zero(canvas + (size_t)((uint32_t)top + y) * stride + (size_t)left * 4u, row_bytes);
    }
}

static void save_rect(uint8_t *dst_rect, const uint8_t *canvas, uint32_t screen_w,
                      uint16_t left, uint16_t top, uint16_t fw, uint16_t fh) {
    size_t stride = (size_t)screen_w * 4u;
    size_t row_bytes = (size_t)fw * 4u;
    for (uint32_t y = 0u; y < (uint32_t)fh; y++) {
        gif_copy(dst_rect + (size_t)y * row_bytes,
                 canvas + (size_t)((uint32_t)top + y) * stride + (size_t)left * 4u,
                 row_bytes);
    }
}

static void restore_rect(uint8_t *canvas, const uint8_t *src_rect, uint32_t screen_w,
                         uint16_t left, uint16_t top, uint16_t fw, uint16_t fh) {
    size_t stride = (size_t)screen_w * 4u;
    size_t row_bytes = (size_t)fw * 4u;
    for (uint32_t y = 0u; y < (uint32_t)fh; y++) {
        gif_copy(canvas + (size_t)((uint32_t)top + y) * stride + (size_t)left * 4u,
                 src_rect + (size_t)y * row_bytes,
                 row_bytes);
    }
}

void im_gif_anim_free(const im_gif_alloc *alloc, im_gif_anim *anim) {
    if (!anim) return;
    if (alloc && alloc->free && anim->frames) {
        for (uint32_t i = 0u; i < anim->frame_count; i++) {
            if (anim->frames[i].image.pixels) {
                alloc->free(alloc->ctx, anim->frames[i].image.pixels, anim->frames[i].image.byte_size);
            }
        }
        alloc->free(alloc->ctx, anim->frames, (size_t)anim->frame_cap * sizeof(im_gif_frame));
    }
    gif_zero(anim, sizeof(*anim));
}

static im_image_error decode_core(const uint8_t *data, size_t len,
                                  im_pixel_format format,
                                  const im_image_limits *limits,
                                  bool want_anim,
                                  uint32_t max_frames_arg,
                                  const im_gif_alloc *alloc,
                                  im_image *out_single,
                                  im_gif_anim *out_anim) {
    if (out_single) gif_zero(out_single, sizeof(*out_single));
    if (out_anim) gif_zero(out_anim, sizeof(*out_anim));
    if (!data || !alloc || !alloc->alloc || !alloc->free) return IM_IMAGE_BAD_ARG;
    if (want_anim && !out_anim) return IM_IMAGE_BAD_ARG;
    if (!want_anim && !out_single) return IM_IMAGE_BAD_ARG;
    if (format != IM_PIXEL_BGRA8 && format != IM_PIXEL_RGBA8) return IM_IMAGE_BAD_ARG;

    im_gif_info info;
    im_image_error err = im_gif_probe(data, len, limits, &info);
    if (err != IM_IMAGE_OK) return err;

    size_t pos = 13u;
    const uint8_t *gct = NULL;
    uint16_t gct_count = 0u;
    if (info.has_gct) {
        size_t gct_bytes = (size_t)info.gct_size * 3u;
        if (len - pos < gct_bytes) return IM_IMAGE_TRUNCATED;
        gct = data + pos;
        gct_count = info.gct_size;
        pos += gct_bytes;
    }

    size_t max_total_bytes = (limits && limits->max_bytes > 0u) ? limits->max_bytes : IM_IMAGE_DEFAULT_MAX_BYTES;
    uint32_t max_frames = (max_frames_arg == 0u) ? IM_GIF_DEFAULT_MAX_FRAMES : max_frames_arg;

    gif_lzw *lzw = (gif_lzw *)alloc->alloc(alloc->ctx, sizeof(gif_lzw));
    if (!lzw) return IM_IMAGE_NO_MEMORY;

    uint8_t *canvas = (uint8_t *)alloc->alloc(alloc->ctx, info.byte_size);
    if (!canvas) {
        alloc->free(alloc->ctx, lzw, sizeof(gif_lzw));
        return IM_IMAGE_NO_MEMORY;
    }
    gif_zero(canvas, info.byte_size);

    uint8_t *prev_canvas = NULL;
    uint8_t *frame0_pixels = NULL;
    im_gif_anim anim;
    gif_zero(&anim, sizeof(anim));
    anim.width = info.width;
    anim.height = info.height;

    gif_gce gce;
    gif_zero(&gce, sizeof(gce));
    uint32_t frame_index = 0u;
    bool saw_trailer = false;
    im_image_error result = IM_IMAGE_OK;

    while (pos < len) {
        uint8_t intro = data[pos++];
        if (intro == 0x3Bu) {
            saw_trailer = true;
            if (pos < len) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }
            break;
        } else if (intro == 0x21u) {
            if (pos >= len) {
                result = IM_IMAGE_TRUNCATED;
                goto fail;
            }
            uint8_t label = data[pos++];
            if (label == 0xF9u) {
                /* Graphic Control Extension: fixed 4-byte block + 0x00 terminator. */
                if (len - pos < 6u) {
                    result = IM_IMAGE_TRUNCATED;
                    goto fail;
                }
                if (data[pos] != 4u || data[pos + 5u] != 0u) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                uint8_t g_packed = data[pos + 1u];
                uint8_t disp = (uint8_t)((g_packed >> 2) & 0x07u);
                if (disp > 3u) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                gce.valid = true;
                gce.disposal = disp;
                gce.user_input = (g_packed & 0x02u) != 0u;
                gce.has_trans = (g_packed & 0x01u) != 0u;
                gce.delay_ms = (uint32_t)rd_le16(data + pos + 2u) * 10u;
                gce.trans_index = data[pos + 4u];
                pos += 6u;
            } else if (label == 0x01u) {
                /* Plain Text Extension: fixed 12-byte header, skipped text, consumes pending GCE per GIF89a §§23,25. */
                if (pos >= len) {
                    result = IM_IMAGE_TRUNCATED;
                    goto fail;
                }
                if (data[pos] != 12u) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                if (len - pos < 13u) {
                    result = IM_IMAGE_TRUNCATED;
                    goto fail;
                }
                pos += 13u;
                result = skip_subblocks(data, len, &pos);
                if (result != IM_IMAGE_OK) goto fail;
                gif_zero(&gce, sizeof(gce));
            } else if (label == 0xFFu) {
                /* Application Extension: fixed 11-byte header, preserves pending GCE. */
                if (pos >= len) {
                    result = IM_IMAGE_TRUNCATED;
                    goto fail;
                }
                if (data[pos] != 11u) {
                    result = IM_IMAGE_INVALID;
                    goto fail;
                }
                if (len - pos < 12u) {
                    result = IM_IMAGE_TRUNCATED;
                    goto fail;
                }
                const uint8_t *app_id = data + pos + 1u;
                bool is_loop_app = bytes_eq(app_id, "NETSCAPE2.0", 11u) || bytes_eq(app_id, "ANIMEXTS1.0", 11u);
                pos += 12u;
                bool first_sb = true;
                for (;;) {
                    if (pos >= len) {
                        result = IM_IMAGE_TRUNCATED;
                        goto fail;
                    }
                    uint8_t sb_len = data[pos++];
                    if (sb_len == 0u) break;
                    if (len - pos < (size_t)sb_len) {
                        result = IM_IMAGE_TRUNCATED;
                        goto fail;
                    }
                    if (is_loop_app && first_sb && sb_len == 3u && data[pos] == 1u) {
                        anim.has_loop_count = true;
                        anim.loop_count = (uint32_t)rd_le16(data + pos + 1u);
                    }
                    first_sb = false;
                    pos += (size_t)sb_len;
                }
            } else {
                /* Comment (0xFE) or other extension: skip sub-blocks, preserve pending GCE. */
                result = skip_subblocks(data, len, &pos);
                if (result != IM_IMAGE_OK) goto fail;
            }
        } else if (intro == 0x2Cu) {
            /* Image Descriptor: 9 bytes. */
            if (len - pos < 9u) {
                result = IM_IMAGE_TRUNCATED;
                goto fail;
            }
            uint16_t left = rd_le16(data + pos);
            uint16_t top = rd_le16(data + pos + 2u);
            uint16_t fw = rd_le16(data + pos + 4u);
            uint16_t fh = rd_le16(data + pos + 6u);
            uint8_t i_packed = data[pos + 8u];
            pos += 9u;

            if (fw == 0u || fh == 0u ||
                (uint32_t)left + (uint32_t)fw > info.width ||
                (uint32_t)top + (uint32_t)fh > info.height) {
                result = IM_IMAGE_INVALID;
                goto fail;
            }

            const uint8_t *pal = gct;
            uint16_t pal_count = gct_count;
            if ((i_packed & 0x80u) != 0u) {
                uint16_t lct_count = (uint16_t)(1u << ((i_packed & 0x07u) + 1u));
                size_t lct_bytes = (size_t)lct_count * 3u;
                if (len - pos < lct_bytes) {
                    result = IM_IMAGE_TRUNCATED;
                    goto fail;
                }
                pal = data + pos;
                pal_count = lct_count;
                pos += lct_bytes;
            }
            /* If neither GCT nor LCT is present, pal remains NULL and decode_lzw_frame uses the
               deterministic grayscale fallback palette (index i -> RGB(i, i, i)) per GIF89a §11. */

            bool interlaced = (i_packed & 0x40u) != 0u;
            uint8_t disp = gce.valid ? gce.disposal : 0u;
            bool user_input = gce.valid ? gce.user_input : false;
            bool has_trans = gce.valid ? gce.has_trans : false;
            uint8_t trans_idx = gce.valid ? gce.trans_index : 0u;
            uint32_t delay_ms = gce.valid ? gce.delay_ms : 0u;
            gif_zero(&gce, sizeof(gce));

            if (want_anim) {
                /* Enforce max_frames and cumulative decoded byte budget BEFORE allocating this frame. */
                if (frame_index >= max_frames) {
                    result = IM_IMAGE_TOO_LARGE;
                    goto fail;
                }
                uint64_t next_total = (uint64_t)(frame_index + 1u) * (uint64_t)info.byte_size;
                if (next_total / (uint64_t)(frame_index + 1u) != (uint64_t)info.byte_size ||
                    next_total > (uint64_t)max_total_bytes) {
                    result = IM_IMAGE_TOO_LARGE;
                    goto fail;
                }
                if (disp == 3u) {
                    if (!prev_canvas) {
                        prev_canvas = (uint8_t *)alloc->alloc(alloc->ctx, info.byte_size);
                        if (!prev_canvas) {
                            result = IM_IMAGE_NO_MEMORY;
                            goto fail;
                        }
                    }
                    save_rect(prev_canvas, canvas, info.width, left, top, fw, fh);
                }
            }

            uint8_t *draw_target = (want_anim || frame_index == 0u) ? canvas : NULL;
            result = decode_lzw_frame(data, len, &pos, lzw, info.width, left, top, fw, fh, interlaced,
                                      pal, pal_count, has_trans, trans_idx, format, draw_target);
            if (result != IM_IMAGE_OK) goto fail;

            if (want_anim) {
                if (anim.frame_count == anim.frame_cap) {
                    uint32_t new_cap = anim.frame_cap ? (anim.frame_cap * 2u) : 4u;
                    if (new_cap > max_frames) new_cap = max_frames;
                    if (new_cap <= anim.frame_cap) new_cap = anim.frame_cap + 1u;
                    im_gif_frame *nf = (im_gif_frame *)alloc->alloc(alloc->ctx, (size_t)new_cap * sizeof(im_gif_frame));
                    if (!nf) {
                        result = IM_IMAGE_NO_MEMORY;
                        goto fail;
                    }
                    for (uint32_t k = 0u; k < anim.frame_count; k++) nf[k] = anim.frames[k];
                    if (anim.frames) {
                        alloc->free(alloc->ctx, anim.frames, (size_t)anim.frame_cap * sizeof(im_gif_frame));
                    }
                    anim.frames = nf;
                    anim.frame_cap = new_cap;
                }

                uint8_t *fpix = (uint8_t *)alloc->alloc(alloc->ctx, info.byte_size);
                if (!fpix) {
                    result = IM_IMAGE_NO_MEMORY;
                    goto fail;
                }
                gif_copy(fpix, canvas, info.byte_size);
                im_gif_frame *fr = &anim.frames[anim.frame_count++];
                fr->image.width = info.width;
                fr->image.height = info.height;
                fr->image.stride = info.width * 4u;
                fr->image.format = format;
                fr->image.pixels = fpix;
                fr->image.byte_size = info.byte_size;
                fr->delay_ms = delay_ms;
                fr->disposal = disp;
                fr->user_input = user_input;
                fr->left = left;
                fr->top = top;
                fr->frame_width = fw;
                fr->frame_height = fh;

                if (disp == 2u) {
                    clear_rect_transparent(canvas, info.width, left, top, fw, fh);
                } else if (disp == 3u) {
                    restore_rect(canvas, prev_canvas, info.width, left, top, fw, fh);
                }
            } else if (frame_index == 0u) {
                frame0_pixels = canvas;
                canvas = NULL;
            }

            frame_index++;
        } else {
            result = IM_IMAGE_INVALID;
            goto fail;
        }
    }

    if (!saw_trailer) {
        result = IM_IMAGE_TRUNCATED;
        goto fail;
    }
    if (frame_index == 0u) {
        /* Valid GIF89a §11 palette-only stream with no image frames has no returnable picture. */
        result = IM_IMAGE_UNSUPPORTED;
        goto fail;
    }

    if (prev_canvas) alloc->free(alloc->ctx, prev_canvas, info.byte_size);
    if (canvas) alloc->free(alloc->ctx, canvas, info.byte_size);
    alloc->free(alloc->ctx, lzw, sizeof(gif_lzw));

    if (want_anim) {
        *out_anim = anim;
    } else {
        out_single->width = info.width;
        out_single->height = info.height;
        out_single->stride = info.width * 4u;
        out_single->format = format;
        out_single->pixels = frame0_pixels;
        out_single->byte_size = info.byte_size;
    }
    return IM_IMAGE_OK;

fail:
    if (want_anim) im_gif_anim_free(alloc, &anim);
    if (frame0_pixels) alloc->free(alloc->ctx, frame0_pixels, info.byte_size);
    if (prev_canvas) alloc->free(alloc->ctx, prev_canvas, info.byte_size);
    if (canvas) alloc->free(alloc->ctx, canvas, info.byte_size);
    alloc->free(alloc->ctx, lzw, sizeof(gif_lzw));
    if (out_single) gif_zero(out_single, sizeof(*out_single));
    if (out_anim) gif_zero(out_anim, sizeof(*out_anim));
    return result;
}

im_image_error im_gif_decode(const uint8_t *data, size_t len,
                             im_pixel_format format,
                             const im_image_limits *limits,
                             const im_gif_alloc *alloc,
                             im_image *out_image) {
    return decode_core(data, len, format, limits, false, 0u, alloc, out_image, NULL);
}

im_image_error im_gif_decode_anim(const uint8_t *data, size_t len,
                                  im_pixel_format format,
                                  const im_image_limits *limits,
                                  uint32_t max_frames,
                                  const im_gif_alloc *alloc,
                                  im_gif_anim *out_anim) {
    return decode_core(data, len, format, limits, true, max_frames, alloc, NULL, out_anim);
}
