#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "umb.h"

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#undef near
#undef far

void umb_normalize(char *path) {
    for (char *p = path; *p; p++) {
        if (*p == '\\') *p = '/';
    }
}

void umb_join(char *out, const char *a, const char *b) {
    size_t len = strlen(a);
    if (len > 0 && (a[len-1] == '/' || a[len-1] == '\\')) {
        snprintf(out, UMB_PATH_LEN, "%s%s", a, b);
    }
    else {
        snprintf(out, UMB_PATH_LEN, "%s/%s", a, b);
    }
}

// Everything under .repo lives behind this so the layout is described in one place.
void umb_meta(char *out, const char *root, const char *leaf) {
    if (leaf == NULL || leaf[0] == '\0') snprintf(out, UMB_PATH_LEN, "%s/.repo", root);
    else snprintf(out, UMB_PATH_LEN, "%s/.repo/%s", root, leaf);
}

// raylib 5.1-dev has DirectoryExists and IsPathFile but no MakeDirectory, so this
// goes straight to Win32.
bool umb_mkdir(const char *path) {
    if (DirectoryExists(path)) return true;
    if (CreateDirectoryA(path, NULL)) return true;
    return (GetLastError() == ERROR_ALREADY_EXISTS);
}

const char *umb_user(void) {
    static char user[UMB_NAME_LEN] = {0};
    if (user[0] != '\0') return user;

    DWORD n = GetEnvironmentVariableA("UMB_USER", user, UMB_NAME_LEN);
    if (n == 0 || n >= UMB_NAME_LEN) {
        n = GetEnvironmentVariableA("USERNAME", user, UMB_NAME_LEN);
        if (n == 0 || n >= UMB_NAME_LEN) snprintf(user, UMB_NAME_LEN, "unknown");
    }
    return user;
}

bool umb_repo_find(char *root) {
    char cur[UMB_PATH_LEN];
    snprintf(cur, UMB_PATH_LEN, "%s", GetWorkingDirectory());
    umb_normalize(cur);

    while (true) {
        char meta[UMB_PATH_LEN];
        snprintf(meta, UMB_PATH_LEN, "%s/.repo", cur);
        if (DirectoryExists(meta)) {
            snprintf(root, UMB_PATH_LEN, "%s", cur);
            return true;
        }

        char *slash = strrchr(cur, '/');
        if (slash == NULL) return false;
        *slash = '\0';
        if (strchr(cur, '/') == NULL) return false;   // stop at the drive root
    }
}

int umb_repo_init(const char *name) {
    const char *dir = GetWorkingDirectory();

    char path[UMB_PATH_LEN];
    snprintf(path, UMB_PATH_LEN, "%s/%s", dir, name);
    umb_normalize(path);

    char metapath[UMB_PATH_LEN];
    snprintf(metapath, UMB_PATH_LEN, "%s/.repo", path);

    TraceLog(LOG_DEBUG, "[UMB] Calling 'init' command");
    TraceLog(LOG_DEBUG, "\t path = '%s'", path);
    TraceLog(LOG_DEBUG, "\t meta = '%s'", metapath);

    if (IsPathFile(path)) {
        TraceLog(LOG_ERROR, "[UMB] Project's name corresponds to an existing file");
        return EXIT_FAILURE;
    }
    if (DirectoryExists(metapath)) {
        TraceLog(LOG_ERROR, "[UMB] Projet's directory already contains a .repo directory");
        return EXIT_FAILURE;
    }
    if (!umb_mkdir(path)) {
        TraceLog(LOG_ERROR, "[UMB] Could not create project directory '%s'", path);
        return EXIT_FAILURE;
    }
    if (!umb_mkdir(metapath)) {
        TraceLog(LOG_ERROR, "[UMB] Could not create '%s'", metapath);
        return EXIT_FAILURE;
    }

    const char *subdirs[] = { "objects", "revs", "refs", "tags", "snapshots" };
    for (int i = 0; i < 5; i++) {
        char sub[UMB_PATH_LEN];
        snprintf(sub, UMB_PATH_LEN, "%s/%s", metapath, subdirs[i]);
        if (!umb_mkdir(sub)) {
            TraceLog(LOG_ERROR, "[UMB] Could not create '%s'", sub);
            return EXIT_FAILURE;
        }
    }

    char file[UMB_PATH_LEN];
    snprintf(file, UMB_PATH_LEN, "%s/HEAD", metapath);
    SaveFileText(file, "main\n");

    snprintf(file, UMB_PATH_LEN, "%s/config", metapath);
    SaveFileText(file,
        "# Which side of an unresolved conflict lands in the working copy.\n"
        "# mine | first-parent | newest\n"
        "prefer = mine\n");

    snprintf(file, UMB_PATH_LEN, "%s/policy", metapath);
    SaveFileText(file,
        "# Branches that refuse to carry unresolved conflicts. Comma separated, a\n"
        "# trailing * wildcard is allowed. Off by default so the umbrella is opt-in.\n"
        "# deny-pending-conflicts = main, release/*\n");

    snprintf(file, UMB_PATH_LEN, "%s/variants", metapath);
    SaveFileText(file,
        "# variant <path> public=<path> keys=A,B,C\n"
        "# The variant path is never committed. Its public counterpart is tracked\n"
        "# normally and carries the contract that private copies are checked against.\n");

    TraceLog(LOG_INFO, "[UMB] Initialized project '%s'", name);
    return EXIT_SUCCESS;
}

// Deliberately dumb "key = value" reader. Returns a pointer into a static buffer.
const char *umb_config(const char *root, const char *key, const char *fallback) {
    static char value[UMB_PATH_LEN];

    char file[UMB_PATH_LEN];
    umb_meta(file, root, "config");
    if (!FileExists(file)) return fallback;
    char *text = LoadFileText(file);
    if (text == NULL) return fallback;

    const char *result = fallback;
    char *lines = NULL;
    char *line = strtok_s(text, "\r\n", &lines);
    while (line != NULL) {
        while (*line == ' ' || *line == '\t') line++;
        if (*line != '#' && *line != '\0') {
            char *eq = strchr(line, '=');
            if (eq != NULL) {
                *eq = '\0';
                char *name = line;
                char *val = eq + 1;

                // trim both halves
                char *end = name + strlen(name);
                while (end > name && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = '\0';
                while (*val == ' ' || *val == '\t') val++;
                end = val + strlen(val);
                while (end > val && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = '\0';

                if (strcmp(name, key) == 0) {
                    snprintf(value, UMB_PATH_LEN, "%s", val);
                    result = value;
                    break;
                }
            }
        }
        line = strtok_s(NULL, "\r\n", &lines);
    }

    UnloadFileText(text);
    return result;
}

// Matches a branch against the deny-pending-conflicts list. Supports a trailing '*'
// so "release/*" behaves the way people expect.
static bool pattern_match(const char *pattern, const char *branch) {
    size_t plen = strlen(pattern);
    if (plen > 0 && pattern[plen-1] == '*') {
        return strncmp(pattern, branch, plen - 1) == 0;
    }
    return strcmp(pattern, branch) == 0;
}

bool umb_policy_denies(const char *root, const char *branch) {
    char file[UMB_PATH_LEN];
    umb_meta(file, root, "policy");
    if (!FileExists(file)) return false;
    char *text = LoadFileText(file);
    if (text == NULL) return false;

    // Two independent strtok_s contexts: a nested plain strtok() would clobber the
    // outer line iteration.
    bool denied = false;
    char *lines = NULL;
    char *line = strtok_s(text, "\r\n", &lines);
    while (line != NULL && !denied) {
        while (*line == ' ' || *line == '\t') line++;
        if (*line != '#' && *line != '\0') {
            char *eq = strchr(line, '=');
            if (eq != NULL) {
                *eq = '\0';
                char *name = line;
                char *end = name + strlen(name);
                while (end > name && (end[-1] == ' ' || end[-1] == '\t')) *(--end) = '\0';

                if (strcmp(name, "deny-pending-conflicts") == 0) {
                    char *items = NULL;
                    char *item = strtok_s(eq + 1, ",", &items);
                    while (item != NULL) {
                        while (*item == ' ' || *item == '\t') item++;
                        char *iend = item + strlen(item);
                        while (iend > item && (iend[-1] == ' ' || iend[-1] == '\t')) *(--iend) = '\0';
                        if (pattern_match(item, branch)) { denied = true; break; }
                        item = strtok_s(NULL, ",", &items);
                    }
                }
            }
        }
        line = strtok_s(NULL, "\r\n", &lines);
    }

    UnloadFileText(text);
    return denied;
}
