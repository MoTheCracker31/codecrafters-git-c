#if !defined(COMMIT_TREE_H)
#define COMMIT_TREE_H

#include <stdio.h>

/* Build a commit object pointing at `tree_sha`, store it zlib-compressed under
   .git/objects/, and print its 40-char hex SHA to stdout.

   `parent_shas` points at `parent_count` parent-commit hex SHAs; one "parent"
   line is emitted per entry (zero parents => a root commit). `message` is the
   commit message, or NULL for an empty message.

   Returns 0 on success, non-zero on failure. */
int commit_tree(const char *tree_sha, const char *const *parent_shas,
                int parent_count, const char *message);

#endif // COMMIT_TREE_H
