#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "zlib.h"
#include <assert.h>

#define CHUNK 16384

struct Blob{
    char* data;
};

struct Node{
    char* name;
    struct Node* subDir;
};

struct Commit{
    char* message;
    char* author;
    char* commiter;
    struct Commit* parent;
};



int zlib_decompress(FILE *objectFile, FILE *destFile);
void cat_file(FILE *objectFile, FILE *destination);


void cat_file(FILE *objectFile, FILE *destination){
    zlib_decompress(objectFile, destination);
    char buf[1024];
    unsigned size;
    fscanf(destination, "%s ", buf);
    fscanf(destination, "%d", &size);
    while(fread(buf, sizefo(char), 1024, destination)){
        fprintf(stdout, "%s", buf);
    }
}

int zlib_decompress(FILE *objectFile, FILE *destFile){
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
    if(ret != Z_OK)
        return ret;
    /* Decompress until the end of the file*/
    do
    {
        strm.avail_in = fread(in, sizeof(char), CHUNK, objectFile);
        if (ferror(objectFile)){
            (void)deflateEnd(&strm);
            return Z_ERRNO;
        }
        if(strm.avail_in == 0) break;
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
                ret =  Z_DATA_ERROR;
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            default:
                break;
            }
            have = CHUNK - strm.avail_out;
            if(fwrite(out, sizeof(char), have, destFile) != have || ferror(destFile)){
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);
        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);
}

int main(int argc, char *argv[]) {
    // Disable output buffering
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    if (argc < 2) {
        fprintf(stderr, "Usage: ./your_program.sh <command> [<args>]\n");
        return 1;
    }
    
    const char *command = argv[1];
    
    if (strcmp(command, "init") == 0) {
        // You can use print statements as follows for debugging, they'll be visible when running tests.
        fprintf(stderr, "Logs from your program will appear here!\n");

        // TODO: Uncomment the code below to pass the first stage
        
        if (mkdir(".git", 0755) == -1 || 
            mkdir(".git/objects", 0755) == -1 || 
            mkdir(".git/refs", 0755) == -1) {
            fprintf(stderr, "Failed to create directories: %s\n", strerror(errno));
            return 1;
        }
        
        FILE *headFile = fopen(".git/HEAD", "w");
        if (headFile == NULL) {
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

        snprintf(dir, sizeof(dir), ".git/objects/%.2s/%s", argv[3], argv[3]+2);
       
        FILE *objectFile = fopen(dir, "rb");
        strcat(dir, "TMP");
        FILE *destFile = fopen(dir, "wb+"); // must delet later
        cat_file(objectFile, destFile);
    }
     
    else {
        fprintf(stderr, "Unknown command %s\n", command);
        return 1;
    }
    
    return 0;
}