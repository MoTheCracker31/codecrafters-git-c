#include "write-tree.h"
#include "hash-object.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <winsock.h>

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

    indexEntryFixed *entries = malloc(sizeof(indexEntryFixed) * numberOfEntries);
    char **fileNames = malloc(sizeof(char *) * numberOfEntries);
    if (!entries || !fileNames)
    {
        fprintf(stderr, "fatal: out of memory\n");
        free(entries);
        free(fileNames);
        return 1;
    }
    for (int i = 0; i < numberOfEntries; i++)
    {
        fileNames[i] = malloc(4096);
        if (!fileNames[i])
        {
            fprintf(stderr, "fatal: out of memory\n");
            for (int j = 0; j < i; j++) free(fileNames[j]);
            free(fileNames);
            free(entries);
            return 1;
        }
    }

    for (int i = 0; i < numberOfEntries; i++)
    {
        /* Skip ctime(8) + mtime(8) + dev(4) + ino(4) = 24 bytes */
        fseek(index, 6 * sizeof(unsigned int), SEEK_CUR);
        fread(&entries[i].mode, sizeof(unsigned int), 1, index);
        entries[i].mode = ntohl(entries[i].mode);
        /* Skip uid(4) + gid(4) + file_size(4) = 12 bytes */
        fseek(index, 3 * sizeof(unsigned int), SEEK_CUR);
        if (fread(entries[i].sha, sizeof(unsigned char), SHA1_SIZE, index) != SHA1_SIZE)
        {
            fprintf(stderr, "fatal: error reading sha of entry %d\n", i);
            goto cleanup_err;
        }
        if (fread(&entries[i].flag, sizeof(unsigned short), 1, index) != 1)
        {
            fprintf(stderr, "fatal: error reading flags of entry %d\n", i);
            goto cleanup_err;
        }

        unsigned short fileNameLen = ntohs(entries[i].flag) & 0x0FFF;
        fread(fileNames[i], sizeof(char), fileNameLen, index);
        fileNames[i][fileNameLen] = '\0';

        /* +1 for NUL terminator already in the entry; seek past NUL + padding */
        int entrySizeBeforePadding = 62 + fileNameLen + 1;
        int padding = 8 - (entrySizeBeforePadding % 8);
        if (padding == 8) padding = 0;
        /* Skip: NUL terminator (1) + padding bytes */
        fseek(index, 1 + padding, SEEK_CUR);
    }

    /* Calculate tree content size: each entry is "<mode> <name>\0<20-byte-sha>" */
    size_t contentSize = 0;
    for (int i = 0; i < numberOfEntries; i++)
    {
        char modeStr[16];
        sprintf(modeStr, "%o", entries[i].mode);
        contentSize += strlen(modeStr) + 1 + strlen(fileNames[i]) + 1 + SHA1_SIZE;
    }

    /* Build header: "tree <contentSize>\0" */
    char header[32];
    int headerLen = sprintf(header, "tree %zu", contentSize) + 1; /* +1 for NUL */

    size_t totalSize = headerLen + contentSize;
    char *treeContent = malloc(totalSize);
    if (!treeContent)
    {
        fprintf(stderr, "fatal: out of memory\n");
        goto cleanup_err;
    }
    memcpy(treeContent, header, headerLen);

    /* Populate entries: "<mode> <filename>\0<20-byte-sha>" */
    size_t offset = headerLen;
    for (int i = 0; i < numberOfEntries; i++)
    {
        char modeStr[16];
        sprintf(modeStr, "%o", entries[i].mode);
        size_t modeLen = strlen(modeStr);
        size_t nameLen = strlen(fileNames[i]);

        memcpy(treeContent + offset, modeStr, modeLen);
        offset += modeLen;
        treeContent[offset++] = ' ';
        memcpy(treeContent + offset, fileNames[i], nameLen);
        offset += nameLen;
        treeContent[offset++] = '\0';
        memcpy(treeContent + offset, entries[i].sha, SHA1_SIZE);
        offset += SHA1_SIZE;
    }

    /* Hash the tree content */
    unsigned char *sha = hash_object(treeContent, (long)totalSize);
    if (!sha)
    {
        fprintf(stderr, "fatal: failed to hash tree object\n");
        free(treeContent);
        goto cleanup_err;
    }

    char shaHex[SHA1_SIZE * 2 + 1];
    for (int i = 0; i < SHA1_SIZE; i++)
        sprintf(&shaHex[i * 2], "%02x", sha[i]);
    shaHex[SHA1_SIZE * 2] = '\0';
    free(sha);

    /* Write to .git/objects/<xx>/<38-char-hash> */
    char objectDir[128];
    char objectPath[192];
    snprintf(objectDir, sizeof(objectDir), ".git%cobjects%c%.2s",
             PATH_SEP_CHAR, PATH_SEP_CHAR, shaHex);
    snprintf(objectPath, sizeof(objectPath), "%s%c%s",
             objectDir, PATH_SEP_CHAR, shaHex + 2);

    if (make_dir(objectDir) == -1)
    {
        fprintf(stderr, "write-tree: failed to create object directory: %s\n", strerror(errno));
        free(treeContent);
        goto cleanup_err;
    }

    FILE *destFile = fopen(objectPath, "wb");
    if (destFile == NULL)
    {
        fprintf(stderr, "write-tree: failed to create object file: %s\n", strerror(errno));
        free(treeContent);
        goto cleanup_err;
    }

    zlib_compress(treeContent, totalSize, destFile);
    fclose(destFile);
    free(treeContent);

    printf("%s\n", shaHex);

    for (int i = 0; i < numberOfEntries; i++) free(fileNames[i]);
    free(fileNames);
    free(entries);
    return 0;

cleanup_err:
    for (int i = 0; i < numberOfEntries; i++) free(fileNames[i]);
    free(fileNames);
    free(entries);
    return 1;
}
