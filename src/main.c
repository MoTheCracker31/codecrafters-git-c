#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "zlib.h"
#include <assert.h>
#include "platform.h"
#include "hash-object.h"
#include "ls-tree.h"

#define CHUNK 16384

struct Blob
{
    char *data;
};

struct Node
{
    char *name;
    struct Node *subDir;
};

struct Commit
{
    char *message;
    char *author;
    char *commiter;
    struct Commit *parent;
};

int zlib_decompress(FILE *objectFile, FILE *destFile);
void cat_file(FILE *objectFile, FILE *destination);

void cat_file(FILE *objectFile, FILE *destination)
{
    zlib_decompress(objectFile, destination);
    rewind(destination);
    unsigned size;
    char obj[6];
    fscanf(destination, "%s ", obj);
    fscanf(destination, "%d", &size);
    fseek(destination, 1, SEEK_CUR);
    char buf[size];
    fread(buf, sizeof(char), size, destination);
    fwrite(buf, sizeof(char), size, stdout);
}

int zlib_decompress(FILE *objectFile, FILE *destFile)
{
    unsigned have;
    char in[CHUNK];
    char out[CHUNK];
    int ret;
    z_stream strm;

    strm.opaque = Z_NULL;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return ret;
    /* Decompress until the end of the file*/
    do
    {
        strm.avail_in = fread(in, sizeof(char), CHUNK, objectFile);
        if (ferror(objectFile))
        {
            (void)deflateEnd(&strm);
            return Z_ERRNO;
        }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        do
        {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);
            switch (ret)
            {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            default:
                break;
            }
            have = CHUNK - strm.avail_out;
            if (fwrite(out, sizeof(char), have, destFile) != have || ferror(destFile))
            {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);
}

int main(int argc, char *argv[])
{
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2)
    {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "init") == 0)
    {
        // You can use print statements as follows for debugging, they'll be visible when running tests.
        fprintf(stderr, "Logs from your program will appear here!\n");

        char objects_path[256];
        char refs_path[256];

        join_path(objects_path, sizeof(objects_path), ".git", "objects");
        join_path(refs_path, sizeof(refs_path), ".git", "refs");

        if (make_dir(".git") == -1 ||
            make_dir(objects_path) == -1 ||
            make_dir(refs_path) == -1)
        {
            fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
            return 1;
        }

        FILE *headFile = fopen(".git/HEAD", "w");
        if (headFile == NULL)
        {
            fprintf(stderr, "Failed to create .git/HEAD file: %s\n", strerror(errno));
            return 1;
        }
        fprintf(headFile, "ref: refs/heads/main\n");
        fclose(headFile);

        printf("Initialized git directory\n");
    }
    else if (strcmp(command, "cat-file") == 0 && strcmp(argv[2], "-p") == 0)
    {
        char fileName[41];
        char dir[100] = ".git/objects/";

        snprintf(dir, sizeof(dir), ".git/objects/%.2s/%s", argv[3], argv[3] + 2);

        FILE *objectFile = fopen(dir, "rb");
        strcat(dir, "TMP");
        FILE *destFile = fopen(dir, "wb+"); // must delet later
        cat_file(objectFile, destFile);
    }

    else if (strcmp(command, "ls-tree") == 0)
    {
        if (argc < 3 || strlen(argv[2]) != 40)
        {
            fprintf(stderr, "Must provide a correct SHA1 hash of the tree object");
            return 1;
        }
        char path[100];
        snprintf(path, sizeof(path), ".git/objects/%.2s/%s", argv[2], argv[2] + 2);

        FILE *treeFile = fopen(path, "rb");
        if (treeFile == NULL && errno == ENOFILE)
        {
            fprintf(stderr, "fatal: not a tree object\n");
            return 128;
        }

        FILE *tmpFile = fopen("./tmpTreeFile", "wb+");

        zlib_decompress(treeFile, tmpFile);

        int ls_tree_returncode = ls_tree(tmpFile);
        if (ls_tree_returncode != 0)
            return ls_tree_returncode;

        fclose(treeFile);
        fclose(tmpFile);
        remove("./tmpTreeFile");
    }

    else if (strcmp(command, "hash-object") == 0 && strcmp(argv[2], "-w") == 0)
    {

        FILE *objectFile = fopen(argv[3], "r");
        if (objectFile == NULL)
        {
            fprintf(stderr, "Error when trying to open object file: %s", strerror(errno));
            return 1;
        }
        /*Get the size of object file and declar buffer for the content of the file*/
        fseek(objectFile, 0L, SEEK_END);
        long contentSize = ftell(objectFile);
        rewind(objectFile);
        if (contentSize == -1)
        {
            fprintf(stderr, "Error happen when getting the size of the file: %s", strerror(errno));
            return 1;
        }
        char objectContent[contentSize];
        /*read the content of the file */
        fread(objectContent, sizeof(char), contentSize, objectFile);
        fclose(objectFile);

        size_t headerSize = strlen("blob ") + sizeof(char) * 11; // 11 is the maximum number of characters needed to represent a long integer
        char fileHeader[headerSize];
        sprintf(fileHeader, "blob %li\0", contentSize);

        char *unhashedContent = (char *)malloc(sizeof(char) * (contentSize + headerSize));
        memset(unhashedContent, 0, sizeof(char) * (contentSize + headerSize));

        memcpy(unhashedContent, fileHeader, strlen(fileHeader) + 1);
        memcpy(&unhashedContent[strlen(fileHeader) + 1], objectContent, contentSize);
        unsigned long unhashedContentLen = contentSize + strlen(fileHeader) + 1;
        /*Hash the content of the file with its header*/
        unsigned char *hashed_object = hash_object(unhashedContent, unhashedContentLen);

        unsigned char objectPath[SHA1_SIZE * 2 + 2 + 20];
        unsigned char objectHash[SHA1_SIZE * 2 + 1];
        char objectDir[16] = ".git\\objects\\";

        for (int i = 0; i < SHA1_SIZE + 1; i++)
        {
            sprintf(&objectHash[i * 2], "%02x", hashed_object[i]);
        }
        objectHash[SHA1_SIZE * 2] = '\0';
        free(hashed_object);

        objectHash[SHA1_SIZE * 2 + 2 - 1] = '\0';
        fprintf(stdout, "%s\n", objectHash);

        snprintf(objectPath, sizeof(objectPath), ".git%cobjects%c%.2s%c%s", PATH_SEP_CHAR, PATH_SEP_CHAR, objectHash, PATH_SEP_CHAR, objectHash + 2);

        strncat(objectDir, objectHash, 2);

        if (make_dir(objectDir) == -1 && errno != EEXIST)
        {
            fprintf(stderr, "hash-object failed to create object file: %s\n", strerror(errno));
            return 1;
        }

        FILE *destFile = fopen(objectPath, "wb");
        if (destFile == NULL || ferror(destFile))
        {
            fprintf(stderr, "Error opening destination file: %s", strerror(errno));
            return 1;
        }
        zlib_compress(unhashedContent, unhashedContentLen, destFile);
        free(unhashedContent);
        fclose(destFile);
    }

    else
    {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }

    return 0;
}