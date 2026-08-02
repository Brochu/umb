#ifndef UMB_H
#define UMB_H

#include <stdbool.h>

// Revisions are fixed capacity and live on the stack, which keeps the prototype free
// of allocation bookkeeping. It also means these numbers are real limits and a merge
// holds four revisions at once - see the /STACK setting in build.bat. Growable
// storage is a problem worth solving only once the conflict model has earned it.
//TODO: Look into max lenght for path
#define UMB_PATH_LEN        260     // MAX_PATH
#define UMB_HASH_LEN        17      // 16 hex digits + NUL
#define UMB_NAME_LEN        64
#define UMB_MSG_LEN         256
#define UMB_MAX_ENTRIES     256
#define UMB_MAX_CONFLICTS   32
#define UMB_MAX_SIDES       4
#define UMB_MAX_VARIANTS    16
#define UMB_MAX_KEYS        24
#define UMB_KEY_LEN         64

#define UMB_NO_HASH         "-"

typedef char umb_hash[UMB_HASH_LEN];

//----------------------------------------------------------------------------------
// repo.c - locating a repository, identity, config, policy, path helpers
//----------------------------------------------------------------------------------

// Walks up from the working directory looking for a '.repo'. 'root' receives the
// project root, i.e. the directory *containing* .repo.
bool umb_repo_find(char *root);
int  umb_repo_init(const char *name);

// Identity drives mine-first materialization. UMB_USER wins so a single machine can
// act as several people, which is exactly what testing the merge rules requires.
const char *umb_user(void);

void umb_join(char *out, const char *a, const char *b);
void umb_meta(char *out, const char *root, const char *leaf);
bool umb_mkdir(const char *path);
void umb_normalize(char *path);

const char *umb_config(const char *root, const char *key, const char *fallback);
bool umb_policy_denies(const char *root, const char *branch);

//----------------------------------------------------------------------------------
// store.c - content addressing and blobs
//----------------------------------------------------------------------------------

// FNV-1a 64. Not collision resistant; deliberate for a prototype where being able to
// eyeball a revision file matters more than adversarial safety.
void umb_hash_bytes(const void *data, int len, umb_hash out);
bool umb_hash_file(const char *path, umb_hash out);

bool umb_store_bytes(const char *root, const void *data, int len, umb_hash out);
bool umb_store_file(const char *root, const char *path, umb_hash out);

unsigned char *umb_load_blob(const char *root, const char *hash, int *len);
void umb_free_blob(unsigned char *data);
bool umb_checkout_blob(const char *root, const char *hash, const char *dest);

//----------------------------------------------------------------------------------
// rev.c - revisions, conflict records, refs
//----------------------------------------------------------------------------------

typedef struct umb_entry {
    char path[UMB_PATH_LEN];
    umb_hash hash;
} umb_entry;

typedef struct umb_side {
    umb_hash rev;                       // revision this side came from
    umb_hash hash;                      // blob at that revision
    char author[UMB_NAME_LEN];
} umb_side;

// Sides are stored symmetrically. Nothing here says which one "wins" - that is a
// materialization decision made per user, see umb_pick_side().
typedef struct umb_conflict {
    char id[UMB_HASH_LEN];
    char path[UMB_PATH_LEN];
    umb_hash base;                      // UMB_NO_HASH for add/add
    umb_side sides[UMB_MAX_SIDES];
    int side_count;
    char owner[UMB_NAME_LEN];
    long long opened;
} umb_conflict;

typedef struct umb_rev {
    umb_hash parent[2];
    int parent_count;
    char author[UMB_NAME_LEN];
    long long time;
    char message[UMB_MSG_LEN];
    umb_entry entries[UMB_MAX_ENTRIES];
    int entry_count;
    umb_conflict conflicts[UMB_MAX_CONFLICTS];
    int conflict_count;
} umb_rev;

void umb_rev_clear(umb_rev *r);
bool umb_rev_load(const char *root, const char *hash, umb_rev *out);
bool umb_rev_save(const char *root, const umb_rev *r, umb_hash out);

umb_entry    *umb_rev_entry(umb_rev *r, const char *path);
void          umb_rev_set(umb_rev *r, const char *path, const char *hash);
umb_conflict *umb_rev_conflict(umb_rev *r, const char *id);
bool          umb_rev_drop_conflict(umb_rev *r, const char *id);

bool umb_ref_read(const char *root, const char *branch, umb_hash out);
bool umb_ref_write(const char *root, const char *branch, const char *hash);
bool umb_head_read(const char *root, char *branch);
bool umb_head_write(const char *root, const char *branch);
bool umb_tip(const char *root, umb_hash out);

//----------------------------------------------------------------------------------
// variant.c - audience scoped paths
//----------------------------------------------------------------------------------

typedef struct umb_variant {
    char path[UMB_PATH_LEN];            // the private path, never committed
    char pub[UMB_PATH_LEN];             // committed public counterpart
    char keys[UMB_MAX_KEYS][UMB_KEY_LEN];
    int key_count;
} umb_variant;

int  umb_variants_load(const char *root, umb_variant *out, int max);
bool umb_variant_is_private(const char *root, const char *relpath);
int  umb_cmd_variants(const char *root);
int  umb_cmd_variant(const char *root, int argc, const char **argv);

//----------------------------------------------------------------------------------
// merge.c - committing, merging, conflicts
//----------------------------------------------------------------------------------

int umb_cmd_commit(const char *root, int argc, const char **argv);
int umb_cmd_branch(const char *root, int argc, const char **argv);
int umb_cmd_switch(const char *root, int argc, const char **argv);
int umb_cmd_merge(const char *root, int argc, const char **argv);
int umb_cmd_conflicts(const char *root);
int umb_cmd_conflict(const char *root, int argc, const char **argv);
int umb_cmd_resolve(const char *root, int argc, const char **argv);
int umb_cmd_status(const char *root);
int umb_cmd_log(const char *root);

#endif // UMB_H
