/* PotentialPunch (PotentialBreaks): a transient shaper for MPC OS standalone devices.
 * ATTACK makes the start of every hit louder (+) or softer (-); SUSTAIN does the same for the tail after it.
 * Level-independent: it reacts to how fast the level rises and falls, not to how loud the track is, so no threshold.
 *   SPEED  TIGHT / MEDIUM / SLOW: how long the "attack" part lasts and how long the tail is followed
 *   FOCUS  FULL / LOWS / HIGHS: what the detector listens to (the whole signal is always shaped)
 *   CLIP   a soft clipper after the shaper, to catch the extra peaks (and add a little bite)
 *   AUTO LEVEL (1.3.0)  measures the loudness in and out and evens it up, so a punchier setting isn't just louder
 * then dry/wet MIX and OUTPUT. A PRESET list (also MPC's PRESET menu) sets them all in one tap.
 * 44.1 kHz, interleaved int16 stereo, 128-frame blocks (wrapper/engine.h). */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine.h"
#include "../shared/looks/potential_theme.h"   /* the LOOK switch (parameter "theme") */

#define FS 44100.0f
#define PI_F 3.14159265f
#define MAX_DB 18.0f          /* the most the gain moves either way */

/* ---- parameters: same order and keys as vst/params.json ---- */
enum { P_OLD_PRESET, P_ATTACK, P_SUSTAIN, P_SPEED, P_FOCUS, P_CLIP, P_MIX, P_OUT, P_PRESET, P_DICE, P_WILD, P_MATCH, NP };
static const char *KEYS[NP] = {"preset", "attack", "sustain", "speed", "focus", "clip", "mix", "output", "preset_pick", "dice", "wild",
                               "match"};

/* ---- presets: attack, sustain, speed, focus, clip, mix, output (same order as the preset param's options) ---- */
#define NPRESET 19
static const float PRESETS[NPRESET][7] = {
    { 45, -15, 1, 0, 1, 100,  0},   /* Punchy Drums */
    { 60, -25, 0, 1, 0, 100, -1},   /* Kick Snap */
    { 55,  10, 0, 2, 0, 100, -1},   /* Snare Crack */
    { 50,  15, 1, 0, 1, 100, -1},   /* Break Smack */
    { 20, -60, 1, 0, 0, 100,  0},   /* Dry Drums */
    {-10,  55, 2, 0, 0, 100, -2},   /* Big Room */
    { 40, -35, 0, 2, 0, 100,  0},   /* Hat Tick */
    {-50,  10, 1, 0, 0, 100,  1},   /* Soft Hits */
    { 35, -25, 1, 1, 0, 100,  0},   /* Bass Pluck */
    { 25,   0, 0, 0, 0,  80,  0},   /* Keys & Guitar */
    {-30,  45, 2, 0, 0, 100, -2},   /* Lush Sustain */
    {  0,   0, 1, 0, 0, 100,  0},   /* Flat */
    /* 1.3.0: drum-bus pairs. Pick the same name in PotentialHeat, put Punch first and Heat after it on the bus */
    { 35, -10, 1, 0, 1, 100,  0},   /* Boom Bap Bus */
    { 15,  25, 2, 1, 0, 100,  0},   /* Lo-Fi Bus */
    { 55, -25, 0, 0, 1, 100,  0},   /* Modern Bus */
    { 10,  10, 1, 0, 0,  60,  0},   /* Glue Bus */
    /* 1.4.0: FX chains. Pick the same name in PotentialPatina, PotentialGrit and PotentialBleed (see the README) */
    { 30,   0, 0, 0, 0,  80,  0},   /* 90s Stab: chain Y (Grit CRUSH, Punch, Bleed TINES, Patina '90s CASSETTE) */
    { 50,  15, 1, 0, 1, 100, -1},   /* Break Bus: chain W (Punch, Grit DUST, Bleed WIRES, Patina '70s TAPE) */
    { 60, -25, 0, 1, 0, 100, -1},   /* Dub Drums: chain Z (Patina '80s SAMPLER, Punch, Bleed ROOM, Grit ECHO) */
};

/* speed: how long the slow follower takes to catch up (= length of the "attack" part), the short release,
 * and the long release that follows the tail */
static const float SPD_ATT[3] = {0.008f, 0.020f, 0.045f};
static const float SPD_REL[3] = {0.030f, 0.060f, 0.120f};
static const float SPD_SUS[3] = {0.150f, 0.300f, 0.600f};

static inline float tc(float sec) { return 1.0f - expf(-1.0f / (sec * FS)); }
static inline float onepole_k(float hz) { return 1.0f - expf(-2.0f * PI_F * hz / FS); }
/* fast log2 (about 0.01 dB error is plenty for a detector) */
static inline float flog2(float x) {
    union { float f; uint32_t i; } u = {x};
    float e = (float)(int)((u.i >> 23) & 255) - 128.0f;
    u.i = (u.i & 0x007fffffu) | 0x3f800000u;
    float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}
static inline float todb(float x) { return 6.0205999f * flog2(x + 1e-9f); }


/* ---- AUTO LEVEL: measures how loud the dry and processed sound are (about the last second) and turns the processed
 * sound by the difference, so louder never just sounds better. Holds still in near-silence. ---- */
typedef struct { float e_in, e_out, g; } lm_t;
static inline float lm_step(lm_t *m, float p_in, float p_out, float k_e, float k_g, float g_lo, float g_hi) {
    m->e_in += (p_in - m->e_in) * k_e; m->e_out += (p_out - m->e_out) * k_e;
    if (m->e_in > 1e-5f && m->e_out > 1e-9f) {   /* above about -50 dBFS */
        float want = sqrtf(m->e_in / m->e_out);
        want = want < g_lo ? g_lo : want > g_hi ? g_hi : want;
        m->g += (want - m->g) * k_g;
    }
    return m->g;
}

typedef struct {
    float p[NP];
    int pending;                   /* keys to report to the host after a preset (bit i = param i) */
    /* detector */
    float lp1, lp2, hp_lp1, hp_lp2;
    float e_fast, e_slow, e_hold, e_short;
    float g_db;                    /* smoothed gain change, dB */
    float s_mix, s_out, s_a, s_s;  /* smoothed controls */
    /* display */
    float win_hi, win_lo, punch_hold, tail_hold, level_hold;
    int win_n, meter, slow;
    unsigned rev;
    lm_t lm; float s_lm;   /* AUTO LEVEL */
    pt_theme_t theme;   /* which look MPC loads next time: not part of the saved state */
} punch_t;

static uint32_t dice_rng = 0x9e3779b9u;
static float dice_u(void) { dice_rng ^= dice_rng << 13; dice_rng ^= dice_rng >> 17; dice_rng ^= dice_rng << 5; return (dice_rng >> 8) / 16777216.0f; }
static float dice_in(float lo, float hi) { return lo + (hi - lo) * dice_u(); }
static int dice_pick(const int *w, int n) { int t = 0; for (int i = 0; i < n; i++) t += w[i]; float r = dice_u() * (float)t; for (int i = 0; i < n; i++) { if (r < (float)w[i]) return i; r -= (float)w[i]; } return n - 1; }
static float dice_step(float x, float step) { return roundf(x / step) * step; }

static void apply_preset(punch_t *s, int n) {
    if (n < 0 || n >= NPRESET) return;
    s->p[P_PRESET] = (float)n;
    for (int i = 0; i < 7; i++) s->p[P_ATTACK + i] = PRESETS[n][i];
    s->pending |= 0xfe;            /* attack .. output changed */
    s->rev++;
}

static void *create(const char *data_dir) {
    punch_t *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    pt_theme_init(&s->theme, data_dir);
    apply_preset(s, 0);
    s->pending = 0;
    s->p[P_WILD] = 1;
    s->p[P_MATCH] = 1; s->lm.g = 1.0f; s->s_lm = 1.0f;   /* AUTO LEVEL on for a new copy */
    dice_rng ^= (uint32_t)(uintptr_t)s;
    s->s_mix = s->p[P_MIX] / 100.0f; s->s_out = powf(10.0f, s->p[P_OUT] / 20.0f);
    s->s_a = s->p[P_ATTACK] / 100.0f; s->s_s = s->p[P_SUSTAIN] / 100.0f;
    s->meter = 15; s->level_hold = -120.0f;
    return s;
}
static void destroy(void *inst) { free(inst); }
static void midi(void *inst, const uint8_t *msg, int len) { (void)inst; (void)msg; (void)len; }

/* "preset" (parameter 0) is an unused slot since this version: MPC's host flicks parameter 0 on STOP and on Overdub while
 * playing, which reloaded a preset and threw away knob tweaks. The PRESET picker is "preset_pick" (last); a saved
 * project's "preset=" still restores into it. */
static int find(const char *key) {
    for (int i = 0; i < NP; i++) if (!strcmp(key, KEYS[i])) return i;
    return -1;
}
static float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static void clamp_all(punch_t *s) {
    s->p[P_PRESET] = clampf(roundf(s->p[P_PRESET]), 0, NPRESET - 1);
    s->p[P_ATTACK] = clampf(s->p[P_ATTACK], -100, 100);
    s->p[P_SUSTAIN] = clampf(s->p[P_SUSTAIN], -100, 100);
    s->p[P_SPEED] = clampf(roundf(s->p[P_SPEED]), 0, 2);
    s->p[P_FOCUS] = clampf(roundf(s->p[P_FOCUS]), 0, 2);
    s->p[P_CLIP] = clampf(roundf(s->p[P_CLIP]), 0, 1);
    s->p[P_MIX] = clampf(s->p[P_MIX], 0, 100);
    s->p[P_OUT] = clampf(s->p[P_OUT], -12, 12);
    s->p[P_WILD] = clampf(roundf(s->p[P_WILD]), 0, 2);
    s->p[P_MATCH] = clampf(roundf(s->p[P_MATCH]), 0, 1);
    s->p[P_DICE] = 0;
}

/* ---- DICE: ROLL makes a new setting; WILD (MILD / MEDIUM / WILD) says how far it goes ---- */

static void roll(punch_t *s) {
    int w = (int)s->p[P_WILD];
    static const float ATK[3][2] = {{0, 40}, {-40, 70}, {-100, 100}}, SUS[3][2] = {{-30, 20}, {-60, 50}, {-100, 100}},
                       MIX[3] = {80, 60, 40};
    static const int SPD[3][3] = {{5, 5, 0}, {4, 4, 2}, {1, 1, 1}}, FOC[3][3] = {{7, 2, 1}, {4, 3, 3}, {1, 1, 1}};
    s->p[P_ATTACK] = dice_step(dice_in(ATK[w][0], ATK[w][1]), 1);
    s->p[P_SUSTAIN] = dice_step(dice_in(SUS[w][0], SUS[w][1]), 1);
    s->p[P_SPEED] = (float)dice_pick(SPD[w], 3);
    s->p[P_FOCUS] = (float)dice_pick(FOC[w], 3);
    if (w) s->p[P_CLIP] = dice_u() < 0.5f ? 0.0f : 1.0f;
    s->p[P_MIX] = dice_step(dice_in(MIX[w], 100), 1);
    s->p[P_OUT] = 0;
    s->pending |= 0xfe; s->rev++;
}

static void set_param(void *inst, const char *key, const char *val) {
    punch_t *s = inst;
    if (pt_theme_set(&s->theme, key, val)) return;
    if (!strcmp(key, "state")) {   /* "key=value;key=value;..." -- restores values as saved, no preset reload */
        char tmp[512]; snprintf(tmp, sizeof tmp, "%s", val);
        s->p[P_MATCH] = 0;   /* a project from before 1.3.0 had no AUTO LEVEL: it sounds as it did */
        for (char *tok = strtok(tmp, ";"); tok; tok = strtok(NULL, ";")) {
            char *eq = strchr(tok, '=');
            if (!eq) continue;
            *eq = 0;
            int i = find(tok);
            if (i == P_OLD_PRESET) i = P_PRESET;
            if (i >= 0) s->p[i] = (float)atof(eq + 1);
        }
        clamp_all(s); s->rev++;
        return;
    }
    int i = find(key);
    if (i < 0 || i == P_OLD_PRESET) return;
    if (i == P_PRESET) { int n = (int)lroundf((float)atof(val)); if (n != (int)s->p[P_PRESET]) apply_preset(s, n); return; }
    if (i == P_DICE) { if (atof(val) > 0.5) roll(s); return; }   /* a trigger: each press rolls */   /* only on a change: a project reload re-sending the same preset keeps its tweaks */
    s->p[i] = (float)atof(val);
    clamp_all(s);
}

int mpc_engine_changed(void *inst, char *key, int len) {
    punch_t *s = inst;
    for (int i = 0; i < NP; i++)
        if (s->pending & (1 << i)) { s->pending &= ~(1 << i); snprintf(key, len, "%s", KEYS[i]); return 1; }
    return 0;
}

static const char *PRESET_NAMES[NPRESET] = {"Punchy Drums", "Kick Snap", "Snare Crack", "Break Smack", "Dry Drums", "Big Room",
                                            "Hat Tick", "Soft Hits", "Bass Pluck", "Keys & Guitar", "Lush Sustain", "Flat",
                                            "Boom Bap Bus", "Lo-Fi Bus", "Modern Bus", "Glue Bus",
                                            "90s Stab", "Break Bus", "Dub Drums"};

static int get_param(void *inst, const char *key, char *buf, int buf_len) {
    punch_t *s = inst;
    { int pt_n = pt_theme_get(&s->theme, key, buf, buf_len); if (pt_n) return pt_n; }
    if (!strcmp(key, "state")) {
        int n = 0;
        for (int i = 1; i < NP && n < buf_len; i++) n += snprintf(buf + n, buf_len - n, "%s=%g;", KEYS[i], (double)s->p[i]);
        return n;
    }
    if (!strcmp(key, "display_rev")) return snprintf(buf, buf_len, "%u", s->rev);
    if (!strcmp(key, "meter")) return snprintf(buf, buf_len, "%d", s->meter);
    if (!strcmp(key, "reading")) return snprintf(buf, buf_len, "TAP A PRESET, THEN SHAPE ATTACK AND SUSTAIN  -  THE METER SHOWS THE CHANGE");   /* fixed: live text would need UpdateDisplays, which close MPC's PRESET menu */
    if (!strncmp(key, "preset:", 7) || !strncmp(key, "preset_pick:", 12)) {
        int n = atoi(strchr(key, ':') + 1);
        if (n >= 0 && n < NPRESET) return snprintf(buf, buf_len, "%s", PRESET_NAMES[n]);
        return 0;
    }
    int i = find(key);
    if (i < 0) return 0;
    if (i == P_OLD_PRESET) i = P_PRESET;
    if (i == P_DICE) return snprintf(buf, buf_len, "0");
    if (i == P_PRESET || i == P_SPEED || i == P_FOCUS || i == P_CLIP || i == P_WILD || i == P_MATCH) return snprintf(buf, buf_len, "%d", (int)lroundf(s->p[i]));
    return snprintf(buf, buf_len, "%g", (double)s->p[i]);
}

static void render(void *inst, int16_t *out_lr, int frames) {   /* an effect: silence without input */
    (void)inst;
    memset(out_lr, 0, (size_t)frames * 2 * sizeof(int16_t));
}

static inline float follow(float env, float x, float ka, float kr) { return env + (x - env) * (x > env ? ka : kr); }
static inline float soft_clip(float x) {    /* clean below 0.7, rounds off above, never past 1.0 */
    float a = fabsf(x);
    if (a <= 0.7f) return x;
    float y = 0.7f + 0.3f * tanhf((a - 0.7f) / 0.3f);
    return x < 0 ? -y : y;
}

static void process(void *inst, const int16_t *in_lr, int16_t *out_lr, int frames) {
    punch_t *s = inst;
    int spd = (int)s->p[P_SPEED], focus = (int)s->p[P_FOCUS], clip = (int)s->p[P_CLIP];
    const float k_fast_a = tc(0.0002f), k_slow_a = tc(SPD_ATT[spd]), k_rel = tc(SPD_REL[spd]), k_sus = tc(SPD_SUS[spd]);
    const float k_lp = onepole_k(180.0f), k_hp = onepole_k(1800.0f), k_g = tc(0.0005f), k_c = tc(0.02f);
    const float t_mix = s->p[P_MIX] / 100.0f, t_out = powf(10.0f, s->p[P_OUT] / 20.0f);
    const float t_a = s->p[P_ATTACK] / 100.0f, t_s = s->p[P_SUSTAIN] / 100.0f;
    const float k_e = tc(0.8f), k_lg = tc(0.25f);
    const int matching = s->p[P_MATCH] > 0.5f;

    for (int n = 0; n < frames; n++) {
        float l = in_lr[2 * n] * (1.0f / 32768.0f), r = in_lr[2 * n + 1] * (1.0f / 32768.0f);
        s->s_mix += (t_mix - s->s_mix) * k_c; s->s_out += (t_out - s->s_out) * k_c;
        s->s_a += (t_a - s->s_a) * k_c; s->s_s += (t_s - s->s_s) * k_c;

        /* detector input: the louder channel, through the focus filter */
        float d = 0.5f * (l + r), m = fabsf(l) > fabsf(r) ? fabsf(l) : fabsf(r);
        if (focus == 1) {          /* lows: two one-pole low-passes */
            s->lp1 += (d - s->lp1) * k_lp; s->lp2 += (s->lp1 - s->lp2) * k_lp; m = fabsf(s->lp2) * 1.6f;
        } else if (focus == 2) {   /* highs: input minus two low-passes */
            s->hp_lp1 += (d - s->hp_lp1) * k_hp; s->hp_lp2 += (s->hp_lp1 - s->hp_lp2) * k_hp; m = fabsf(d - s->hp_lp2) * 1.6f;
        }
        s->e_fast = follow(s->e_fast, m, k_fast_a, k_rel);
        s->e_slow = follow(s->e_slow, m, k_slow_a, k_rel);
        s->e_hold = follow(s->e_hold, m, k_fast_a, k_sus);
        s->e_short = follow(s->e_short, m, k_fast_a, k_rel);

        float lvl = todb(s->e_hold);
        float att_db = todb(s->e_fast) - todb(s->e_slow); if (att_db < 0) att_db = 0; if (att_db > 24) att_db = 24;
        float sus_db = lvl - todb(s->e_short); if (sus_db < 0) sus_db = 0; if (sus_db > 24) sus_db = 24;
        float gate = clampf((lvl + 66.0f) / 6.0f, 0.0f, 1.0f);   /* leave near-silence alone (no noise pumping) */
        float pa = s->s_a * att_db * 1.1f * gate, ps = s->s_s * sus_db * 0.9f * gate;
        float target = clampf(pa + ps, -MAX_DB, MAX_DB);
        s->g_db += (target - s->g_db) * k_g;
        float g = exp2f(s->g_db * 0.16609640f);   /* dB -> gain */

        float yl = l * g, yr = r * g;
        if (clip) { yl = soft_clip(yl); yr = soft_clip(yr); }
        {   /* AUTO LEVEL: the shaped sound evened up with the dry (-12 to +6 dB) */
            float gm = lm_step(&s->lm, 0.5f * (l * l + r * r), 0.5f * (yl * yl + yr * yr), k_e, k_lg, 0.25f, 2.0f);
            s->s_lm += ((matching ? gm : 1.0f) - s->s_lm) * k_c;
            yl *= s->s_lm; yr *= s->s_lm;
        }
        float ol = (l + (yl - l) * s->s_mix) * s->s_out, orr = (r + (yr - r) * s->s_mix) * s->s_out;
        if (!(ol == ol)) ol = 0;
        if (!(orr == orr)) orr = 0;
        /* safety limiter above -1 dBFS, then a hard stop */
        ol = ol > 0.89f ? 0.89f + 0.11f * tanhf((ol - 0.89f) / 0.11f) : ol < -0.89f ? -0.89f - 0.11f * tanhf((-ol - 0.89f) / 0.11f) : ol;
        orr = orr > 0.89f ? 0.89f + 0.11f * tanhf((orr - 0.89f) / 0.11f) : orr < -0.89f ? -0.89f - 0.11f * tanhf((-orr - 0.89f) / 0.11f) : orr;
        out_lr[2 * n] = (int16_t)lrintf(clampf(ol, -1.0f, 1.0f) * 32767.0f);
        out_lr[2 * n + 1] = (int16_t)lrintf(clampf(orr, -1.0f, 1.0f) * 32767.0f);

        /* display */
        if (s->g_db > s->win_hi) s->win_hi = s->g_db;
        if (s->g_db < s->win_lo) s->win_lo = s->g_db;
        if (fabsf(pa) > fabsf(s->punch_hold)) s->punch_hold = pa;
        if (fabsf(ps) > fabsf(s->tail_hold)) s->tail_hold = ps;
        if (lvl > s->level_hold) s->level_hold = lvl;
    }
    s->win_n += frames;
    if (s->win_n >= 1764) {                       /* every 40 ms */
        float v = fabsf(s->win_hi) >= fabsf(s->win_lo) ? s->win_hi : s->win_lo;
        int mtr = 15 + (int)lroundf(v * (15.0f / MAX_DB));
        mtr = mtr < 0 ? 0 : mtr > 30 ? 30 : mtr;
        if (mtr != s->meter) { s->meter = mtr; s->rev++; }
        s->win_hi = s->win_lo = 0; s->win_n = 0;
        /* readout holds decay slowly so it can be read */
        if (++s->slow >= 8) {                      /* ~320 ms */
            s->slow = 0;
            s->punch_hold *= 0.5f; s->tail_hold *= 0.5f;
            if (fabsf(s->punch_hold) < 0.05f) s->punch_hold = 0;
            if (fabsf(s->tail_hold) < 0.05f) s->tail_hold = 0;
            s->level_hold = s->level_hold > -120.0f ? s->level_hold - 12.0f : -120.0f;
        }
    }
}

static const mpc_engine_t ENGINE = { create, destroy, midi, set_param, get_param, render, process, NULL };
const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
