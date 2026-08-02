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

void umb_usage() {
    printf("usage: umb <command> [<args>]\n\n"
    "commands: \n"
    "\tinit <name>            -> Initializes project <name> in current directory for umb.\n"
    "\tstatus                 -> Working copy state, loudly flagging unresolved conflicts.\n"
    "\tcommit -m <msg>        -> Snapshot the working copy. Carries pending conflicts forward.\n"
    "\tlog                    -> History, flagging revisions that carry conflicts.\n"
    "\n"
    "\tbranch <name>          -> Create a branch from HEAD and switch to it.\n"
    "\tswitch <name>          -> Switch to an existing branch.\n"
    "\tmerge <branch>         -> Merge. Succeeds even when it conflicts.\n"
    "\n"
    "\tconflicts              -> List conflicts left pending.\n"
    "\tconflict show <id>     -> Every side of one conflict. --extract <side> <dest>.\n"
    "\tresolve <id> --take <author|index> | --file <path> | --edit\n"
    "\n"
    "\tvariants               -> Report variant paths: satisfied, missing or stale.\n"
    "\tvariant add <path> [--public <path>] [--keys A,B,C]\n"
    "\n"
    "Identity comes from UMB_USER, falling back to USERNAME. It decides which side of\n"
    "an unresolved conflict lands in your working copy.\n"
    "\n");
}

int umb_tag() {
    TraceLog(LOG_WARNING, "[UMB] 'tag' is not implemented yet");
    return EXIT_SUCCESS;
}

int main(int argc, const char **argv) {
    // raylib narrates every file read at LOG_INFO, which drowns out the tool's own
    // output. UMB_DEBUG turns it back on.
    char debug[8];
    SetTraceLogLevel(GetEnvironmentVariableA("UMB_DEBUG", debug, sizeof(debug)) ? LOG_DEBUG : LOG_WARNING);

    if (argc <= 1) {
        umb_usage();
        return EXIT_SUCCESS;
    }

    const char *cmd = argv[1];
    int rest_argc = argc - 2;
    const char **rest_argv = argv + 2;

    if (strcmp(cmd, "init") == 0) {
        if (rest_argc == 1 && strlen(rest_argv[0]) > 0) return umb_repo_init(rest_argv[0]);
        TraceLog(LOG_ERROR, "[UMB] Specify the new project's name");
        return EXIT_FAILURE;
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0) {
        umb_usage();
        return EXIT_SUCCESS;
    }

    // Everything below this point needs to be inside a repository.
    char root[UMB_PATH_LEN];
    if (!umb_repo_find(root)) {
        TraceLog(LOG_ERROR, "[UMB] Not inside a umb project (no .repo found)");
        return EXIT_FAILURE;
    }

    if (strcmp(cmd, "status")    == 0) return umb_cmd_status(root);
    if (strcmp(cmd, "log")       == 0) return umb_cmd_log(root);
    if (strcmp(cmd, "commit")    == 0) return umb_cmd_commit(root, rest_argc, rest_argv);
    if (strcmp(cmd, "branch")    == 0) return umb_cmd_branch(root, rest_argc, rest_argv);
    if (strcmp(cmd, "switch")    == 0) return umb_cmd_switch(root, rest_argc, rest_argv);
    if (strcmp(cmd, "merge")     == 0) return umb_cmd_merge(root, rest_argc, rest_argv);
    if (strcmp(cmd, "conflicts") == 0) return umb_cmd_conflicts(root);
    if (strcmp(cmd, "conflict")  == 0) return umb_cmd_conflict(root, rest_argc, rest_argv);
    if (strcmp(cmd, "resolve")   == 0) return umb_cmd_resolve(root, rest_argc, rest_argv);
    if (strcmp(cmd, "variants")  == 0) return umb_cmd_variants(root);
    if (strcmp(cmd, "variant")   == 0) return umb_cmd_variant(root, rest_argc, rest_argv);
    if (strcmp(cmd, "tag")       == 0) return umb_tag();

    TraceLog(LOG_ERROR, "[UMB] Could not execute requested command");
    return EXIT_FAILURE;
}
