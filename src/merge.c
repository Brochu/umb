#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "raylib.h"
#include "umb.h"

#define MAX_ANCESTORS 512

//----------------------------------------------------------------------------------
// Working tree
//----------------------------------------------------------------------------------

static bool relative_to(const char *root, const char *full, char *out) {
    size_t rlen = strlen(root);
    if (_strnicmp(root, full, rlen) != 0) return false;

    const char *rel = full + rlen;
    while (*rel == '/' || *rel == '\\') rel++;
    if (*rel == '\0') return false;

    snprintf(out, UMB_PATH_LEN, "%s", rel);
    umb_normalize(out);
    return true;
}

// Everything in the tree is tracked except repository metadata and declared variant
// paths. No ignore list yet - that is a separate axis and not what this prototype is
// testing.
static void scan_worktree(const char *root, umb_rev *rev) {
    umb_variant vars[UMB_MAX_VARIANTS];
    int var_count = umb_variants_load(root, vars, UMB_MAX_VARIANTS);

    FilePathList files = LoadDirectoryFilesEx(root, NULL, true);
    for (unsigned int i = 0; i < files.count; i++) {
        if (!IsPathFile(files.paths[i])) continue;

        char rel[UMB_PATH_LEN];
        if (!relative_to(root, files.paths[i], rel)) continue;
        if (strncmp(rel, ".repo/", 6) == 0) continue;

        bool private_variant = false;
        for (int v = 0; v < var_count; v++) {
            if (strcmp(vars[v].path, rel) == 0) { private_variant = true; break; }
        }
        if (private_variant) continue;

        umb_hash h;
        if (umb_store_file(root, files.paths[i], h)) umb_rev_set(rev, rel, h);
    }
    UnloadDirectoryFiles(files);
}

//----------------------------------------------------------------------------------
// History walking
//----------------------------------------------------------------------------------

static int collect_ancestors(const char *root, const char *start, umb_hash *out, int max) {
    int count = 0;
    if (start == NULL || start[0] == '\0') return 0;

    snprintf(out[count++], UMB_HASH_LEN, "%s", start);
    for (int i = 0; i < count && count < max; i++) {
        umb_rev r;
        if (!umb_rev_load(root, out[i], &r)) continue;
        for (int p = 0; p < r.parent_count && count < max; p++) {
            bool seen = false;
            for (int j = 0; j < count; j++) {
                if (strcmp(out[j], r.parent[p]) == 0) { seen = true; break; }
            }
            if (!seen) snprintf(out[count++], UMB_HASH_LEN, "%s", r.parent[p]);
        }
    }
    return count;
}

static bool merge_base(const char *root, const char *a, const char *b, umb_hash out) {
    umb_hash mine[MAX_ANCESTORS];
    int mine_count = collect_ancestors(root, a, mine, MAX_ANCESTORS);

    umb_hash theirs[MAX_ANCESTORS];
    int theirs_count = collect_ancestors(root, b, theirs, MAX_ANCESTORS);

    for (int i = 0; i < theirs_count; i++) {
        for (int j = 0; j < mine_count; j++) {
            if (strcmp(theirs[i], mine[j]) == 0) {
                snprintf(out, UMB_HASH_LEN, "%s", theirs[i]);
                return true;
            }
        }
    }
    snprintf(out, UMB_HASH_LEN, "%s", UMB_NO_HASH);
    return false;
}

// Walks back while the path still carries this blob, so the author returned is the one
// who introduced the content rather than whoever happened to commit last.
static void find_side_author(const char *root, const char *tip, const char *path,
                             const char *hash, char *author, umb_hash rev_out) {
    snprintf(author, UMB_NAME_LEN, "%s", "unknown");
    snprintf(rev_out, UMB_HASH_LEN, "%s", tip);

    umb_hash cursor;
    snprintf(cursor, UMB_HASH_LEN, "%s", tip);

    for (int guard = 0; guard < MAX_ANCESTORS; guard++) {
        umb_rev r;
        if (!umb_rev_load(root, cursor, &r)) break;

        umb_entry *e = umb_rev_entry(&r, path);
        if (e == NULL || strcmp(e->hash, hash) != 0) break;

        snprintf(author, UMB_NAME_LEN, "%s", r.author);
        snprintf(rev_out, UMB_HASH_LEN, "%s", cursor);

        if (r.parent_count == 0) break;
        snprintf(cursor, UMB_HASH_LEN, "%s", r.parent[0]);
    }
}

//----------------------------------------------------------------------------------
// Mine-first materialization
//
// Which side lands on disk is a local rendering decision. History keeps every side
// symmetrically; only the working copy takes a position.
//----------------------------------------------------------------------------------

static const umb_side *pick_side(const char *root, const umb_conflict *c, const char *prefer) {
    if (c->side_count == 0) return NULL;

    if (strcmp(prefer, "mine") == 0) {
        const char *me = umb_user();
        for (int i = 0; i < c->side_count; i++) {
            if (strcmp(c->sides[i].author, me) == 0) return &c->sides[i];
        }
        // fall through to first-parent when you authored neither side
    }

    if (strcmp(prefer, "newest") == 0) {
        const umb_side *best = &c->sides[0];
        long long best_time = -1;
        for (int i = 0; i < c->side_count; i++) {
            umb_rev r;
            if (umb_rev_load(root, c->sides[i].rev, &r) && r.time > best_time) {
                best_time = r.time;
                best = &c->sides[i];
            }
        }
        return best;
    }

    return &c->sides[0];        // sides[0] is always ours, i.e. the first parent
}

static void materialize(const char *root, umb_rev *rev) {
    const char *prefer = umb_config(root, "prefer", "mine");

    for (int i = 0; i < rev->entry_count; i++) {
        char full[UMB_PATH_LEN];
        umb_join(full, root, rev->entries[i].path);

        umb_hash disk;
        if (umb_hash_file(full, disk) && strcmp(disk, rev->entries[i].hash) == 0) continue;
        umb_checkout_blob(root, rev->entries[i].hash, full);
    }

    for (int i = 0; i < rev->conflict_count; i++) {
        const umb_side *side = pick_side(root, &rev->conflicts[i], prefer);
        if (side == NULL) continue;

        char full[UMB_PATH_LEN];
        umb_join(full, root, rev->conflicts[i].path);

        umb_hash disk;
        if (umb_hash_file(full, disk) && strcmp(disk, side->hash) == 0) continue;
        umb_checkout_blob(root, side->hash, full);
    }
}

static void format_age(long long opened, char *out, int len) {
    if (opened <= 0) { snprintf(out, len, "unknown"); return; }

    long long secs = (long long)time(NULL) - opened;
    if (secs < 0) secs = 0;

    if (secs < 60)      snprintf(out, len, "%llds", secs);
    else if (secs < 3600)  snprintf(out, len, "%lldm", secs / 60);
    else if (secs < 86400) snprintf(out, len, "%lldh", secs / 3600);
    else                   snprintf(out, len, "%lldd", secs / 86400);
}

//----------------------------------------------------------------------------------
// commit
//----------------------------------------------------------------------------------

int umb_cmd_commit(const char *root, int argc, const char **argv) {
    const char *message = "";
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) message = argv[++i];
    }
    if (message[0] == '\0') {
        TraceLog(LOG_ERROR, "[UMB] usage: umb commit -m <message>");
        return EXIT_FAILURE;
    }

    char branch[UMB_NAME_LEN];
    if (!umb_head_read(root, branch)) {
        TraceLog(LOG_ERROR, "[UMB] Could not read HEAD");
        return EXIT_FAILURE;
    }

    umb_rev rev;
    umb_rev_clear(&rev);

    umb_hash tip;
    bool has_tip = umb_ref_read(root, branch, tip);
    if (has_tip) {
        umb_rev parent;
        if (umb_rev_load(root, tip, &parent)) {
            snprintf(rev.parent[0], UMB_HASH_LEN, "%s", tip);
            rev.parent_count = 1;

            // Pending conflicts travel forward untouched. Editing a conflicted file
            // does not quietly resolve it - that needs 'umb resolve'.
            for (int i = 0; i < parent.conflict_count; i++) {
                rev.conflicts[rev.conflict_count++] = parent.conflicts[i];
            }
        }
    }

    scan_worktree(root, &rev);

    // Drop tree entries for paths that are still conflicted, so a revision either has
    // defined content for a path or an open conflict, never both.
    for (int c = 0; c < rev.conflict_count; c++) {
        for (int i = 0; i < rev.entry_count; i++) {
            if (strcmp(rev.entries[i].path, rev.conflicts[c].path) == 0) {
                for (int j = i; j < rev.entry_count - 1; j++) rev.entries[j] = rev.entries[j+1];
                rev.entry_count--;
                break;
            }
        }
    }

    snprintf(rev.author, UMB_NAME_LEN, "%s", umb_user());
    snprintf(rev.message, UMB_MSG_LEN, "%s", message);
    rev.time = (long long)time(NULL);

    umb_hash out;
    if (!umb_rev_save(root, &rev, out)) {
        TraceLog(LOG_ERROR, "[UMB] Could not write revision");
        return EXIT_FAILURE;
    }
    if (!umb_ref_write(root, branch, out)) {
        TraceLog(LOG_ERROR, "[UMB] Could not update branch '%s'", branch);
        return EXIT_FAILURE;
    }

    printf("[%s %.8s] %s\n", branch, out, message);
    printf("  %d file%s tracked", rev.entry_count, (rev.entry_count == 1) ? "" : "s");
    if (rev.conflict_count > 0) printf(", %d conflict%s still pending",
                                       rev.conflict_count, (rev.conflict_count == 1) ? "" : "s");
    printf("\n");
    return EXIT_SUCCESS;
}

//----------------------------------------------------------------------------------
// branch / switch
//----------------------------------------------------------------------------------

int umb_cmd_branch(const char *root, int argc, const char **argv) {
    if (argc < 1) {
        TraceLog(LOG_ERROR, "[UMB] usage: umb branch <name>");
        return EXIT_FAILURE;
    }

    char current[UMB_NAME_LEN];
    if (!umb_head_read(root, current)) return EXIT_FAILURE;

    umb_hash tip;
    if (umb_ref_read(root, current, tip)) {
        if (!umb_ref_write(root, argv[0], tip)) return EXIT_FAILURE;
    }
    if (!umb_head_write(root, argv[0])) return EXIT_FAILURE;

    printf("Switched to a new branch '%s'\n", argv[0]);
    return EXIT_SUCCESS;
}

int umb_cmd_switch(const char *root, int argc, const char **argv) {
    if (argc < 1) {
        TraceLog(LOG_ERROR, "[UMB] usage: umb switch <name>");
        return EXIT_FAILURE;
    }

    umb_hash tip;
    if (!umb_ref_read(root, argv[0], tip)) {
        TraceLog(LOG_ERROR, "[UMB] No such branch '%s'", argv[0]);
        return EXIT_FAILURE;
    }
    if (!umb_head_write(root, argv[0])) return EXIT_FAILURE;

    umb_rev rev;
    if (umb_rev_load(root, tip, &rev)) materialize(root, &rev);

    printf("Switched to branch '%s'\n", argv[0]);
    return EXIT_SUCCESS;
}

//----------------------------------------------------------------------------------
// merge
//----------------------------------------------------------------------------------

// Derived from the path plus every side hash in sorted order, so the same divergence
// always yields the same id whichever direction the merge ran. That stability is what
// makes resolution reuse possible later, and it is why re-merging an already merged
// branch does not duplicate the conflict.
static void refresh_conflict_id(umb_conflict *c) {
    char sorted[UMB_MAX_SIDES][UMB_HASH_LEN];
    for (int i = 0; i < c->side_count; i++) {
        snprintf(sorted[i], UMB_HASH_LEN, "%s", c->sides[i].hash);
    }
    for (int i = 1; i < c->side_count; i++) {
        char tmp[UMB_HASH_LEN];
        snprintf(tmp, UMB_HASH_LEN, "%s", sorted[i]);
        int j = i - 1;
        while (j >= 0 && strcmp(sorted[j], tmp) > 0) {
            snprintf(sorted[j+1], UMB_HASH_LEN, "%s", sorted[j]);
            j--;
        }
        snprintf(sorted[j+1], UMB_HASH_LEN, "%s", tmp);
    }

    char buf[UMB_PATH_LEN + UMB_MAX_SIDES*(UMB_HASH_LEN+1) + 4];
    int n = snprintf(buf, sizeof(buf), "%s", c->path);
    for (int i = 0; i < c->side_count; i++) {
        n += snprintf(buf + n, sizeof(buf) - n, "|%s", sorted[i]);
    }
    umb_hash_bytes(buf, n, c->id);
}

// A path that is already conflicted can diverge again. Dropping that second divergence
// on the floor would be exactly the silent loss this whole design claims to prevent,
// so it becomes an additional side instead.
static bool absorb_side(const char *root, umb_conflict *c, const char *tip,
                        const char *path, const char *hash) {
    for (int i = 0; i < c->side_count; i++) {
        if (strcmp(c->sides[i].hash, hash) == 0) return false;
    }
    if (c->side_count >= UMB_MAX_SIDES) {
        TraceLog(LOG_WARNING, "[UMB] '%s' already has %d sides, cannot record another",
                 path, UMB_MAX_SIDES);
        return false;
    }

    umb_side *s = &c->sides[c->side_count++];
    snprintf(s->hash, UMB_HASH_LEN, "%s", hash);
    find_side_author(root, tip, path, hash, s->author, s->rev);
    refresh_conflict_id(c);
    return true;
}

int umb_cmd_merge(const char *root, int argc, const char **argv) {
    if (argc < 1) {
        TraceLog(LOG_ERROR, "[UMB] usage: umb merge <branch>");
        return EXIT_FAILURE;
    }

    char branch[UMB_NAME_LEN];
    if (!umb_head_read(root, branch)) return EXIT_FAILURE;
    if (strcmp(branch, argv[0]) == 0) {
        TraceLog(LOG_ERROR, "[UMB] Cannot merge a branch into itself");
        return EXIT_FAILURE;
    }

    umb_hash ours, theirs;
    if (!umb_ref_read(root, branch, ours)) {
        TraceLog(LOG_ERROR, "[UMB] Branch '%s' has no commits", branch);
        return EXIT_FAILURE;
    }
    if (!umb_ref_read(root, argv[0], theirs)) {
        TraceLog(LOG_ERROR, "[UMB] No such branch '%s'", argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(ours, theirs) == 0) {
        printf("Already up to date.\n");
        return EXIT_SUCCESS;
    }

    umb_hash base;
    bool has_base = merge_base(root, ours, theirs, base);

    umb_rev rev_ours, rev_theirs, rev_base;
    umb_rev_load(root, ours, &rev_ours);
    umb_rev_load(root, theirs, &rev_theirs);
    umb_rev_clear(&rev_base);
    if (has_base) umb_rev_load(root, base, &rev_base);

    umb_rev merged;
    umb_rev_clear(&merged);
    snprintf(merged.parent[0], UMB_HASH_LEN, "%s", ours);
    snprintf(merged.parent[1], UMB_HASH_LEN, "%s", theirs);
    merged.parent_count = 2;

    // Conflicts already pending on our side stay pending.
    for (int i = 0; i < rev_ours.conflict_count; i++) {
        merged.conflicts[merged.conflict_count++] = rev_ours.conflicts[i];
    }

    int opened = 0;
    int auto_merged = 0;
    int absorbed = 0;

    // Three-way over the union of paths.
    for (int pass = 0; pass < 2; pass++) {
        umb_rev *src = (pass == 0) ? &rev_ours : &rev_theirs;
        for (int i = 0; i < src->entry_count; i++) {
            const char *path = src->entries[i].path;
            if (umb_rev_entry(&merged, path) != NULL) continue;

            umb_conflict *existing = NULL;
            for (int c = 0; c < merged.conflict_count; c++) {
                if (strcmp(merged.conflicts[c].path, path) == 0) { existing = &merged.conflicts[c]; break; }
            }
            if (existing != NULL) {
                umb_entry *incoming = umb_rev_entry(&rev_theirs, path);
                if (incoming != NULL && absorb_side(root, existing, theirs, path, incoming->hash)) {
                    absorbed++;
                }
                continue;
            }

            umb_entry *o = umb_rev_entry(&rev_ours, path);
            umb_entry *t = umb_rev_entry(&rev_theirs, path);
            umb_entry *b = umb_rev_entry(&rev_base, path);

            const char *oh = (o != NULL) ? o->hash : NULL;
            const char *th = (t != NULL) ? t->hash : NULL;
            const char *bh = (b != NULL) ? b->hash : NULL;

            if (oh != NULL && th != NULL && strcmp(oh, th) == 0) {
                umb_rev_set(&merged, path, oh);                 // same on both sides
            }
            else if (oh == NULL && th != NULL && bh == NULL) {
                umb_rev_set(&merged, path, th);                 // added by them
                auto_merged++;
            }
            else if (th == NULL && oh != NULL && bh == NULL) {
                umb_rev_set(&merged, path, oh);                 // added by us
            }
            else if (bh != NULL && oh != NULL && strcmp(oh, bh) == 0) {
                if (th != NULL) umb_rev_set(&merged, path, th); // only they changed it
                auto_merged++;                                  // (or they deleted it)
            }
            else if (bh != NULL && th != NULL && strcmp(th, bh) == 0) {
                if (oh != NULL) umb_rev_set(&merged, path, oh); // only we changed it
            }
            else if (oh == NULL || th == NULL) {
                // edit/delete. Keep the surviving content and record nothing: a delete
                // is cheap to redo, losing an edit is not.
                umb_rev_set(&merged, path, (oh != NULL) ? oh : th);
            }
            else {
                if (merged.conflict_count >= UMB_MAX_CONFLICTS) {
                    TraceLog(LOG_WARNING, "[UMB] Conflict limit reached, skipping '%s'", path);
                    continue;
                }

                umb_conflict *c = &merged.conflicts[merged.conflict_count++];
                memset(c, 0, sizeof(umb_conflict));
                snprintf(c->path, UMB_PATH_LEN, "%s", path);
                snprintf(c->base, UMB_HASH_LEN, "%s", (bh != NULL) ? bh : UMB_NO_HASH);
                c->opened = (long long)time(NULL);

                c->side_count = 2;
                snprintf(c->sides[0].hash, UMB_HASH_LEN, "%s", oh);
                find_side_author(root, ours, path, oh, c->sides[0].author, c->sides[0].rev);
                snprintf(c->sides[1].hash, UMB_HASH_LEN, "%s", th);
                find_side_author(root, theirs, path, th, c->sides[1].author, c->sides[1].rev);
                refresh_conflict_id(c);

                // The person whose work is about to become invisible has the strongest
                // interest in it not being lost, so they own it by default.
                const char *me = umb_user();
                snprintf(c->owner, UMB_NAME_LEN, "%s",
                         (strcmp(c->sides[0].author, me) == 0) ? c->sides[1].author : c->sides[0].author);

                opened++;
            }
        }
    }

    if (merged.conflict_count > 0 && umb_policy_denies(root, branch)) {
        TraceLog(LOG_ERROR, "[UMB] Branch '%s' refuses to carry unresolved conflicts", branch);
        printf("\nPolicy 'deny-pending-conflicts' covers '%s'.\n", branch);
        printf("%d conflict%s would be pending after this merge.\n",
               merged.conflict_count, (merged.conflict_count == 1) ? "" : "s");
        printf("Merge into a feature branch, resolve there, then bring it across.\n");
        return EXIT_FAILURE;
    }

    snprintf(merged.author, UMB_NAME_LEN, "%s", umb_user());
    snprintf(merged.message, UMB_MSG_LEN, "merge '%s' into '%s'", argv[0], branch);
    merged.time = (long long)time(NULL);

    umb_hash out;
    if (!umb_rev_save(root, &merged, out)) return EXIT_FAILURE;
    if (!umb_ref_write(root, branch, out)) return EXIT_FAILURE;

    materialize(root, &merged);

    printf("[%s %.8s] merge '%s'\n", branch, out, argv[0]);
    printf("  %d path%s in tree, %d adopted from '%s'\n",
           merged.entry_count, (merged.entry_count == 1) ? "" : "s", auto_merged, argv[0]);

    if (absorbed > 0) {
        printf("  %d already conflicted path%s gained another side\n",
               absorbed, (absorbed == 1) ? "" : "s");
    }
    if (opened > 0 || absorbed > 0) {
        const char *prefer = umb_config(root, "prefer", "mine");
        if (opened > 0) printf("  %d conflict%s recorded and left pending\n", opened, (opened == 1) ? "" : "s");
        printf("\nThe merge succeeded. Your working copy holds your own side (prefer = %s).\n", prefer);
        printf("Run 'umb conflicts' to see what is outstanding.\n");
    }
    return EXIT_SUCCESS;
}

//----------------------------------------------------------------------------------
// conflicts / conflict show / resolve
//----------------------------------------------------------------------------------

int umb_cmd_conflicts(const char *root) {
    umb_hash tip;
    if (!umb_tip(root, tip)) {
        printf("No commits yet.\n");
        return EXIT_SUCCESS;
    }

    umb_rev rev;
    if (!umb_rev_load(root, tip, &rev)) return EXIT_FAILURE;

    if (rev.conflict_count == 0) {
        printf("No pending conflicts.\n");
        return EXIT_SUCCESS;
    }

    printf("%d pending conflict%s:\n\n", rev.conflict_count, (rev.conflict_count == 1) ? "" : "s");
    for (int i = 0; i < rev.conflict_count; i++) {
        umb_conflict *c = &rev.conflicts[i];
        char age[32];
        format_age(c->opened, age, sizeof(age));

        printf("  %.8s  %s\n", c->id, c->path);
        printf("            owner %s, open %s\n", c->owner, age);
        for (int s = 0; s < c->side_count; s++) {
            printf("            side %d: %s (%.8s)\n", s + 1, c->sides[s].author, c->sides[s].rev);
        }
        printf("\n");
    }
    printf("Resolve with: umb resolve <id> --take <author|index>\n");
    return EXIT_SUCCESS;
}

static const umb_side *side_by_selector(const umb_conflict *c, const char *sel) {
    // One author can hold several sides once a conflicted path diverges again, so an
    // ambiguous name must not quietly pick the first one.
    const umb_side *by_author = NULL;
    int matches = 0;
    for (int i = 0; i < c->side_count; i++) {
        if (strcmp(c->sides[i].author, sel) == 0) { by_author = &c->sides[i]; matches++; }
    }
    if (matches == 1) return by_author;
    if (matches > 1) {
        TraceLog(LOG_ERROR, "[UMB] '%s' authored %d of these sides - select by index instead", sel, matches);
        return NULL;
    }

    int index = atoi(sel);
    if (index >= 1 && index <= c->side_count) return &c->sides[index - 1];
    for (int i = 0; i < c->side_count; i++) {
        if (strncmp(c->sides[i].rev, sel, strlen(sel)) == 0) return &c->sides[i];
    }
    return NULL;
}

int umb_cmd_conflict(const char *root, int argc, const char **argv) {
    if (argc < 2 || strcmp(argv[0], "show") != 0) {
        TraceLog(LOG_ERROR, "[UMB] usage: umb conflict show <id> [--extract <side> <dest>]");
        return EXIT_FAILURE;
    }

    umb_hash tip;
    if (!umb_tip(root, tip)) return EXIT_FAILURE;

    umb_rev rev;
    if (!umb_rev_load(root, tip, &rev)) return EXIT_FAILURE;

    umb_conflict *c = umb_rev_conflict(&rev, argv[1]);
    if (c == NULL) {
        TraceLog(LOG_ERROR, "[UMB] No such conflict '%s' (or ambiguous prefix)", argv[1]);
        return EXIT_FAILURE;
    }

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--extract") == 0 && i + 2 < argc) {
            const umb_side *side = side_by_selector(c, argv[i+1]);
            if (side == NULL) {
                TraceLog(LOG_ERROR, "[UMB] No such side '%s'", argv[i+1]);
                return EXIT_FAILURE;
            }
            if (!umb_checkout_blob(root, side->hash, argv[i+2])) return EXIT_FAILURE;
            printf("Extracted %s's side to '%s'\n", side->author, argv[i+2]);
            return EXIT_SUCCESS;
        }
    }

    char age[32];
    format_age(c->opened, age, sizeof(age));

    printf("conflict %s\n", c->id);
    printf("  path   %s\n", c->path);
    printf("  owner  %s\n", c->owner);
    printf("  open   %s\n", age);
    printf("  base   %s\n\n", c->base);

    for (int s = 0; s < c->side_count; s++) {
        int len = 0;
        unsigned char *data = umb_load_blob(root, c->sides[s].hash, &len);
        printf("  side %d  %s  rev %.8s  blob %s  %d bytes\n",
               s + 1, c->sides[s].author, c->sides[s].rev, c->sides[s].hash, len);

        // Only preview things that look like text; a binary preview is noise.
        if (data != NULL) {
            bool text = true;
            int probe = (len < 512) ? len : 512;
            for (int i = 0; i < probe; i++) {
                if (data[i] == 0) { text = false; break; }
            }
            if (text && len > 0) {
                int show = (len < 200) ? len : 200;
                printf("          | ");
                for (int i = 0; i < show; i++) {
                    putchar(data[i]);
                    if (data[i] == '\n' && i + 1 < show) printf("          | ");
                }
                if (show < len) printf(" ...");
                printf("\n");
            }
            umb_free_blob(data);
        }
        printf("\n");
    }

    printf("Extract with: umb conflict show %.8s --extract <author|index> <dest>\n", c->id);
    return EXIT_SUCCESS;
}

// Writes a marked-up scratch file so text conflicts can still be edited by hand. The
// working copy stays clean; this is opt-in.
static int resolve_edit(const char *root, umb_conflict *c) {
    char dest[UMB_PATH_LEN];
    snprintf(dest, UMB_PATH_LEN, "%s/%s.umbedit", root, c->path);

    char *buf = (char *)malloc(1024*1024);
    if (buf == NULL) return EXIT_FAILURE;
    buf[0] = '\0';

    int n = 0;
    for (int s = 0; s < c->side_count; s++) {
        int len = 0;
        unsigned char *data = umb_load_blob(root, c->sides[s].hash, &len);
        n += snprintf(buf + n, 1024*1024 - n, "<<<<<<< side %d: %s\n", s + 1, c->sides[s].author);
        if (data != NULL) {
            int copy = (len < 1024*512) ? len : 1024*512;
            memcpy(buf + n, data, copy);
            n += copy;
            if (copy > 0 && buf[n-1] != '\n') buf[n++] = '\n';
            umb_free_blob(data);
        }
        n += snprintf(buf + n, 1024*1024 - n, ">>>>>>> end side %d\n", s + 1);
    }
    buf[n] = '\0';

    bool ok = SaveFileText(dest, buf);
    free(buf);
    if (!ok) return EXIT_FAILURE;

    printf("Wrote '%s.umbedit' with every side marked up.\n", c->path);
    printf("Edit it, then: umb resolve %.8s --file %s.umbedit\n", c->id, c->path);
    return EXIT_SUCCESS;
}

int umb_cmd_resolve(const char *root, int argc, const char **argv) {
    if (argc < 1) {
        TraceLog(LOG_ERROR, "[UMB] usage: umb resolve <id> [--take <author|index> | --file <path> | --edit]");
        return EXIT_FAILURE;
    }

    char branch[UMB_NAME_LEN];
    if (!umb_head_read(root, branch)) return EXIT_FAILURE;

    umb_hash tip;
    if (!umb_ref_read(root, branch, tip)) return EXIT_FAILURE;

    umb_rev rev;
    if (!umb_rev_load(root, tip, &rev)) return EXIT_FAILURE;

    umb_conflict *c = umb_rev_conflict(&rev, argv[0]);
    if (c == NULL) {
        TraceLog(LOG_ERROR, "[UMB] No such conflict '%s' (or ambiguous prefix)", argv[0]);
        return EXIT_FAILURE;
    }

    umb_conflict chosen = *c;      // umb_rev_drop_conflict will shuffle the array
    umb_hash content;
    content[0] = '\0';
    const char *how = "";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--edit") == 0) {
            return resolve_edit(root, &chosen);
        }
        if (strcmp(argv[i], "--take") == 0 && i + 1 < argc) {
            const umb_side *side = side_by_selector(&chosen, argv[i+1]);
            if (side == NULL) {
                TraceLog(LOG_ERROR, "[UMB] No such side '%s'", argv[i+1]);
                return EXIT_FAILURE;
            }
            snprintf(content, UMB_HASH_LEN, "%s", side->hash);
            how = side->author;
            i++;
        }
        else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
            if (!umb_store_file(root, argv[i+1], content)) {
                TraceLog(LOG_ERROR, "[UMB] Could not read '%s'", argv[i+1]);
                return EXIT_FAILURE;
            }
            how = argv[i+1];
            i++;
        }
    }

    if (content[0] == '\0') {
        TraceLog(LOG_ERROR, "[UMB] Pick a resolution: --take <author|index>, --file <path>, or --edit");
        return EXIT_FAILURE;
    }

    umb_rev next;
    umb_rev_clear(&next);
    next = rev;
    snprintf(next.parent[0], UMB_HASH_LEN, "%s", tip);
    next.parent_count = 1;
    umb_rev_drop_conflict(&next, chosen.id);
    umb_rev_set(&next, chosen.path, content);
    snprintf(next.author, UMB_NAME_LEN, "%s", umb_user());
    snprintf(next.message, UMB_MSG_LEN, "resolve conflict on %s", chosen.path);
    next.time = (long long)time(NULL);

    umb_hash out;
    if (!umb_rev_save(root, &next, out)) return EXIT_FAILURE;
    if (!umb_ref_write(root, branch, out)) return EXIT_FAILURE;

    materialize(root, &next);

    printf("[%s %.8s] resolved %s (took %s)\n", branch, out, chosen.path, how);
    if (next.conflict_count > 0) {
        printf("  %d conflict%s still pending\n", next.conflict_count, (next.conflict_count == 1) ? "" : "s");
    }
    return EXIT_SUCCESS;
}

//----------------------------------------------------------------------------------
// status / log
//----------------------------------------------------------------------------------

int umb_cmd_status(const char *root) {
    char branch[UMB_NAME_LEN];
    if (!umb_head_read(root, branch)) return EXIT_FAILURE;

    printf("On branch %s (acting as %s)\n", branch, umb_user());

    umb_hash tip;
    if (!umb_ref_read(root, branch, tip)) {
        printf("No commits yet.\n");
        return EXIT_SUCCESS;
    }

    umb_rev rev;
    if (!umb_rev_load(root, tip, &rev)) return EXIT_FAILURE;

    if (rev.conflict_count > 0) {
        const char *prefer = umb_config(root, "prefer", "mine");
        printf("\n  UNRESOLVED CONFLICTS (%d)\n", rev.conflict_count);
        printf("  These files hold YOUR SIDE of a conflict, not merged truth.\n");
        printf("  The repository has no agreed content for them yet (prefer = %s).\n\n", prefer);
        for (int i = 0; i < rev.conflict_count; i++) {
            char age[32];
            format_age(rev.conflicts[i].opened, age, sizeof(age));
            printf("    %-24s  %.8s  owner %s, open %s\n",
                   rev.conflicts[i].path, rev.conflicts[i].id, rev.conflicts[i].owner, age);
        }
        printf("\n");
    }

    umb_rev disk;
    umb_rev_clear(&disk);
    scan_worktree(root, &disk);

    int modified = 0, added = 0;
    for (int i = 0; i < disk.entry_count; i++) {
        bool conflicted = false;
        for (int c = 0; c < rev.conflict_count; c++) {
            if (strcmp(rev.conflicts[c].path, disk.entries[i].path) == 0) { conflicted = true; break; }
        }
        if (conflicted) continue;

        umb_entry *e = umb_rev_entry(&rev, disk.entries[i].path);
        if (e == NULL) { printf("  new       %s\n", disk.entries[i].path); added++; }
        else if (strcmp(e->hash, disk.entries[i].hash) != 0) {
            printf("  modified  %s\n", disk.entries[i].path);
            modified++;
        }
    }

    if (modified == 0 && added == 0 && rev.conflict_count == 0) printf("Nothing to commit.\n");
    return EXIT_SUCCESS;
}

int umb_cmd_log(const char *root) {
    char branch[UMB_NAME_LEN];
    if (!umb_head_read(root, branch)) return EXIT_FAILURE;

    umb_hash cursor;
    if (!umb_ref_read(root, branch, cursor)) {
        printf("No commits yet.\n");
        return EXIT_SUCCESS;
    }

    for (int guard = 0; guard < MAX_ANCESTORS; guard++) {
        umb_rev rev;
        if (!umb_rev_load(root, cursor, &rev)) break;

        printf("%.8s  %-10s  %s\n", cursor, rev.author, rev.message);
        if (rev.conflict_count > 0) {
            printf("          carries %d pending conflict%s\n",
                   rev.conflict_count, (rev.conflict_count == 1) ? "" : "s");
        }

        if (rev.parent_count == 0) break;
        snprintf(cursor, UMB_HASH_LEN, "%s", rev.parent[0]);
    }
    return EXIT_SUCCESS;
}
