/* engine-level test of the USER slots: build with -DENGINE_C="path/to/engine.c" -I<kit>/wrapper */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include ENGINE_C
static void ms(int n) { usleep(n * 1000); }
int main(int argc, char **argv) {
    const char *root = argv[1], *k1 = argv[2], *v1 = argv[3];   /* a sound key to change and a value for it */
    char dir[600], buf[PU_STATE_MAX], key[64];
    snprintf(dir, sizeof dir, "%s/VST2 Plugins/PotentialBreaks - VST - X", root);
    char cmd[800]; snprintf(cmd, sizeof cmd, "rm -rf '%s'; mkdir -p '%s'", root, dir); assert(!system(cmd));
    const mpc_engine_t *e = mpc_engine();
    void *a = e->create(dir);
    assert(e->get_param(a, "user_3", buf, sizeof buf) > 0 && !strcmp(buf, "EMPTY 3"));
    e->set_param(a, "user_save", "1"); e->set_param(a, "user_3", "1");          /* inside the first 3 s: ignored */
    e->get_param(a, "user_3", buf, sizeof buf); assert(!strcmp(buf, "EMPTY 3"));
    ms(3100);
    e->get_param(a, k1, buf, sizeof buf); char before[64]; snprintf(before, sizeof before, "%s", buf);
    e->set_param(a, k1, v1);
    e->get_param(a, k1, buf, sizeof buf); char changed[64]; snprintf(changed, sizeof changed, "%s", buf);
    assert(strcmp(before, changed));
    e->get_param(a, "user_save", buf, sizeof buf); assert(!strcmp(buf, "1"));    /* still armed from above */
    e->set_param(a, "user_3", "0");                                              /* saves */
    e->get_param(a, "user_3", buf, sizeof buf); assert(!strcmp(buf, "USER 3"));
    e->get_param(a, "user_3_on", buf, sizeof buf); assert(!strcmp(buf, "1"));
    e->get_param(a, "user_save", buf, sizeof buf); assert(!strcmp(buf, "0"));
    int saw = 0; while (mpc_engine_changed(a, key, sizeof key)) if (!strcmp(key, "user_save")) saw = 1;
    assert(saw);
    char path[800]; snprintf(path, sizeof path, "%s/PotentialSuite Presets/", root);
    struct stat st; assert(!stat(path, &st));
    /* a second copy (another track) sees it and loads it */
    void *b = e->create(dir); ms(3100);
    e->get_param(b, k1, buf, sizeof buf); assert(!strcmp(buf, before));
    e->get_param(b, "user_3", buf, sizeof buf); assert(!strcmp(buf, "USER 3"));
    e->set_param(b, "user_3", "1");                                              /* loads */
    e->get_param(b, k1, buf, sizeof buf); assert(!strcmp(buf, changed));
    int moved = 0; while (mpc_engine_changed(b, key, sizeof key)) if (!strcmp(key, k1)) moved = 1;
    assert(moved);
    e->get_param(b, "user_3_on", buf, sizeof buf); assert(!strcmp(buf, "1"));
    e->set_param(b, "user_3", "0"); /* twice within 400 ms: ignored, fine either way */
    /* an empty slot does nothing */
    e->set_param(b, "user_5", "1"); e->get_param(b, k1, buf, sizeof buf); assert(!strcmp(buf, changed));
    /* a project's state turns taps off for 3 s */
    char stt[PU_STATE_MAX]; e->get_param(b, "state", stt, sizeof stt); assert(!strstr(stt, "user"));
    e->set_param(b, "state", stt); ms(500); e->set_param(b, "user_save", "1"); e->set_param(b, "user_6", "1");
    e->get_param(b, "user_6", buf, sizeof buf); assert(!strcmp(buf, "EMPTY 6"));
    /* the file holds no dropped keys */
    FILE *f; snprintf(cmd, sizeof cmd, "cat '%s'/PotentialSuite\\ Presets/*.txt", root); f = popen(cmd, "r");
    char line[PU_STATE_MAX + 16]; assert(fgets(line, sizeof line, f)); pclose(f);
    assert(!strncmp(line, "3\t", 2) && !strstr(line, "dice=") && !strstr(line, "theme"));
    printf("file: %s", line);
    /* no folder: memory only, still works */
    void *c = e->create(""); ms(3100);
    e->set_param(c, "user_save", "1"); e->set_param(c, "user_1", "1");
    e->get_param(c, "user_1", buf, sizeof buf); assert(!strcmp(buf, "USER 1"));
    e->destroy(a); e->destroy(b); e->destroy(c);
    puts("USER slots: OK");
    return 0;
}
