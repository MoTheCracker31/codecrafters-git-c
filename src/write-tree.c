#include "write-tree.h"
#include "hash-object.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <winsock.h>

/* A single index entry, with the full slash-separated path git stores
   (e.g. "src/main.c"). */
typedef struct
{
    unsigned int mode;
    unsigned char sha[SHA1_SIZE];
    char *path;
} IndexEntry;

/* Growable byte buffer used to assemble a tree object's body. */
typedef struct
{
    char *data;
    size_t len;
    size_t cap;
} Buffer;

static int buf_append(Buffer *b, const void *src, size_t n)
{
    if (b->len + n > b->cap)
    {
        size_t newcap = b->cap ? b->cap * 2 : 256;
        while (newcap < b->len + n)
            newcap *= 2;
        char *nd = realloc(b->data, newcap);
        if (!nd)
            return -1;
        b->data = nd;
        b->cap = newcap;
    }
    memcpy(b->data + b->len, src, n);
    b->len += n;
    return 0;
}

/* Hash `content` as an already-headered object, store it zlib-compressed under
   .git/objects/, and return its 20-byte binary SHA in out_sha. */
static int write_object(const char *content, size_t len, unsigned char out_sha[SHA1_SIZE])
{
    unsigned char *sha = hash_object(content, (long)len);
    if (!sha)
        return -1;
    memcpy(out_sha, sha, SHA1_SIZE);

    char hex[SHA1_SIZE * 2 + 1];
    for (int i = 0; i < SHA1_SIZE; i++)
        sprintf(&hex[i * 2], "%02x", sha[i]);
    hex[SHA1_SIZE * 2] = '\0';
    free(sha);

    char dir[128], path[192];
    snprintf(dir, sizeof(dir), ".git%cobjects%c%.2s", PATH_SEP_CHAR, PATH_SEP_CHAR, hex);
    snprintf(path, sizeof(path), "%s%c%s", dir, PATH_SEP_CHAR, hex + 2);

    if (make_dir(dir) == -1)
    {
        fprintf(stderr, "write-tree: failed to create object directory: %s\n", strerror(errno));
        return -1;
    }

    FILE *destFile = fopen(path, "wb");
    if (destFile == NULL)
    {
        fprintf(stderr, "write-tree: failed to create object file: %s\n", strerror(errno));
        return -1;
    }
    zlib_compress(content, len, destFile);
    fclose(destFile);
    return 0;
}

/* Recursively build the tree object for entries[start, end) that all share the
   directory prefix of `prefixLen` bytes.  Entries are assumed sorted by path
   (the git index always is), which is exactly the order git writes tree
   entries in, so we can group consecutive entries per subdirectory.

   Writes the tree object and returns its 20-byte SHA in out_sha. */
static int build_tree(IndexEntry *entries, int start, int end,
                      size_t prefixLen, unsigned char out_sha[SHA1_SIZE])
{
    Buffer body = {0};
    int i = start;

    while (i < end)
    {
        const char *name = entries[i].path + prefixLen;
        const char *slash = strchr(name, '/');

        if (!slash)
        {
            /* A file living directly in this directory: "<mode> <name>\0<sha>". */
            char modeStr[16];
            sprintf(modeStr, "%o", entries[i].mode);
            if (buf_append(&body, modeStr, strlen(modeStr)) ||
                buf_append(&body, " ", 1) ||
                buf_append(&body, name, strlen(name)) ||
                buf_append(&body, "", 1) /* NUL */ ||
                buf_append(&body, entries[i].sha, SHA1_SIZE))
            {
                free(body.data);
                return -1;
            }
            i++;
        }
        else
        {
            /* A subdirectory: recurse over every consecutive entry under it. */
            size_t subNameLen = (size_t)(slash - name);
            size_t newPrefixLen = prefixLen + subNameLen + 1; /* include the '/' */

            int j = i;
            while (j < end &&
                   strncmp(entries[j].path + prefixLen, name, subNameLen) == 0 &&
                   entries[j].path[prefixLen + subNameLen] == '/')
                j++;

            unsigned char sub_sha[SHA1_SIZE];
            if (build_tree(entries, i, j, newPrefixLen, sub_sha) != 0)
            {
                free(body.data);
                return -1;
            }

            /* Directory mode in a tree object is "40000" (no leading zero). */
            if (buf_append(&body, "40000", 5) ||
                buf_append(&body, " ", 1) ||
                buf_append(&body, name, subNameLen) ||
                buf_append(&body, "", 1) /* NUL */ ||
                buf_append(&body, sub_sha, SHA1_SIZE))
            {
                free(body.data);
                return -1;
            }
            i = j;
        }
    }

    /* Prepend "tree <size>\0" and store. */
    char header[32];
    int headerLen = sprintf(header, "tree %zu", body.len) + 1; /* +1 for NUL */
    size_t totalSize = headerLen + body.len;

    char *obj = malloc(totalSize);
    if (!obj)
    {
        free(body.data);
        return -1;
    }
    memcpy(obj, header, headerLen);
    if (body.len)
        memcpy(obj + headerLen, body.data, body.len);
    free(body.data);

    int rc = write_object(obj, totalSize, out_sha);
    free(obj);
    return rc;
}

int read_git_index_file(FILE *index)
{
    unsigned int indexMagicNumber;
    unsigned int versionNumber;
    unsigned int numberOfEntries;

    if (fread(&indexMagicNumber, sizeof(unsigned int), 1, index) != 1 ||
        fread(&versionNumber, sizeof(unsigned int), 1, index) != 1 ||
        fread(&numberOfEntries, sizeof(unsigned int), 1, index) != 1)
    {
        fprintf(stderr, "fatal: error reading index header\n");
        return 1;
    }
    if (ntohl(indexMagicNumber) != 0x44495243u) /* "DIRC" */
    {
        fprintf(stderr, "fatal: index file corrupt (bad magic number)\n");
        return 1;
    }
    if (ntohl(versionNumber) != 2)
    {
        fprintf(stderr, "fatal: unsupported index version %u\n", ntohl(versionNumber));
        return 1;
    }
    numberOfEntries = ntohl(numberOfEntries);

    IndexEntry *entries = NULL;
    if (numberOfEntries > 0)
    {
        entries = calloc(numberOfEntries, sizeof(IndexEntry));
        if (!entries)
        {
            fprintf(stderr, "fatal: out of memory\n");
            return 1;
        }
    }

    for (unsigned int i = 0; i < numberOfEntries; i++)
    {
        unsigned short flag;

        /* Skip ctime(8) + mtime(8) + dev(4) + ino(4) = 24 bytes */
        fseek(index, 6 * sizeof(unsigned int), SEEK_CUR);
        fread(&entries[i].mode, sizeof(unsigned int), 1, index);
        entries[i].mode = ntohl(entries[i].mode);
        /* Skip uid(4) + gid(4) + file_size(4) = 12 bytes */
        fseek(index, 3 * sizeof(unsigned int), SEEK_CUR);
        if (fread(entries[i].sha, sizeof(unsigned char), SHA1_SIZE, index) != SHA1_SIZE)
        {
            fprintf(stderr, "fatal: error reading sha of entry %u\n", i);
            goto cleanup_err;
        }
        if (fread(&flag, sizeof(unsigned short), 1, index) != 1)
        {
            fprintf(stderr, "fatal: error reading flags of entry %u\n", i);
            goto cleanup_err;
        }

        unsigned short fileNameLen = ntohs(flag) & 0x0FFF;
        entries[i].path = malloc(fileNameLen + 1);
        if (!entries[i].path)
        {
            fprintf(stderr, "fatal: out of memory\n");
            goto cleanup_err;
        }
        fread(entries[i].path, sizeof(char), fileNameLen, index);
        entries[i].path[fileNameLen] = '\0';

        /* Entry is the 62 fixed bytes + name + NUL, padded to an 8-byte
           boundary; skip the NUL plus any padding. */
        int entrySizeBeforePadding = 62 + fileNameLen + 1;
        int padding = 8 - (entrySizeBeforePadding % 8);
        if (padding == 8)
            padding = 0;
        fseek(index, 1 + padding, SEEK_CUR);
    }

    unsigned char root_sha[SHA1_SIZE];
    if (build_tree(entries, 0, (int)numberOfEntries, 0, root_sha) != 0)
        goto cleanup_err;

    char hex[SHA1_SIZE * 2 + 1];
    for (int i = 0; i < SHA1_SIZE; i++)
        sprintf(&hex[i * 2], "%02x", root_sha[i]);
    hex[SHA1_SIZE * 2] = '\0';
    printf("%s\n", hex);

    for (unsigned int i = 0; i < numberOfEntries; i++)
        free(entries[i].path);
    free(entries);
    return 0;

cleanup_err:
    if (entries)
        for (unsigned int i = 0; i < numberOfEntries; i++)
            free(entries[i].path);
    free(entries);
    return 1;
}
