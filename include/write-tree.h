#if !defined(WRITE_TREE_H)
#define WRITE_TREE_H

#include <stdio.h>

typedef struct
{
    unsigned int ctime, ctime_ns;
    unsigned int mtime, mtime_ns;
    unsigned int dev_id;
    unsigned int ino;
    unsigned int mode;
    unsigned int uid;
    unsigned int gid;
    unsigned int file_size;
    unsigned char sha[20];
    unsigned short flag;
} indexEntryFixed;

int read_git_index_file(FILE *index);

#endif // WRITE_TREE_H
