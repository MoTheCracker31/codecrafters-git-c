#if !defined(PLATFROM_H)
#define PLATFROM_H

#include <stddef.h>

#if defined(_WIN32)
    #define PATH_SEP_CHAR '\\'
    #define PATH_SEP_STR "\\"
#else
    #define PATH_SEP_CHAR '/'
    #define PATH_SEP_STR "/"

#endif // _WIN32

int make_dir(const char* path);
void join_path(char *out, size_t out_size, const char *a, const char *b);


#endif // PLATFROM_H
