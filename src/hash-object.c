#include "hash-object.h"
#include "openssl/evp.h"
#include <string.h>
#include <zlib.h>
#include <assert.h>


unsigned char* hash_object(const char* msg, long msgLen){

    unsigned char *hashedObj = (unsigned char *)malloc(SHA1_SIZE * sizeof(unsigned char));
    memset(hashedObj, 0, SHA1_SIZE * sizeof(unsigned char));
    if (hashedObj == NULL) return NULL;
    
    EVP_MD_CTX *mdctx = EVP_MD_CTX_create();
    if(!mdctx) return NULL;
    int md_len;
    if(EVP_DigestInit_ex(mdctx, EVP_sha1(), NULL) == 0 || EVP_DigestUpdate(mdctx, msg, msgLen) == 0 || EVP_DigestFinal(mdctx, hashedObj, &md_len) == 0){
        EVP_MD_CTX_free(mdctx);
        free(hashedObj);    
        return NULL;
    }

    EVP_MD_CTX_free(mdctx);

    return hashedObj;
}

int zlib_compress(const char *content, unsigned long contentSize, FILE *dest){
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    unsigned long total_out = 0;
    unsigned long bytes_out;
    unsigned long sizeBound = compressBound(contentSize);
    char *out = (char *) malloc(sizeBound);
    if(out == NULL){
        fprintf(stderr, "Failed to allocate memory to output compressed data: %s", strerror(errno));
        return 1;
    }
    memset(out, 0, sizeBound);

    strm.zalloc = Z_NULL;
    strm.opaque = Z_NULL;
    strm.zfree = Z_NULL;
    strm.next_in = Z_NULL;
    strm.avail_in = 0;

    int ret = deflateInit(&strm, Z_DEFAULT_COMPRESSION);
    if(ret != Z_OK){
        fprintf(stderr, "Zlib compression of content has failed");
        free(out);
        return ret;
    }

    do
    {
        strm.avail_in = contentSize - strm.total_in;
        strm.next_in = content + strm.total_in;
        strm.avail_out = sizeBound;
        strm.next_out = out;
        ret = deflate(&strm, Z_FINISH);
        if(ret == Z_STREAM_ERROR){
            fprintf(stderr, "Compression failed with return value: %i", ret);
            free(out);
            deflateEnd(&strm);
            return ret;
        }
        bytes_out = strm.total_out - total_out; 
        total_out = strm.total_out;
        if(fwrite(out, sizeof(char), bytes_out, dest) != bytes_out || ferror(dest)){
            fprintf(stderr, "Failed writing compressed content");
            deflateEnd(&strm);
            free(out);
            return Z_ERRNO;
        }
    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    free(out);
    return Z_OK;
}