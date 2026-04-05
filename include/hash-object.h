#if !defined(HASH_OBJECT_H)
#define HASH_OBJECT_H

#define SHA1_SIZE 20
#include <stdio.h>

unsigned char* hash_object(const char* msg, long msgLen);
int zlib_compress(const char *content, unsigned long contentSize, FILE *dest);


#endif // HASH_OBJECT_H
