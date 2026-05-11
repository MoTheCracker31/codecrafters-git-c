#include "write-tree.h"
#include "hash-object.h"
#include <stdlib.h>
#include <string.h>

int read_git_index_file(FILE *index)
{
    unsigned int indexMagicNumber;
    unsigned int versionNumber;
    unsigned int numberOfEnteries;
    unsigned int readBytes, writtenBytes;
    unsigned int osFilePathLimit = 255;
    short fileNameLen;

    readBytes = fread(&indexMagicNumber, sizeof(unsigned int), 1, index);
    readBytes += fread(&versionNumber, sizeof(unsigned int), 1, index);
    readBytes += fread(&numberOfEnteries, sizeof(unsigned int), 1, index);
    if (readBytes != 3 * sizeof(unsigned int))
    {
        fprintf(index, "Error reading the metadata of index file");
        return 1; // return actaual error code.
    }

    indexEntryFixed entry[numberOfEnteries];
    unsigned short boundryBytesLimit = 8;
    char fileName[numberOfEnteries][osFilePathLimit];
    char *treeContent = (char *)malloc(sizeof(char) * 1024 * numberOfEnteries);
    memset(treeContent, 0, sizeof(char) * 1024 * numberOfEnteries);
    writtenBytes = sprintf(treeContent, "tree %.06u", (unsigned int)((6 + 4 + 1 + 20) * numberOfEnteries));
    if (writtenBytes < 0)
    {
        fprintf(stderr, "Error writting the header of tree object file: %s\n", strerror(errno));
    }

    for (int i = 0; i < numberOfEnteries; i++)
    {
        fseek(index, 6 * sizeof(unsigned int), SEEK_CUR);
        readBytes = 6 * sizeof(unsigned char);
        readBytes += fread(&entry[i].mode, sizeof(unsigned int), 1, index);
        fseek(index, 3 * sizeof(unsigned int), SEEK_CUR);
        readBytes += 3 * sizeof(unsigned int);
        if (fread(entry[i].sha, sizeof(unsigned char), SHA1_SIZE, index) != SHA1_SIZE)
        {
            fprintf(stderr, "fatal: error reading sha of the %dth entry\n", i);
            return 1;
        }
        readBytes += SHA1_SIZE;

        if (fread(&entry[i].flag, sizeof(short), 1, index) != sizeof(short))
        {
            fprintf(stderr, "fatal: error reading file name length of the %dth entry\n");
            return 1;
        }
        readBytes += sizeof(short);
        readBytes += fscanf(index, "%s\0", fileName[i]);
        fileNameLen = entry[i].flag & 0x0FFF;
        fseek(index, -fileNameLen, SEEK_CUR);
        fread(fileName[i], sizeof(char), fileNameLen + 1, index);
        fseek(index, boundryBytesLimit - (readBytes % boundryBytesLimit), SEEK_CUR);
    }
}