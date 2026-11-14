/* potential_user.h: the USER preset slots every Potential effect shares (8 slots: tap SAVE, then a slot, to keep the
 * current settings; tap a slot to bring them back).
 *
 * MPC has no "save preset" for a VST2 plugin (mpc-vst-plugins docs/NOTES.md), so the plugin keeps its own. A slot holds
 * the engine's own "state" text (the same text a project saves), minus the keys the engine says are not part of a sound
 * (the ROLL wildness, the built-in preset's number ...).
 *
 * Where: one text file per plugin, "<drive>/PotentialSuite Presets/<Plugin>.txt", beside the "VST2 Plugins" (or Synths)
 * folder the plugin is in, so a plugin update (which replaces the plugin's folder) keeps them, every project and every
 * copy of the plugin on the MPC sees the same slots, and the file can be backed up or shared. One line per filled slot:
 * "<slot number><TAB><state>". The file is read again before every tap, so a slot saved by another copy of the plugin
 * (another track) is there when you tap it. Without a folder (the computer test, a desktop host) the slots work for as
 * long as the plugin is open.
 *
 * Screen (a list widget, key=user, cols=4 rows=2, mark=1, and a button key=user_save on an OFF/ON parameter):
 *   user_1 .. user_8      text "USER n" (filled) or "EMPTY n"; "user_n_on" lights the slot loaded or saved last
 *   user_save             SAVE, lit while armed; a slot tap saves and clears it
 * A list row toggles on every tap, so any set of "user_n" is a tap. Taps in the first 3 seconds after the plugin is
 * created or a project's state arrives are ignored (a project load replays values), and so is the second of two taps on
 * one row within 400 ms (one touch can arrive twice).
 *
 * Use, in an engine:
 *     pu_user_t user;                                                     in the instance
 *     pu_init(&s->user, data_dir, "PotentialPunch");                      in create()
 *     pu_quiet(&s->user);                                                 when a project's "state" arrives
 *     int n, r = pu_set(&s->user, key, val, &n);                          first in set_param() (after the theme)
 *     if (r == PU_SAVE) { char st[PU_STATE_MAX]; get_state(s, st, sizeof st); pu_store(&s->user, n, st, "dice,wild"); }
 *     else if (r == PU_LOAD) { restore_state(s, pu_slot(&s->user, n)); report every value to the host }
 *     if (r) return;
 *     n = pu_get(&s->user, key, buf, buf_len); if (n) return n;           first in get_param() (after the theme)
 *     if (pu_changed(&s->user, key, len)) return 1;                       first in mpc_engine_changed()
 */
#ifndef POTENTIAL_USER_H
#define POTENTIAL_USER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define PU_SLOTS 8
#define PU_STATE_MAX 1024
#define PU_QUIET_MS 3000
#define PU_TWICE_MS 400

enum { PU_NONE = 0, PU_DONE, PU_SAVE, PU_LOAD };

typedef struct {
    char file[800];                     /* "<drive>/PotentialSuite Presets/<Plugin>.txt", or "" (memory only) */
    char slot[PU_SLOTS][PU_STATE_MAX];  /* "" = empty */
    int lit;                            /* the slot loaded or saved last, -1 none */
    int armed;                          /* SAVE is lit */
    int report_save;                    /* user_save changed by the plugin: tell the host */
    long long quiet_until, tap_ms[PU_SLOTS];
} pu_user_t;

static long long pu_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* the slots as the file has them now (a missing or unreadable file changes nothing) */
static void pu_read(pu_user_t *u) {
    char line[PU_STATE_MAX + 16];
    if (!u->file[0]) return;
    FILE *f = fopen(u->file, "r");
    if (!f) return;
    for (int i = 0; i < PU_SLOTS; i++) u->slot[i][0] = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *tab = strchr(line, '\t');
        if (!tab) continue;
        int n = atoi(line) - 1;
        if (n >= 0 && n < PU_SLOTS) snprintf(u->slot[n], PU_STATE_MAX, "%s", tab + 1);
    }
    fclose(f);
}

/* write all slots through "<file>.tmp" and a rename, so a half-written file is never read. 1 = written */
static int pu_write(pu_user_t *u) {
    char tmp[820], dir[800];
    if (!u->file[0]) return 0;
    snprintf(dir, sizeof dir, "%s", u->file);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdir(dir, 0755); }
    snprintf(tmp, sizeof tmp, "%s.tmp", u->file);
    FILE *f = fopen(tmp, "w");
    if (!f) return 0;
    int ok = 1;
    for (int i = 0; i < PU_SLOTS; i++)
        if (u->slot[i][0] && fprintf(f, "%d\t%s\n", i + 1, u->slot[i]) < 0) ok = 0;
    if (fclose(f) != 0) ok = 0;
    if (ok && rename(tmp, u->file) != 0) ok = 0;
    if (!ok) remove(tmp);
    return ok;
}

static void pu_init(pu_user_t *u, const char *data_dir, const char *plugin) {
    memset(u, 0, sizeof *u);
    u->lit = -1;
    u->quiet_until = pu_now_ms() + PU_QUIET_MS;
    if (!data_dir || !data_dir[0]) return;
    /* data_dir = ".../VST2 Plugins/<Vendor> - VST - <Plugin>": the presets folder goes beside "VST2 Plugins" */
    char base[700];
    snprintf(base, sizeof base, "%s", data_dir);
    for (int up = 0; up < 2; up++) {
        char *slash = strrchr(base, '/');
        if (!slash || slash == base) return;    /* too near the top to tell: memory only */
        *slash = 0;
    }
    if (snprintf(u->file, sizeof u->file, "%s/PotentialSuite Presets/%s.txt", base, plugin) >= (int)sizeof u->file) u->file[0] = 0;
    pu_read(u);
}

/* a project's state arrived: ignore the slot taps its replay may bring */
static void pu_quiet(pu_user_t *u) { u->quiet_until = pu_now_ms() + PU_QUIET_MS; }

static int pu_slot_of(const char *key) {    /* "user_3" -> 2, else -1 */
    if (strncmp(key, "user_", 5) || key[5] < '1' || key[5] > '9' || key[6]) return -1;
    int n = key[5] - '1';
    return n < PU_SLOTS ? n : -1;
}

/* set_param(): PU_NONE (not ours), PU_DONE (handled), PU_SAVE (save the current state into slot *n with pu_store) or
 * PU_LOAD (restore pu_slot(u, *n)) */
static int pu_set(pu_user_t *u, const char *key, const char *val, int *n) {
    if (!strcmp(key, "user_save")) {
        int on = atof(val) > 0.5;
        u->armed = on;
        return PU_DONE;
    }
    int i = pu_slot_of(key);
    if (i < 0) return PU_NONE;
    long long t = pu_now_ms();
    if (t < u->quiet_until || t - u->tap_ms[i] < PU_TWICE_MS) return PU_DONE;
    u->tap_ms[i] = t;
    pu_read(u);
    *n = i;
    if (u->armed) { u->armed = 0; u->report_save = 1; return PU_SAVE; }
    return u->slot[i][0] ? PU_LOAD : PU_DONE;    /* an empty slot: nothing to load */
}

/* keep `state` in slot n, without the keys in `drop` ("dice,wild"), and write the file. 1 = written to the file */
static int pu_store(pu_user_t *u, int n, const char *state, const char *drop) {
    char tmp[PU_STATE_MAX], out[PU_STATE_MAX];
    int len = 0;
    if (n < 0 || n >= PU_SLOTS) return 0;
    snprintf(tmp, sizeof tmp, "%s", state);
    out[0] = 0;
    for (char *save = NULL, *tok = strtok_r(tmp, ";", &save); tok; tok = strtok_r(NULL, ";", &save)) {
        char *eq = strchr(tok, '=');
        if (!eq || strchr(tok, '\t')) continue;
        size_t kl = (size_t)(eq - tok);
        int skip = 0;
        for (const char *d = drop; d && *d && !skip;) {
            const char *c = strchr(d, ',');
            size_t dl = c ? (size_t)(c - d) : strlen(d);
            if (dl == kl && !strncmp(d, tok, kl)) skip = 1;
            d = c ? c + 1 : NULL;
        }
        if (skip) continue;
        int w = snprintf(out + len, sizeof out - len, "%s;", tok);
        if (w < 0 || w >= (int)sizeof out - len) break;
        len += w;
    }
    snprintf(u->slot[n], PU_STATE_MAX, "%s", out[0] ? out : ";");
    u->lit = n;
    return pu_write(u);
}

static const char *pu_slot(const pu_user_t *u, int n) { return n >= 0 && n < PU_SLOTS ? u->slot[n] : ""; }

/* after a PU_LOAD: light the slot that was loaded */
static void pu_loaded(pu_user_t *u, int n) { u->lit = n; }

/* get_param(): the characters written for one of our keys, else 0 */
static int pu_get(const pu_user_t *u, const char *key, char *buf, int buf_len) {
    if (!strcmp(key, "user_save")) return snprintf(buf, buf_len, "%d", u->armed);
    if (strncmp(key, "user_", 5)) return 0;
    char k[16];
    snprintf(k, sizeof k, "%s", key);
    int on = 0;
    size_t kl = strlen(k);
    if (kl > 3 && !strcmp(k + kl - 3, "_on")) { k[kl - 3] = 0; on = 1; }
    int i = pu_slot_of(k);
    if (i < 0) return 0;
    if (on) return snprintf(buf, buf_len, "%d", u->lit == i);
    return snprintf(buf, buf_len, u->slot[i][0] ? "USER %d" : "EMPTY %d", i + 1);
}

/* mpc_engine_changed(): 1 when SAVE went out by itself (after a save) and the host must be told */
static int pu_changed(pu_user_t *u, char *key, int len) {
    if (!u->report_save) return 0;
    u->report_save = 0;
    snprintf(key, len, "user_save");
    return 1;
}

#endif
