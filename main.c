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

void umb_usage() {
    printf("usage: umb <command> [<args>]\n\n"
    "commands: \n"
    "\tinit [<name>] -> Initializes project <name> in current directory for umb.\n"
    "\n");
}

int umb_init(const char *name) {
    const char *dir = GetWorkingDirectory();
    size_t dirlen = strlen(dir);

    char path[512];
    strncpy_s(path, 512, dir, strlen(dir));
    path[dirlen] = '\\';
    path[dirlen+1] = '\0';

    strcat_s(path, 512, name);
    TraceLog(LOG_DEBUG, "[UMB] Calling 'init' command", dir);
    TraceLog(LOG_DEBUG, "\t dir = '%s'", dir);
    TraceLog(LOG_DEBUG, "\t path = '%s'", path);

    if (IsPathFile(path)) {
        TraceLog(LOG_ERROR, "[UMB] Path specified '%s' is already a file", path);
        return EXIT_FAILURE;
    }

    if (!DirectoryExists(path)) {
        TraceLog(LOG_DEBUG, "[UMB] Could not find path '%s', Create it", path);
        //TODO: Look at a way to make this OS agnostic?
        CreateDirectory(path, NULL);
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
            if (argc == 2) {
                return umb_init(NULL);
            }
            else {
                return umb_init(argv[2]);
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
