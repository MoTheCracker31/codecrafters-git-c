#include <string.h>
#include "ls-tree.h"
#include "hash-object.h"

void ls_tree(FILE *dest)
{
    unsigned int cnt;
    char objectFileType[10];
    unsigned long objectFileSize;
    unsigned char shaHash[SHA1_SIZE * 2 + 1];
    unsigned char rawHash[SHA1_SIZE];
    char type[5];
    char fileName[1024];
    int c;
    unsigned long mode;
    rewind(dest);
    // Skip the first line in the file
    fscanf(dest, "%s %lu\0\n", objectFileType, &objectFileSize);

    while (true)
    {
        cnt = fscanf(dest, "%lu", &mode); // doesn't work correctly.
        if (cnt == EOF)
            break; // eof
        else if (cnt != 1)
        {
            fprintf(stderr, "Faild to read the file mode\n");
            break;
        }

        switch (mode)
        {
        case 100644:
            memcpy(type, "blob", sizeof("blob"));
            break;
        case 100755:
            memcpy(type, "exec", sizeof("exec"));
            break;
        case 120000:
            memcpy(type, "link", sizeof("link"));
            break;
        case 040000:
            memccpy(type, "tree", sizeof("tree"));
            break;
        default:
            memcpy(type, "N/A", sizeof("N/A"));
            break;
        }

        fseek(dest, 1, SEEK_CUR); // skip white space

        cnt = 0;
        while ((c = fgetc(dest)) != EOF && c != '\0')
        {
            if (cnt < sizeof(fileName) - 1)
            {
                fileName[cnt++] = (char)c;
            }
        }
        fileName[cnt] = '\0';

        if (fread(rawHash, sizeof(unsigned char), sizeof(rawHash), dest) != SHA1_SIZE)
        {
            fprintf(stderr, "Failed to read raw SHA has\n");
            break;
        }

        // fseek(dest, 1, SEEK_CUR); // skip the new line

        for (cnt = 0; cnt < SHA1_SIZE; cnt++)
        {
            sprintf(&shaHash[cnt * 2], "%.02x", rawHash[cnt]);
        }
        shaHash[SHA1_SIZE * 2 + 1] = '\0';
        fprintf(stdout, "%.06d %s %s %s\n", mode, type, shaHash, fileName);
        cnt = 0;
    }
}