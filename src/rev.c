#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "umb.h"

#define REV_BUFFER (1024*1024)

void umb_rev_clear(umb_rev *r) {
    memset(r, 0, sizeof(umb_rev));
}

//----------------------------------------------------------------------------------
// Serialization
//
// Plain text on purpose. Being able to 'type' a revision and see the conflict record
// with your own eyes is worth far more at this stage than a compact encoding.
// Variable length fields go last on their line so they may contain spaces.
//----------------------------------------------------------------------------------

static int cmp_entry(const void *a, const void *b) {
    return strcmp(((const umb_entry *)a)->path, ((const umb_entry *)b)->path);
}

static int cmp_conflict(const void *a, const void *b) {
    return strcmp(((const umb_conflict *)a)->id, ((const umb_conflict *)b)->id);
}

bool umb_rev_save(const char *root, const umb_rev *rev, umb_hash out) {
    // Work on a copy so the caller's revision is not reordered underneath it. Sorting
    // is what makes two identical trees hash identically.
    umb_rev r = *rev;
    qsort(r.entries, r.entry_count, sizeof(umb_entry), cmp_entry);
    qsort(r.conflicts, r.conflict_count, sizeof(umb_conflict), cmp_conflict);

    char *buf = (char *)malloc(REV_BUFFER);
    if (buf == NULL) return false;

    int n = 0;
    for (int i = 0; i < r.parent_count; i++) {
        n += snprintf(buf + n, REV_BUFFER - n, "parent %s\n", r.parent[i]);
    }
    n += snprintf(buf + n, REV_BUFFER - n, "author %s\n", r.author);
    n += snprintf(buf + n, REV_BUFFER - n, "time %lld\n", r.time);
    n += snprintf(buf + n, REV_BUFFER - n, "message %s\n", r.message);

    for (int i = 0; i < r.entry_count; i++) {
        n += snprintf(buf + n, REV_BUFFER - n, "tree %s %s\n", r.entries[i].hash, r.entries[i].path);
    }

    for (int i = 0; i < r.conflict_count; i++) {
        const umb_conflict *c = &r.conflicts[i];
        n += snprintf(buf + n, REV_BUFFER - n, "conflict %s base=%s owner=%s opened=%lld",
                      c->id, c->base, c->owner, c->opened);
        for (int s = 0; s < c->side_count; s++) {
            n += snprintf(buf + n, REV_BUFFER - n, " side=%s:%s:%s",
                          c->sides[s].rev, c->sides[s].hash, c->sides[s].author);
        }
        n += snprintf(buf + n, REV_BUFFER - n, " path=%s\n", c->path);
    }

    umb_hash_bytes(buf, n, out);

    char path[UMB_PATH_LEN];
    snprintf(path, UMB_PATH_LEN, "%s/.repo/revs/%s", root, out);
    bool ok = SaveFileData(path, buf, n);

    free(buf);
    return ok;
}

static void parse_side(umb_conflict *c, char *spec) {
    if (c->side_count >= UMB_MAX_SIDES) return;

    umb_side *side = &c->sides[c->side_count];
    char *first = strchr(spec, ':');
    if (first == NULL) return;
    *first = '\0';
    char *second = strchr(first + 1, ':');
    if (second == NULL) return;
    *second = '\0';

    snprintf(side->rev, UMB_HASH_LEN, "%s", spec);
    snprintf(side->hash, UMB_HASH_LEN, "%s", first + 1);
    snprintf(side->author, UMB_NAME_LEN, "%s", second + 1);
    c->side_count++;
}

static void parse_conflict(umb_rev *r, char *rest) {
    if (r->conflict_count >= UMB_MAX_CONFLICTS) return;

    umb_conflict *c = &r->conflicts[r->conflict_count];
    memset(c, 0, sizeof(umb_conflict));
    snprintf(c->base, UMB_HASH_LEN, "%s", UMB_NO_HASH);

    char *id = rest;
    char *sp = strchr(rest, ' ');
    if (sp == NULL) return;
    *sp = '\0';
    snprintf(c->id, UMB_HASH_LEN, "%s", id);

    char *cursor = sp + 1;
    while (cursor != NULL && *cursor != '\0') {
        // path= is always last and runs to end of line, so it may contain spaces
        if (strncmp(cursor, "path=", 5) == 0) {
            snprintf(c->path, UMB_PATH_LEN, "%s", cursor + 5);
            break;
        }

        char *next = strchr(cursor, ' ');
        if (next != NULL) *next = '\0';

        if      (strncmp(cursor, "base=",   5) == 0) snprintf(c->base,  UMB_HASH_LEN, "%s", cursor + 5);
        else if (strncmp(cursor, "owner=",  6) == 0) snprintf(c->owner, UMB_NAME_LEN, "%s", cursor + 6);
        else if (strncmp(cursor, "opened=", 7) == 0) c->opened = _atoi64(cursor + 7);
        else if (strncmp(cursor, "side=",   5) == 0) parse_side(c, cursor + 5);

        cursor = (next != NULL) ? next + 1 : NULL;
    }

    r->conflict_count++;
}

bool umb_rev_load(const char *root, const char *hash, umb_rev *out) {
    umb_rev_clear(out);
    if (hash == NULL || hash[0] == '\0') return false;

    char path[UMB_PATH_LEN];
    snprintf(path, UMB_PATH_LEN, "%s/.repo/revs/%s", root, hash);
    if (!FileExists(path)) return false;
    char *text = LoadFileText(path);
    if (text == NULL) return false;

    char *lines = NULL;
    char *line = strtok_s(text, "\r\n", &lines);
    while (line != NULL) {
        if (strncmp(line, "parent ", 7) == 0) {
            if (out->parent_count < 2) {
                snprintf(out->parent[out->parent_count], UMB_HASH_LEN, "%s", line + 7);
                out->parent_count++;
            }
        }
        else if (strncmp(line, "author ", 7) == 0) {
            snprintf(out->author, UMB_NAME_LEN, "%s", line + 7);
        }
        else if (strncmp(line, "time ", 5) == 0) {
            out->time = _atoi64(line + 5);
        }
        else if (strncmp(line, "message ", 8) == 0) {
            snprintf(out->message, UMB_MSG_LEN, "%s", line + 8);
        }
        else if (strncmp(line, "tree ", 5) == 0) {
            if (out->entry_count < UMB_MAX_ENTRIES) {
                char *h = line + 5;
                char *sp = strchr(h, ' ');
                if (sp != NULL) {
                    *sp = '\0';
                    umb_entry *e = &out->entries[out->entry_count];
                    snprintf(e->hash, UMB_HASH_LEN, "%s", h);
                    snprintf(e->path, UMB_PATH_LEN, "%s", sp + 1);
                    out->entry_count++;
                }
            }
        }
        else if (strncmp(line, "conflict ", 9) == 0) {
            parse_conflict(out, line + 9);
        }

        line = strtok_s(NULL, "\r\n", &lines);
    }

    UnloadFileText(text);
    return true;
}

//----------------------------------------------------------------------------------
// Lookup helpers
//----------------------------------------------------------------------------------

umb_entry *umb_rev_entry(umb_rev *r, const char *path) {
    for (int i = 0; i < r->entry_count; i++) {
        if (strcmp(r->entries[i].path, path) == 0) return &r->entries[i];
    }
    return NULL;
}

void umb_rev_set(umb_rev *r, const char *path, const char *hash) {
    umb_entry *e = umb_rev_entry(r, path);
    if (e != NULL) {
        snprintf(e->hash, UMB_HASH_LEN, "%s", hash);
        return;
    }
    if (r->entry_count >= UMB_MAX_ENTRIES) {
        TraceLog(LOG_WARNING, "[UMB] Entry limit reached, dropping '%s'", path);
        return;
    }
    e = &r->entries[r->entry_count++];
    snprintf(e->path, UMB_PATH_LEN, "%s", path);
    snprintf(e->hash, UMB_HASH_LEN, "%s", hash);
}

// Accepts a unique prefix so conflict ids stay typeable.
umb_conflict *umb_rev_conflict(umb_rev *r, const char *id) {
    size_t len = strlen(id);
    umb_conflict *found = NULL;
    for (int i = 0; i < r->conflict_count; i++) {
        if (strncmp(r->conflicts[i].id, id, len) == 0) {
            if (found != NULL) return NULL;         // ambiguous prefix
            found = &r->conflicts[i];
        }
    }
    return found;
}

bool umb_rev_drop_conflict(umb_rev *r, const char *id) {
    for (int i = 0; i < r->conflict_count; i++) {
        if (strcmp(r->conflicts[i].id, id) == 0) {
            for (int j = i; j < r->conflict_count - 1; j++) r->conflicts[j] = r->conflicts[j+1];
            r->conflict_count--;
            return true;
        }
    }
    return false;
}

//----------------------------------------------------------------------------------
// Refs and HEAD
//----------------------------------------------------------------------------------

// Branch names may contain '/', ref files may not.
static void ref_path(char *out, const char *root, const char *branch) {
    char safe[UMB_NAME_LEN];
    snprintf(safe, UMB_NAME_LEN, "%s", branch);
    for (char *p = safe; *p; p++) {
        if (*p == '/' || *p == '\\') *p = '%';
    }
    snprintf(out, UMB_PATH_LEN, "%s/.repo/refs/%s", root, safe);
}

// A branch with no commits yet is an ordinary situation, not a problem, so this must
// not leave raylib complaining about a missing file.
bool umb_ref_read(const char *root, const char *branch, umb_hash out) {
    char path[UMB_PATH_LEN];
    ref_path(path, root, branch);
    if (!FileExists(path)) return false;
    char *text = LoadFileText(path);
    if (text == NULL) return false;

    snprintf(out, UMB_HASH_LEN, "%s", text);
    for (char *p = out; *p; p++) {
        if (*p == '\r' || *p == '\n') { *p = '\0'; break; }
    }

    UnloadFileText(text);
    return (out[0] != '\0');
}

bool umb_ref_write(const char *root, const char *branch, const char *hash) {
    char path[UMB_PATH_LEN];
    ref_path(path, root, branch);
    char text[UMB_HASH_LEN + 2];
    snprintf(text, sizeof(text), "%s\n", hash);
    return SaveFileText(path, text);
}

bool umb_head_read(const char *root, char *branch) {
    char path[UMB_PATH_LEN];
    umb_meta(path, root, "HEAD");
    if (!FileExists(path)) return false;
    char *text = LoadFileText(path);
    if (text == NULL) return false;

    snprintf(branch, UMB_NAME_LEN, "%s", text);
    for (char *p = branch; *p; p++) {
        if (*p == '\r' || *p == '\n') { *p = '\0'; break; }
    }

    UnloadFileText(text);
    return (branch[0] != '\0');
}

bool umb_head_write(const char *root, const char *branch) {
    char path[UMB_PATH_LEN];
    umb_meta(path, root, "HEAD");
    char text[UMB_NAME_LEN + 2];
    snprintf(text, sizeof(text), "%s\n", branch);
    return SaveFileText(path, text);
}

bool umb_tip(const char *root, umb_hash out) {
    char branch[UMB_NAME_LEN];
    if (!umb_head_read(root, branch)) return false;
    return umb_ref_read(root, branch, out);
}
