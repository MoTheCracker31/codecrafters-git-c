#include "commit-tree.h"
#include "hash-object.h"
#include "platform.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>

/* Author/committer identity used when the matching environment variables are
   unset. Real git pulls these from config; we keep simple fallbacks. */
#define DEFAULT_NAME "Tester"
#define DEFAULT_EMAIL "tester@testing.com"

/* Hash `content` (already including its "commit <size>\0" header) as a git
   object, store it zlib-compressed under .git/objects/, and return its 40-char
   hex SHA (plus NUL) in out_hex. Returns 0 on success, -1 on failure. */
static int write_object(const char *content, size_t len, char out_hex[SHA1_SIZE * 2 + 1])
{
    unsigned char *sha = hash_object(content, (long)len);
    if (!sha)
        return -1;

    char hex[SHA1_SIZE * 2 + 1];
    for (int i = 0; i < SHA1_SIZE; i++)
        sprintf(&hex[i * 2], "%02x", sha[i]);
    hex[SHA1_SIZE * 2] = '\0';
    free(sha);

    char dir[128], path[192];
    snprintf(dir, sizeof(dir), ".git%cobjects%c%.2s", PATH_SEP_CHAR, PATH_SEP_CHAR, hex);
    snprintf(path, sizeof(path), "%s%c%s", dir, PATH_SEP_CHAR, hex + 2);

    if (make_dir(dir) == -1 && errno != EEXIST)
    {
        fprintf(stderr, "commit-tree: failed to create object directory: %s\n", strerror(errno));
        return -1;
    }

    FILE *destFile = fopen(path, "wb");
    if (destFile == NULL)
    {
        fprintf(stderr, "commit-tree: failed to create object file: %s\n", strerror(errno));
        return -1;
    }
    zlib_compress(content, len, destFile);
    fclose(destFile);

    memcpy(out_hex, hex, SHA1_SIZE * 2 + 1);
    return 0;
}

/* Return 1 if `sha` is exactly 40 lowercase-or-uppercase hex digits, else 0. */
static int is_hex_sha(const char *sha)
{
    if (!sha || strlen(sha) != 40)
        return 0;
    for (int i = 0; i < 40; i++)
        if (!isxdigit((unsigned char)sha[i]))
            return 0;
    return 1;
}

/* Return 1 if a loose object with hex name `sha` exists under .git/objects/. */
static int object_exists(const char *sha)
{
    char path[192];
    snprintf(path, sizeof(path), ".git%cobjects%c%.2s%c%s",
             PATH_SEP_CHAR, PATH_SEP_CHAR, sha, PATH_SEP_CHAR, sha + 2);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

/* Format the current local time as a git identity timestamp: the seconds since
   the epoch followed by the local UTC offset, e.g. "1700000000 +0200". */
static void format_time(char *out, size_t out_size)
{
    time_t now = time(NULL);
    struct tm local = *localtime(&now);
    struct tm utc = *gmtime(&now);

    /* Offset of local time from UTC, in minutes. */
    int offMinutes = (int)((mktime(&local) - mktime(&utc)) / 60);
    char sign = offMinutes < 0 ? '-' : '+';
    if (offMinutes < 0)
        offMinutes = -offMinutes;

    snprintf(out, out_size, "%lld %c%02d%02d",
             (long long)now, sign, offMinutes / 60, offMinutes % 60);
}

int commit_tree(const char *tree_sha, const char *const *parent_shas,
                int parent_count, const char *message)
{
    if (!is_hex_sha(tree_sha))
    {
        fprintf(stderr, "commit-tree: a 40-char hex tree SHA is required\n");
        return 1;
    }
    if (!object_exists(tree_sha))
    {
        fprintf(stderr, "commit-tree: not a valid object name %s\n", tree_sha);
        return 1;
    }
    for (int i = 0; i < parent_count; i++)
    {
        if (!is_hex_sha(parent_shas[i]))
        {
            fprintf(stderr, "commit-tree: parent must be a 40-char hex SHA\n");
            return 1;
        }
    }
    if (!message)
        message = "";

    const char *name = getenv("GIT_AUTHOR_NAME");
    if (!name)
        name = DEFAULT_NAME;
    const char *email = getenv("GIT_AUTHOR_EMAIL");
    if (!email)
        email = DEFAULT_EMAIL;

    char when[64];
    format_time(when, sizeof(when));

    /* Assemble the commit body:
         tree <tree_sha>\n
         [parent <parent_sha>\n]...
         author <name> <<email>> <when>\n
         committer <name> <<email>> <when>\n
         \n
         [<message>\n]                                                        */
    size_t msgLen = strlen(message);
    size_t bodyCap = strlen(tree_sha) + parent_count * 48 +
                     2 * (strlen(name) + strlen(email) + strlen(when)) +
                     msgLen + 128;
    char *body = malloc(bodyCap);
    if (!body)
    {
        fprintf(stderr, "commit-tree: out of memory\n");
        return 1;
    }

    int bodyLen = snprintf(body, bodyCap, "tree %s\n", tree_sha);
    for (int i = 0; i < parent_count; i++)
        bodyLen += snprintf(body + bodyLen, bodyCap - bodyLen,
                            "parent %s\n", parent_shas[i]);
    bodyLen += snprintf(body + bodyLen, bodyCap - bodyLen,
                        "author %s <%s> %s\n", name, email, when);
    bodyLen += snprintf(body + bodyLen, bodyCap - bodyLen,
                        "committer %s <%s> %s\n", name, email, when);
    /* Blank line separates the headers from the message. Like real git, only
       emit the message (and its trailing newline) when it is non-empty, and
       avoid doubling a newline the caller already supplied. */
    bodyLen += snprintf(body + bodyLen, bodyCap - bodyLen, "\n");
    if (msgLen > 0)
        bodyLen += snprintf(body + bodyLen, bodyCap - bodyLen,
                            message[msgLen - 1] == '\n' ? "%s" : "%s\n", message);

    /* Prepend the "commit <size>\0" object header. */
    char header[32];
    int headerLen = sprintf(header, "commit %d", bodyLen) + 1; /* +1 for NUL */
    size_t totalSize = headerLen + bodyLen;

    char *obj = malloc(totalSize);
    if (!obj)
    {
        fprintf(stderr, "commit-tree: out of memory\n");
        free(body);
        return 1;
    }
    memcpy(obj, header, headerLen);
    memcpy(obj + headerLen, body, bodyLen);
    free(body);

    char hex[SHA1_SIZE * 2 + 1];
    int rc = write_object(obj, totalSize, hex);
    free(obj);
    if (rc != 0)
        return 1;

    printf("%s\n", hex);
    return 0;
}
