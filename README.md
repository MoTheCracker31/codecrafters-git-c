# Simplified Git

A tiny version-control tool, written from scratch in C, whose whole purpose is
to **show how Git actually works** — in the simplest way possible.

Git can feel like magic: you type `git add` and `git commit`, and your history
appears out of nowhere. This project pulls that magic apart. It reimplements
the core plumbing of Git so you can watch, step by step, how a few simple ideas
— hashing, compression, and a handful of object types — add up to a complete
version-control system.

Everything it produces lives in a real `.git` directory and is byte-for-byte
compatible with Git itself, so you can cross-check every step against the real
thing.

## How Git works, in three ideas

Real Git isn't as mysterious as it looks. Underneath, it's built on three small
ideas, and this project implements each one so you can see it directly:

1. **Content is addressed by its hash.** Every file, directory, and commit is
   stored under the SHA-1 of its contents. Same content → same name; change one
   byte and you get a brand-new object. This is what makes Git tamper-evident
   and deduplicated for free.
2. **Everything is one of a few object types.** A *blob* is a file's contents.
   A *tree* is a directory listing (names + modes + the hashes of its blobs and
   sub-trees). A *commit* points at one tree (a full snapshot) plus its parent
   commit(s) and a message. That's the entire data model.
3. **Objects are just compressed files on disk.** Each object is zlib-compressed
   and saved under `.git/objects/`. There's no database — your history is a pile
   of small files that reference each other by hash.

A commit points to a tree, a tree points to blobs and other trees, and each
commit points to its parent. Follow those pointers and you've walked the entire
history of a project.

## Seeing it for yourself

The commands below let you build up a commit by hand — exactly the steps Git
runs for you behind `git commit`. They use `mygit`, a shorthand for this
program; set it up with `alias mygit=/path/to/your/repo/your_program.sh` (see
[Building](#building) below):

```sh
mygit init                              # create an empty .git directory

echo "hello world" > hello.txt
mygit hash-object -w hello.txt          # store the file as a blob -> prints its hash
mygit cat-file -p <blob-hash>           # read the blob's contents back

mygit write-tree                        # snapshot the staged files into a tree -> prints its hash
mygit ls-tree <tree-hash>               # list what's inside that tree

mygit commit-tree <tree-hash> -m "first commit"   # wrap the tree in a commit
```

Because the objects are real Git objects, you can verify each one with actual
Git: `git cat-file -p <hash>`, `git ls-tree <hash>`, or `git log <commit>` all
work on the objects this program writes.

### Commands implemented

| Command | What it shows |
| --- | --- |
| `init` | The `.git` directory is just a few folders and a `HEAD` file. |
| `hash-object -w <file>` | A file becomes a content-addressed *blob*. |
| `cat-file -p <sha>` | Any object can be read back from its hash. |
| `write-tree` | Staged files snapshot into a *tree* (Git's idea of a directory). |
| `ls-tree <sha>` | A tree is just a list of names, modes, and hashes. |
| `commit-tree <tree> [-p <parent>]... [-m <msg>]` | A *commit* ties a tree to a message and a parent. |

## Background

This project started from the CodeCrafters
["Build Your Own Git"](https://codecrafters.io/challenges/git) challenge, which
got me through the first stage (initializing a repository).
**Everything past that point I designed and built on my own** — choosing which
plumbing to implement, working out Git's on-disk formats, and writing the test
suite.

## Building

Requires `cmake`, a C compiler, and the OpenSSL and zlib development libraries
(resolved here through [vcpkg](https://vcpkg.io/)).

```sh
./your_program.sh init        # builds via CMake, then runs the program
```

`your_program.sh` compiles the project into `build/` and forwards its arguments
to the binary. To build only:

```sh
cmake -G Ninja -B build -S .
cmake --build ./build         # produces build/git(.exe)
```

`your_program.sh` operates on the `.git` folder in the current directory, so run
it inside a throwaway folder to avoid touching this repo's own `.git`. A shell
alias keeps it short:

```sh
alias mygit=/path/to/your/repo/your_program.sh
mkdir -p /tmp/testing && cd /tmp/testing
mygit init
```

## Testing

The suite in `test/` is a black-box pytest suite that uses **real Git as the
testing oracle**: each test runs both this program and `git` over the same
inputs and asserts they agree. Tests are organized by equivalence classes, with
boundary-value cases for input validation.

Because commit objects embed a non-deterministic timestamp, the `commit-tree`
tests can't compare SHAs directly. They use a differential + round-trip oracle
instead: `git cat-file -t` confirms real Git accepts each object this program
stores, and `git cat-file -p` (with the timestamp normalized away) must match
the commit `git commit-tree` produces from identical inputs.

```sh
python -m pytest test/ -v
```
