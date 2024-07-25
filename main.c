#include <stdio.h>
#include <windows.h>

const char *path = NULL;

void usage() {
    printf("usage: umb <command> [<args>]\n\n"
    "commands: \n"
    "\tinit -> Initializes directory for umb\n"
    "\n");
}

int main(int argc, char **argv) {
    if (argc <= 1) {
        usage();
    }
    else {
        printf("Running exe: '%s'\n", argv[0]);

        DWORD len = 0;
        char dir[128];
        len = GetCurrentDirectory(0, NULL);
        GetCurrentDirectory(len, dir);
        printf("PWD: '%s'\n", dir);
    }
}
