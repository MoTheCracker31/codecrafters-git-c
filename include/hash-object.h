#if !defined(HASH_OBJECT_H)
#define HASH_OBJECT_H

#define SHA1_SIZE 20
unsigned char* hash_object(const char* msg, long msgLen);
void zlib_compress(char *content);


#endif // HASH_OBJECT_H
