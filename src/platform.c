#include "platform.h"
#include <stdio.h>
#include <errno.h>

#if defined(_WIN32)
    #include<direct.h>
    int make_dir(const char *path){
        int rc = _mkdir(path);
        if(rc == -1 && errno == EEXIST) return 0;
        return rc;
    }
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    int make_dir(const char *path){
        int rc = mkdir(path, 0755);
        if(rc == -1 && errno == EEXIST) return 0;
        return rc;
    }

#endif // _WIN32

void join_path(char *out, size_t out_size, const char *a, const char *b){
    snprintf(out, out_size, "%s%c%s",a ,PATH_SEP_CHAR, b);
}
