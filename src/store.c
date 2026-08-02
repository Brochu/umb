#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"
#include "umb.h"

// FNV-1a 64. Whole files only - no chunking, no packing. Lore already solved content
// defined chunking properly and rebuilding it here would only distract from the
// conflict model this prototype exists to test.
void umb_hash_bytes(const void *data, int len, umb_hash out) {
    const unsigned char *bytes = (const unsigned char *)data;
    unsigned long long h = 14695981039346656037ULL;
    for (int i = 0; i < len; i++) {
        h ^= (unsigned long long)bytes[i];
        h *= 1099511628211ULL;
    }
    snprintf(out, UMB_HASH_LEN, "%016llx", h);
}

bool umb_hash_file(const char *path, umb_hash out) {
    if (!FileExists(path)) return false;        // asking about an absent file is normal
    int len = 0;
    unsigned char *data = LoadFileData(path, &len);
    if (data == NULL) return false;
    umb_hash_bytes(data, len, out);
    UnloadFileData(data);
    return true;
}

static void blob_path(char *out, const char *root, const char *hash) {
    snprintf(out, UMB_PATH_LEN, "%s/.repo/objects/%s", root, hash);
}

bool umb_store_bytes(const char *root, const void *data, int len, umb_hash out) {
    umb_hash_bytes(data, len, out);

    char path[UMB_PATH_LEN];
    blob_path(path, root, out);
    if (FileExists(path)) return true;              // content addressed, so already done

    return SaveFileData(path, (void *)data, len);
}

bool umb_store_file(const char *root, const char *path, umb_hash out) {
    int len = 0;
    unsigned char *data = LoadFileData(path, &len);
    if (data == NULL) return false;
    bool ok = umb_store_bytes(root, data, len, out);
    UnloadFileData(data);
    return ok;
}

unsigned char *umb_load_blob(const char *root, const char *hash, int *len) {
    char path[UMB_PATH_LEN];
    blob_path(path, root, hash);
    return LoadFileData(path, len);
}

void umb_free_blob(unsigned char *data) {
    if (data != NULL) UnloadFileData(data);
}

bool umb_checkout_blob(const char *root, const char *hash, const char *dest) {
    int len = 0;
    unsigned char *data = umb_load_blob(root, hash, &len);
    if (data == NULL) {
        TraceLog(LOG_ERROR, "[UMB] Missing blob '%s'", hash);
        return false;
    }
    bool ok = SaveFileData(dest, data, len);
    umb_free_blob(data);
    return ok;
}
