#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

const char *dir = NULL;

void umb_usage() {
    printf("usage: umb <command> [<args>]\n\n"
    "commands: \n"
    "\tinit [<dir>] -> Initializes <dir> directory for umb (will default to current directory)\n"
    "\n");
}

int umb_init(const char *p) {
    dir = (p == NULL) ? GetWorkingDirectory() : p;
    TraceLog(LOG_DEBUG, "[UMB] Calling 'init' command with <dir> = '%s'\n", dir);

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
