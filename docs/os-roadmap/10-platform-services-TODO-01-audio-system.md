# From the Impossible OS roadmap: todo/10-platform-services/TODO-01-audio-system.md

These sections as they were on 2026-11-23, when their work moved here (Impossible OS commit 973b8844a). They are a reference: this repository's design and work list win where they differ, names of OS functions in them do not apply here, and an item that is the OS's own work stayed there too.

## 3. WAV Decoder

Vendor `dr_wav.h` (public domain, ~1500 lines) at `include/libs/dr_wav.h`. Redirect `DRWAV_MALLOC/FREE` → `kmalloc/kfree`. `audio_load_wav(path)` → `audio_clip`. Support 8/16-bit, mono/stereo, 22050/44100/48000 Hz.

**Files:** `include/libs/dr_wav.h` (new vendor), `src/kernel/audio_wav.c` (new)

> [!NOTE]
> Define `DRWAV_IMPLEMENTATION` in `audio_wav.c` only. `#define DRWAV_MALLOC(sz) kmalloc(sz)` / `DRWAV_FREE(p) kfree(p)` -- place before `#include "libs/dr_wav.h"`. `audio_load_wav(path)`: `vfs_open()` → `vfs_read()` full file → `drwav_init_memory(&wav, data, size, NULL)` → `drwav_read_pcm_frames_s16()` → fill `audio_clip`. PCM buffer alloc: `samples > 2048` → `pmm_alloc_contiguous(ceil(bytes / 4096))` else `kmalloc`. 8-bit unsigned → `dr_wav` converts to s16 internally via `drwav_read_pcm_frames_s16`. Channel count preserved (mono/stereo); `audio_play()` will handle mono duplication if driver requires stereo. `drwav_uninit()` after decode (dr_wav keeps no references after uninit). `audio_clip_free(clip)` frees PMM or kmalloc buffer based on size flag.

- [ ] Download / vendor `dr_wav.h` to `include/libs/dr_wav.h` (public domain -- David Reid)
- [ ] `src/kernel/audio_wav.c`: `#define DRWAV_IMPLEMENTATION`, `DRWAV_MALLOC/FREE` redirects
- [ ] `audio_load_wav(const char *path)` → `audio_clip *` (PMM or kmalloc by size)
- [ ] Support 8-bit unsigned + 16-bit signed input; mono + stereo; 22050/44100/48000 Hz
- [ ] `audio_clip_free(audio_clip *clip)` -- PMM or kmalloc free based on internal size flag
- [ ] Test: load `C:\Impossible\Media\startup.wav` → play via `audio_play()` → audible chime
- [ ] Commit: `"libs: dr_wav -- WAV decoder, freestanding kmalloc/kfree redirect, audio_load_wav"`

## 4. MP3 Decoder

Vendor `dr_mp3.h` (public domain, ~3000 lines) at `include/libs/dr_mp3.h`. Same redirect pattern. `audio_load_mp3(path)` → `audio_clip`. Linear interpolation resampler if driver rate differs.

**Files:** `include/libs/dr_mp3.h` (new vendor), `src/kernel/audio_mp3.c` (new)

> [!NOTE]
> `DRMP3_MALLOC/REALLOC/FREE` → `kmalloc`/`kmalloc_resize` (or `kmalloc` new + `kfree` old)/`kfree`. `audio_load_mp3()`: VFS read full file → `drmp3_init_memory()` → `drmp3_read_pcm_frames_s16()` → `audio_clip`. MP3 is commonly 44100 Hz stereo MPEG-1 Layer 3; driver may expect 48000 Hz. **Linear resampler**: if `clip.sample_rate != g_audio_dev.sample_rate`: output_samples = `clip.sample_count * target_rate / clip.sample_rate`; for each output index `i`: `float src_pos = i * (float)clip.sample_rate / target_rate`; `int s0 = src_pos`, `frac = src_pos - s0`; `out[i] = pcm[s0] + frac * (pcm[s0+1] - pcm[s0])`. PMM alloc for resampled buffer if > 4 KiB.

- [ ] Vendor `dr_mp3.h` to `include/libs/dr_mp3.h`
- [ ] `src/kernel/audio_mp3.c`: `DRMP3_MALLOC/REALLOC/FREE` redirects + `DRMP3_IMPLEMENTATION`
- [ ] `audio_load_mp3(const char *path)` → `audio_clip *`
- [ ] Linear interpolation resampler: `audio_resample(clip, target_rate)` → resampled `audio_clip`
- [ ] `audio_resample()` called automatically in `audio_load_mp3()` if rate mismatch with `g_audio_dev`
- [ ] PMM alloc for resampled buffer when `output_bytes > 4096`
- [ ] Test: load `.mp3` track → play → audible music output
- [ ] Commit: `"libs: dr_mp3 -- MP3 decoder, freestanding redirects, audio_load_mp3, linear resampler"`

## 5. OGG Vorbis Decoder

Vendor `stb_vorbis.c` as `src/libs/stb_vorbis_impl.c` (same pattern as `stb_truetype`). Redirect `malloc/realloc/free` → `kmalloc/kfree`. `audio_load_ogg(path)` → `audio_clip`.

**Files:** `src/libs/stb_vorbis_impl.c` (new), `include/libs/stb_vorbis.h` (new -- split from single-header)

> [!NOTE]
> `stb_vorbis` is included in the project as `stb_vorbis.c` -- define `STB_VORBIS_IMPLEMENTATION` in `stb_vorbis_impl.c`, set `#define malloc kmalloc`, `#define free kfree`, `#define realloc(p,s) kmalloc(s)` (with kfree old pointer manually if needed -- or use `stb_vorbis_alloc` struct to pass a fixed PMM arena). Preferred: `stb_vorbis_open_memory()` with arena buffer from `pmm_alloc_contiguous()` → no `realloc` calls. `audio_load_ogg(path)`: VFS read → `stb_vorbis_open_memory(data, size, &err, &alloc)` → `stb_vorbis_get_samples_short_interleaved()` → `audio_clip`. Check `stb_vorbis_get_info()` for `sample_rate` + `channels`; apply `audio_resample()` if needed.

- [ ] `src/libs/stb_vorbis_impl.c`: `STB_VORBIS_IMPLEMENTATION`, PMM arena alloc struct
- [ ] `include/libs/stb_vorbis.h` split header (or use `#include "stb_vorbis.c"` approach matching existing `stb_truetype`)
- [ ] `audio_load_ogg(const char *path)` → `audio_clip *`; PMM alloc for decoded PCM
- [ ] `stb_vorbis_alloc` struct backed by `pmm_alloc_contiguous()` -- eliminate `realloc` dependency
- [ ] Apply `audio_resample()` if driver rate differs
- [ ] Test: play `.ogg` audio file
- [ ] Commit: `"libs: stb_vorbis -- OGG decoder, PMM arena alloc, audio_load_ogg"`

## 6. FLAC Decoder (Stretch)

Vendor `dr_flac.h` (public domain, ~5000 lines). Same redirect pattern. `audio_load_flac(path)` → `audio_clip`. Lossless decode; may be large buffers -- PMM mandatory for output.

**Files:** `include/libs/dr_flac.h` (new vendor), `src/kernel/audio_flac.c` (new)

> [!NOTE]
> `DRFLAC_MALLOC/REALLOC/FREE` → `kmalloc`/`kmalloc_resize` (alloc new + copy + free old)/`kfree`. FLAC output is 16/24-bit; use `drflac_read_pcm_frames_s16()` for uniform 16-bit output. FLAC files are lossless and can be large (e.g., 50 MB for album track) -- **always PMM** for output buffer regardless of size: `pmm_alloc_contiguous(ceil(output_bytes / 4096))`. Log a warning via `klog_warn()` if `output_bytes > 64 MB`.

- [ ] Vendor `dr_flac.h` to `include/libs/dr_flac.h`
- [ ] `src/kernel/audio_flac.c`: `DRFLAC_MALLOC/REALLOC/FREE` redirects + `DRFLAC_IMPLEMENTATION`
- [ ] `audio_load_flac(const char *path)` → `audio_clip *`; always PMM alloc
- [ ] `drflac_read_pcm_frames_s16()` for uniform 16-bit output
- [ ] `klog_warn()` if decoded buffer > 64 MiB
- [ ] Apply `audio_resample()` if needed
- [ ] Test: play lossless `.flac` file
- [ ] Commit: `"libs: dr_flac -- FLAC decoder, freestanding redirects, audio_load_flac"`

