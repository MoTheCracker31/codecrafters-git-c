#if !defined(WRITE_TREE_H)
#define WRITE_TREE_H

typedef struct indexEntryFixed
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
};

int write_tree(FILE *index);

#endif // WRITE_TREE_H
