#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "umb.h"

//----------------------------------------------------------------------------------
// Audience scoped variants
//
// A declared variant path is never committed. Its public counterpart is tracked
// normally and carries the contract. That gives the state git cannot express: the
// path and its shape are tracked while its content is not, so the tool can tell you
// your .env is missing a key somebody added last week.
//
// Nothing private is ever stored. Secrets in history are permanent and revocation is
// not a version control feature.
//----------------------------------------------------------------------------------

int umb_variants_load(const char *root, umb_variant *out, int max) {
    char file[UMB_PATH_LEN];
    umb_meta(file, root, "variants");
    if (!FileExists(file)) return 0;
    char *text = LoadFileText(file);
    if (text == NULL) return 0;

    int count = 0;
    char *lines = NULL;
    char *line = strtok_s(text, "\r\n", &lines);
    while (line != NULL && count < max) {
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, "variant ", 8) == 0) {
            umb_variant *v = &out[count];
            memset(v, 0, sizeof(umb_variant));

            char *cursor = line + 8;
            while (*cursor == ' ') cursor++;

            char *sp = strchr(cursor, ' ');
            if (sp != NULL) *sp = '\0';
            snprintf(v->path, UMB_PATH_LEN, "%s", cursor);

            cursor = (sp != NULL) ? sp + 1 : NULL;
            while (cursor != NULL && *cursor != '\0') {
                char *next = strchr(cursor, ' ');
                if (next != NULL) *next = '\0';

                if (strncmp(cursor, "public=", 7) == 0) {
                    snprintf(v->pub, UMB_PATH_LEN, "%s", cursor + 7);
                }
                else if (strncmp(cursor, "keys=", 5) == 0) {
                    char *keys = NULL;
                    char *key = strtok_s(cursor + 5, ",", &keys);
                    while (key != NULL && v->key_count < UMB_MAX_KEYS) {
                        while (*key == ' ') key++;
                        if (*key != '\0') snprintf(v->keys[v->key_count++], UMB_KEY_LEN, "%s", key);
                        key = strtok_s(NULL, ",", &keys);
                    }
                }

                cursor = (next != NULL) ? next + 1 : NULL;
            }

            if (v->path[0] != '\0') count++;
        }
        line = strtok_s(NULL, "\r\n", &lines);
    }

    UnloadFileText(text);
    return count;
}

bool umb_variant_is_private(const char *root, const char *relpath) {
    umb_variant vars[UMB_MAX_VARIANTS];
    int count = umb_variants_load(root, vars, UMB_MAX_VARIANTS);
    for (int i = 0; i < count; i++) {
        if (strcmp(vars[i].path, relpath) == 0) return true;
    }
    return false;
}

// Reads the KEY= names present in a local variant file. Deliberately naive: enough
// for .env shaped content, which is the case that motivated the feature.
static int read_keys(const char *path, char keys[][UMB_KEY_LEN], int max) {
    if (!FileExists(path)) return -1;           // no private variant supplied yet
    char *text = LoadFileText(path);
    if (text == NULL) return -1;

    int count = 0;
    char *lines = NULL;
    char *line = strtok_s(text, "\r\n", &lines);
    while (line != NULL && count < max) {
        while (*line == ' ' || *line == '\t') line++;
        if (*line != '#' && *line != '\0') {
            char *eq = strchr(line, '=');
            if (eq != NULL) {
                *eq = '\0';
                char *end = line + strlen(line);
                while (end > line && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = '\0';
                if (*line != '\0') snprintf(keys[count++], UMB_KEY_LEN, "%s", line);
            }
        }
        line = strtok_s(NULL, "\r\n", &lines);
    }

    UnloadFileText(text);
    return count;
}

int umb_cmd_variants(const char *root) {
    umb_variant vars[UMB_MAX_VARIANTS];
    int count = umb_variants_load(root, vars, UMB_MAX_VARIANTS);

    if (count == 0) {
        printf("No variant paths declared.\n");
        printf("  umb variant add <path> --public <path> --keys A,B,C\n");
        return EXIT_SUCCESS;
    }

    int unsatisfied = 0;
    for (int i = 0; i < count; i++) {
        umb_variant *v = &vars[i];

        char full[UMB_PATH_LEN];
        umb_join(full, root, v->path);

        char present[UMB_MAX_KEYS][UMB_KEY_LEN];
        int have = read_keys(full, present, UMB_MAX_KEYS);

        if (have < 0) {
            printf("  missing    %s\n", v->path);
            if (v->pub[0] != '\0') printf("             provide your own, public template is %s\n", v->pub);
            unsatisfied++;
            continue;
        }

        // Stale means the tracked contract grew a key the local copy has not got.
        char missing[UMB_MAX_KEYS][UMB_KEY_LEN];
        int missing_count = 0;
        for (int k = 0; k < v->key_count; k++) {
            bool found = false;
            for (int p = 0; p < have; p++) {
                if (strcmp(v->keys[k], present[p]) == 0) { found = true; break; }
            }
            if (!found && missing_count < UMB_MAX_KEYS) {
                snprintf(missing[missing_count++], UMB_KEY_LEN, "%s", v->keys[k]);
            }
        }

        if (missing_count > 0) {
            printf("  stale      %s\n", v->path);
            printf("             missing key%s:", (missing_count == 1) ? "" : "s");
            for (int m = 0; m < missing_count; m++) printf(" %s", missing[m]);
            printf("\n");
            unsatisfied++;
        }
        else {
            printf("  satisfied  %s  (%d key%s)\n", v->path, v->key_count, (v->key_count == 1) ? "" : "s");
        }
    }

    if (unsatisfied == 1)     printf("\n1 variant path needs attention.\n");
    else if (unsatisfied > 1) printf("\n%d variant paths need attention.\n", unsatisfied);
    return EXIT_SUCCESS;
}

static int variant_add(const char *root, int argc, const char **argv) {
    if (argc < 1) {
        TraceLog(LOG_ERROR, "[UMB] usage: umb variant add <path> [--public <path>] [--keys A,B,C]");
        return EXIT_FAILURE;
    }

    char path[UMB_PATH_LEN];
    snprintf(path, UMB_PATH_LEN, "%s", argv[0]);
    umb_normalize(path);

    const char *pub = "";
    const char *keys = "";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--public") == 0 && i + 1 < argc) pub = argv[++i];
        else if (strcmp(argv[i], "--keys") == 0 && i + 1 < argc) keys = argv[++i];
    }

    char line[UMB_PATH_LEN * 2];
    int ln = snprintf(line, sizeof(line), "variant %s", path);
    if (pub[0] != '\0')  ln += snprintf(line + ln, sizeof(line) - ln, " public=%s", pub);
    if (keys[0] != '\0') ln += snprintf(line + ln, sizeof(line) - ln, " keys=%s", keys);
    snprintf(line + ln, sizeof(line) - ln, "\n");

    char file[UMB_PATH_LEN];
    umb_meta(file, root, "variants");

    char *existing = LoadFileText(file);
    size_t len = (existing != NULL) ? strlen(existing) : 0;
    size_t total = len + strlen(line) + 2;

    char *buf = (char *)malloc(total);
    if (buf == NULL) {
        if (existing != NULL) UnloadFileText(existing);
        return EXIT_FAILURE;
    }

    int n = 0;
    if (existing != NULL) {
        n += snprintf(buf + n, total - n, "%s", existing);
        UnloadFileText(existing);
        if (n > 0 && buf[n-1] != '\n') n += snprintf(buf + n, total - n, "\n");
    }
    snprintf(buf + n, total - n, "%s", line);

    bool ok = SaveFileText(file, buf);
    free(buf);

    if (!ok) {
        TraceLog(LOG_ERROR, "[UMB] Could not update .repo/variants");
        return EXIT_FAILURE;
    }

    printf("Declared '%s' as a variant path. It will never be committed.\n", path);
    return EXIT_SUCCESS;
}

int umb_cmd_variant(const char *root, int argc, const char **argv) {
    if (argc >= 1 && strcmp(argv[0], "add") == 0) {
        return variant_add(root, argc - 1, argv + 1);
    }
    TraceLog(LOG_ERROR, "[UMB] usage: umb variant add <path> [--public <path>] [--keys A,B,C]");
    return EXIT_FAILURE;
}
