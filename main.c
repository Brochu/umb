#include <stdio.h>
#include <windows.h>

char *dir = NULL;

void umb_usage() {
    printf("usage: umb <command> [<args>]\n\n"
    "commands: \n"
    "\tinit [<dir>] -> Initializes <dir> directory for umb (will default to current directory)\n"
    "\n");
}

int umb_init(char *p) {
    if (p == NULL) {
        DWORD len = 0;
        len = GetCurrentDirectory(0, NULL);

        dir = malloc(len); //TODO: Look into combining allocs in one arena?
        GetCurrentDirectory(len, dir);
    } else {
        dir = p;
    }

    printf("[UMB] Calling 'init' command with <dir> = '%s'\n", dir);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        umb_usage();
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
    }

    return EXIT_FAILURE;
}
