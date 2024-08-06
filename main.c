#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#undef near
#undef far

//TODO: Look into max lenght for path
#define PATH_LEN 512

void umb_usage() {
    printf("usage: umb <command> [<args>]\n\n"
    "commands: \n"
    "\tinit <name> -> Initializes project <name> in current directory for umb.\n"
    "\n");
}

int umb_init(const char *name) {
    const char *dir = GetWorkingDirectory();
    size_t dirlen = strlen(dir);
    size_t namelen = strlen(name);
    size_t pathlen = dirlen + 1 + namelen;

    char path[PATH_LEN];
    strncpy_s(path, PATH_LEN, dir, dirlen);
    strcat_s(path, PATH_LEN, "\\");
    strcat_s(path, PATH_LEN, name);

    char metapath[PATH_LEN];
    strncpy_s(metapath, PATH_LEN, path, pathlen);
    strcat_s(metapath, PATH_LEN, "\\.repo");

    char refpath[PATH_LEN];
    strncpy_s(refpath, PATH_LEN, metapath, pathlen+6);
    strcat_s(refpath, PATH_LEN, "\\refs");
    char tagpath[PATH_LEN];
    strncpy_s(tagpath, PATH_LEN, metapath, pathlen+6);
    strcat_s(tagpath, PATH_LEN, "\\tags");
    char snappath[PATH_LEN];
    strncpy_s(snappath, PATH_LEN, metapath, pathlen+6);
    strcat_s(snappath, PATH_LEN, "\\snapshots");

    TraceLog(LOG_DEBUG, "[UMB] Calling 'init' command");
    TraceLog(LOG_DEBUG, "\t [%zu] dir = '%s'", dirlen, dir);
    TraceLog(LOG_DEBUG, "\t [%zu] path = '%s'", pathlen, path);
    TraceLog(LOG_DEBUG, "\t [%zu] meta = '%s'", pathlen+6, metapath);
    TraceLog(LOG_DEBUG, "\t [%zu] refs = '%s'", pathlen+6+5, refpath);
    TraceLog(LOG_DEBUG, "\t [%zu] tags = '%s'", pathlen+6+5, tagpath);
    TraceLog(LOG_DEBUG, "\t [%zu] snap = '%s'", pathlen+6+10, snappath);

    if (IsPathFile(path)) {
        TraceLog(LOG_ERROR, "[UMB] Project's name corresponds to an existing file");
        return EXIT_FAILURE;
    }
    if (DirectoryExists(metapath)) {
        TraceLog(LOG_ERROR, "[UMB] Projet's directory already contains a .repo directory");
        return EXIT_FAILURE;
    }
    if (!DirectoryExists(path)) {
        TraceLog(LOG_DEBUG, "[UMB] Need to create a new directory for the project");
    }

    return EXIT_SUCCESS;
}

int umb_commit() {
    return EXIT_SUCCESS;
}

int umb_tag() {
    return EXIT_SUCCESS;
}

int main(int argc, const char **argv) {
    SetTraceLogLevel(LOG_DEBUG);

    if (argc <= 1) {
        umb_usage();
        return EXIT_SUCCESS;
    }
    else {
        if (strcmp(argv[1], "init") == 0) {
            if (argc == 3 && strlen(argv[2]) > 0) {
                return umb_init(argv[2]);
            }
            else {
                TraceLog(LOG_ERROR, "[UMB] Specify the new project's name");
                return EXIT_FAILURE;
            }
        }
        else if (strcmp(argv[1], "commit") == 0) {
            return umb_commit();
        }
        else if (strcmp(argv[1], "tag") == 0) {
            return umb_tag();
        }
    }

    TraceLog(LOG_ERROR, "[UMB] Could not execute requested command");
    return EXIT_FAILURE;
}
