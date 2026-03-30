#include "hash-object.h"
#include "openssl/evp.h"
#include <string.h>


unsigned char* hash_object(const char* msg){


    unsigned char *hashedObj = (unsigned char *)malloc(SHA1_SIZE * sizeof(unsigned char));
    if (hashedObj == NULL) return NULL;
    
    EVP_MD_CTX *mdctx = EVP_MD_CTX_create();
    if(!mdctx) return NULL;
    int md_len;
    if(EVP_DigestInit_ex(mdctx, EVP_sha1(), NULL) == 0 || EVP_DigestUpdate(mdctx, msg, strlen(msg)) == 0 || EVP_DigestFinal(mdctx, hashedObj, &md_len) == 0){
        EVP_MD_CTX_free(mdctx);
        free(hashedObj);    
        return NULL;
    }

    EVP_MD_CTX_free(mdctx);

    return hashedObj;
}