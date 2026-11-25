/* Impossible UI: perceptual fixed-point D65 CIELAB accent shade derivation. Portable: no float, no libc. */
#include "accent.h"

/* Q24 fixed-point scale (1.0 == 16777216) for linear RGB, XYZ, and Lab intermediate math. */
#define Q24_ONE 16777216LL
#define Q24_HALF 8388608LL

/* Exact sRGB byte [0..255] -> linear light in Q24, rounded half-up from IEC 61966-2-1:
   c <= 0.04045 ? c/12.92 : ((c + 0.055)/1.055)^2.4. */
static const uint32_t k_srgb_to_lin_q24[256] = {
           0u,     5092u,    10185u,    15277u,    20369u,    25462u,    30554u,    35646u,
       40739u,    45831u,    50923u,    56146u,    61682u,    67524u,    73676u,    80144u,
       86931u,    94043u,   101483u,   109255u,   117364u,   125813u,   134607u,   143749u,
      153244u,   163095u,   173306u,   183880u,   194821u,   206133u,   217819u,   229883u,
      242327u,   255157u,   268373u,   281981u,   295983u,   310382u,   325182u,   340386u,
      355996u,   372016u,   388449u,   405298u,   422565u,   440255u,   458369u,   476910u,
      495881u,   515286u,   535127u,   555406u,   576126u,   597291u,   618902u,   640963u,
      663476u,   686443u,   709868u,   733752u,   758099u,   782910u,   808189u,   833938u,
      860159u,   886854u,   914027u,   941680u,   969814u,   998433u,  1027538u,  1057133u,
     1087218u,  1117798u,  1148873u,  1180447u,  1212520u,  1245097u,  1278179u,  1311767u,
     1345865u,  1380475u,  1415598u,  1451237u,  1487394u,  1524071u,  1561270u,  1598994u,
     1637244u,  1676023u,  1715332u,  1755173u,  1795550u,  1836463u,  1877915u,  1919907u,
     1962442u,  2005522u,  2049149u,  2093324u,  2138049u,  2183328u,  2229161u,  2275550u,
     2322497u,  2370005u,  2418074u,  2466708u,  2515908u,  2565675u,  2616012u,  2666920u,
     2718402u,  2770458u,  2823092u,  2876304u,  2930097u,  2984472u,  3039432u,  3094977u,
     3151110u,  3207832u,  3265145u,  3323052u,  3381553u,  3440650u,  3500346u,  3560641u,
     3621538u,  3683038u,  3745144u,  3807855u,  3871176u,  3935106u,  3999648u,  4064803u,
     4130573u,  4196960u,  4263965u,  4331589u,  4399836u,  4468706u,  4538200u,  4608321u,
     4679069u,  4750448u,  4822457u,  4895099u,  4968376u,  5042288u,  5116838u,  5192027u,
     5267856u,  5344328u,  5421443u,  5499204u,  5577611u,  5656667u,  5736372u,  5816729u,
     5897738u,  5979402u,  6061722u,  6144699u,  6228335u,  6312631u,  6397589u,  6483210u,
     6569496u,  6656448u,  6744068u,  6832357u,  6921317u,  7010948u,  7101253u,  7192233u,
     7283889u,  7376223u,  7469237u,  7562930u,  7657306u,  7752366u,  7848110u,  7944540u,
     8041658u,  8139465u,  8237963u,  8337152u,  8437035u,  8537612u,  8638885u,  8740855u,
     8843524u,  8946893u,  9050964u,  9155737u,  9261215u,  9367397u,  9474287u,  9581885u,
     9690192u,  9799210u,  9908940u, 10019383u, 10130542u, 10242416u, 10355008u, 10468318u,
    10582349u, 10697100u, 10812575u, 10928773u, 11045697u, 11163346u, 11281724u, 11400831u,
    11520668u, 11641236u, 11762538u, 11884573u, 12007344u, 12130852u, 12255098u, 12380082u,
    12505807u, 12632274u, 12759484u, 12887438u, 13016137u, 13145583u, 13275776u, 13406719u,
    13538412u, 13670857u, 13804054u, 13938006u, 14072712u, 14208175u, 14344396u, 14481375u,
    14619114u, 14757615u, 14896878u, 15036905u, 15177696u, 15319253u, 15461578u, 15604671u,
    15748533u, 15893166u, 16038571u, 16184750u, 16331702u, 16479430u, 16627934u, 16777216u
};

static int64_t mul_q24(int64_t a, int64_t b) {
    __int128 p = (__int128)a * (__int128)b;
    if (p >= 0) p += Q24_HALF;
    else p -= Q24_HALF;
    return (int64_t)(p >> 24);
}

/* Exact integer cube root of x in Q24 (x >= 0): returns cbrt(x / 2^24) * 2^24. */
static int64_t cbrt_q24(int64_t x) {
    if (x <= 0) return 0;
    /* Want y such that y^3 ~= x * 2^48. y <= 2^26 for x <= 2^30. */
    __int128 target = ((__int128)x) << 48;
    int64_t lo = 0, hi = (1LL << 26);
    while (lo < hi) {
        int64_t mid = lo + ((hi - lo + 1) >> 1);
        __int128 cube = (__int128)mid * (__int128)mid * (__int128)mid;
        if (cube <= target) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

/* CIE Lab forward transfer f(t) in Q24 (epsilon = 216/24389 = 148586, kappa_div_116 = 7.787037 = 130644767, 16/116 = 2314099). */
static int64_t lab_f(int64_t t_q24) {
    if (t_q24 > 148586LL) return cbrt_q24(t_q24);
    return mul_q24(130644767LL, t_q24) + 2314099LL;
}

/* CIE Lab inverse transfer f^-1(u) in Q24 (delta = 6/29 = 3471148, 3*delta^2 = 2154506, 16/116 = 2314099). */
static int64_t lab_finv(int64_t u_q24) {
    if (u_q24 > 3471148LL) {
        int64_t u2 = mul_q24(u_q24, u_q24);
        return mul_q24(u2, u_q24);
    }
    return mul_q24(2154506LL, u_q24 - 2314099LL);
}

static void rgb_to_lab_q24(uint8_t r8, uint8_t g8, uint8_t b8, int64_t *out_L, int64_t *out_a, int64_t *out_b) {
    int64_t rl = (int64_t)k_srgb_to_lin_q24[r8];
    int64_t gl = (int64_t)k_srgb_to_lin_q24[g8];
    int64_t bl = (int64_t)k_srgb_to_lin_q24[b8];

    /* sRGB -> CIE XYZ normalized to D65 white point (X_n=0.95047, Y_n=1.0, Z_n=1.08883) in Q24. */
    int64_t xn = mul_q24(7280684LL, rl) + mul_q24(6311734LL, gl) + mul_q24(3185086LL, bl);
    int64_t yn = mul_q24(3568049LL, rl) + mul_q24(11998249LL, gl) + mul_q24(1210918LL, bl);
    int64_t zn = mul_q24(297942LL, rl) + mul_q24(1836885LL, gl) + mul_q24(14642389LL, bl);

    int64_t fx = lab_f(xn);
    int64_t fy = lab_f(yn);
    int64_t fz = lab_f(zn);

    *out_L = 116LL * fy - 16LL * Q24_ONE;
    *out_a = 500LL * (fx - fy);
    *out_b = 200LL * (fy - fz);
}

static void lab_to_lin_rgb_q24(int64_t L_q24, int64_t a_q24, int64_t b_q24,
                               int64_t *out_r, int64_t *out_g, int64_t *out_b) {
    int64_t fy = (L_q24 + 16LL * Q24_ONE + 58LL) / 116LL;
    int64_t fx = fy + (a_q24 >= 0 ? (a_q24 + 250LL) / 500LL : -((-a_q24 + 250LL) / 500LL));
    int64_t fz = fy - (b_q24 >= 0 ? (b_q24 + 100LL) / 200LL : -((-b_q24 + 100LL) / 200LL));

    /* Multiply by D65 white point (X_n=15946240, Y_n=16777216, Z_n=18267536). */
    int64_t X = mul_q24(lab_finv(fx), 15946240LL);
    int64_t Y = lab_finv(fy);
    int64_t Z = mul_q24(lab_finv(fz), 18267536LL);

    /* CIE XYZ -> linear sRGB matrix in Q24. */
    *out_r = mul_q24(54368186LL, X) - mul_q24(25790925LL, Y) - mul_q24(8364461LL, Z);
    *out_g = mul_q24(-16261423LL, X) + mul_q24(31474208LL, Y) + mul_q24(697170LL, Z);
    *out_b = mul_q24(933548LL, X) - mul_q24(3422014LL, Y) + mul_q24(17736884LL, Z);
}

static uint8_t lin_q24_to_srgb_u8(int64_t lin) {
    if (lin <= 0) return 0u;
    if (lin >= Q24_ONE) return 255u;
    uint32_t u = (uint32_t)lin;
    /* Binary search k_srgb_to_lin_q24 for nearest byte (exact inverse of sRGB transfer). */
    int lo = 0, hi = 255;
    while (lo < hi) {
        int mid = (lo + hi) >> 1;
        if (k_srgb_to_lin_q24[mid] < u) lo = mid + 1;
        else hi = mid;
    }
    if (lo > 0) {
        uint32_t d_hi = k_srgb_to_lin_q24[lo] - u;
        uint32_t d_lo = u - k_srgb_to_lin_q24[lo - 1];
        if (d_lo <= d_hi) return (uint8_t)(lo - 1);
    }
    return (uint8_t)lo;
}

static uint32_t lab_to_gamut_srgb(int64_t L_q24, int64_t a_q24, int64_t b_q24) {
    if (L_q24 <= 0) return 0xFF000000u;
    if (L_q24 >= 100LL * Q24_ONE) return 0xFFFFFFFFu;
    int64_t rl = 0, gl = 0, bl = 0;
    lab_to_lin_rgb_q24(L_q24, a_q24, b_q24, &rl, &gl, &bl);
    if (rl < 0 || gl < 0 || bl < 0 || rl > Q24_ONE || gl > Q24_ONE || bl > Q24_ONE) {
        /* Hue-preserving sRGB gamut chroma bisection in Q24 (24 iterations -> < 1e-7). */
        int64_t lo = 0, hi = Q24_ONE;
        for (int it = 0; it < 24; it++) {
            int64_t mid = (lo + hi) >> 1;
            int64_t tr = 0, tg = 0, tb = 0;
            lab_to_lin_rgb_q24(L_q24, mul_q24(a_q24, mid), mul_q24(b_q24, mid), &tr, &tg, &tb);
            if (tr >= 0 && tg >= 0 && tb >= 0 && tr <= Q24_ONE && tg <= Q24_ONE && tb <= Q24_ONE) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        lab_to_lin_rgb_q24(L_q24, mul_q24(a_q24, lo), mul_q24(b_q24, lo), &rl, &gl, &bl);
    }
    uint8_t r8 = lin_q24_to_srgb_u8(rl);
    uint8_t g8 = lin_q24_to_srgb_u8(gl);
    uint8_t b8 = lin_q24_to_srgb_u8(bl);
    return 0xFF000000u | ((uint32_t)r8 << 16) | ((uint32_t)g8 << 8) | (uint32_t)b8;
}

void im_accent_derive(uint32_t accent_argb, im_accent_shades *out) {
    if (!out) return;
    uint8_t r = (uint8_t)((accent_argb >> 16) & 0xFFu);
    uint8_t g = (uint8_t)((accent_argb >> 8) & 0xFFu);
    uint8_t b = (uint8_t)(accent_argb & 0xFFu);
    out->accent = 0xFF000000u | (accent_argb & 0x00FFFFFFu);

    int64_t L = 0, a = 0, bv = 0;
    rgb_to_lab_q24(r, g, b, &L, &a, &bv);

    /* Perceptual D65 CIELAB lightness shifts matching Fluent accent ramp proportions:
       Light1/2/3 shift toward L*=100 by +10/+22/+34 (proportionally compressed when L* is already high),
       Dark1/2/3 shift toward L*=0 by -10/-22/-34 (proportionally compressed when L* is already low). */
    int64_t up_room = 100LL * Q24_ONE - L;
    int64_t dn_room = L;
    int64_t l1 = L + (up_room > 34LL * Q24_ONE ? 10LL * Q24_ONE : (up_room * 294LL) / 1000LL);
    int64_t l2 = L + (up_room > 34LL * Q24_ONE ? 22LL * Q24_ONE : (up_room * 647LL) / 1000LL);
    int64_t l3 = L + (up_room > 34LL * Q24_ONE ? 34LL * Q24_ONE : (up_room * 900LL) / 1000LL);
    int64_t d1 = L - (dn_room > 34LL * Q24_ONE ? 10LL * Q24_ONE : (dn_room * 294LL) / 1000LL);
    int64_t d2 = L - (dn_room > 34LL * Q24_ONE ? 22LL * Q24_ONE : (dn_room * 647LL) / 1000LL);
    int64_t d3 = L - (dn_room > 34LL * Q24_ONE ? 34LL * Q24_ONE : (dn_room * 900LL) / 1000LL);

    out->light1 = lab_to_gamut_srgb(l1, a, bv);
    out->light2 = lab_to_gamut_srgb(l2, a, bv);
    out->light3 = lab_to_gamut_srgb(l3, a, bv);
    out->dark1  = lab_to_gamut_srgb(d1, a, bv);
    out->dark2  = lab_to_gamut_srgb(d2, a, bv);
    out->dark3  = lab_to_gamut_srgb(d3, a, bv);
}

void im_accent_fill_palette(im_palette *palette, uint32_t accent_argb) {
    if (!palette) return;
    im_accent_shades s;
    im_accent_derive(accent_argb, &s);
    im_accent_set_os_shades(palette, &s);
}

void im_accent_set_os_shades(im_palette *palette, const im_accent_shades *shades) {
    if (!palette || !shades) return;
    palette->slot[IM_SLOT_ACCENT] = shades->accent;
    palette->slot[IM_SLOT_ACCENT_COLOR_LIGHT_1] = shades->light1;
    palette->slot[IM_SLOT_ACCENT_COLOR_LIGHT_2] = shades->light2;
    palette->slot[IM_SLOT_ACCENT_COLOR_LIGHT_3] = shades->light3;
    palette->slot[IM_SLOT_ACCENT_COLOR_DARK_1] = shades->dark1;
    palette->slot[IM_SLOT_ACCENT_COLOR_DARK_2] = shades->dark2;
    palette->slot[IM_SLOT_ACCENT_COLOR_DARK_3] = shades->dark3;
    palette->slot[IM_SLOT_COLOR_CONTROL_ACCENT] = shades->accent;
}
