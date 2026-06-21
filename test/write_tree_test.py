"""
Black-box equivalence class tests for `git write-tree`.

Real git is the test oracle. Each test stages a specific combination of file
types into a fresh git repo, runs both implementations, and asserts that the
printed SHA and exit code match.

Equivalence classes (based on what the root tree object will contain):

  EC1a  Only blobs    — single regular file (100644)
  EC1b  Only blobs    — multiple regular files (100644)
  EC2a  Only execs    — single executable (100755)          [skip on Windows]
  EC2b  Only execs    — multiple executables (100755)       [skip on Windows]
  EC3a  Only trees    — single subdirectory
  EC3b  Only trees    — nested subdirectories (tree -> tree -> blob)
  EC4   Blob + Tree   — root file(s) alongside a subdirectory
  EC5   Exec + Tree   — executable(s) alongside a subdirectory [skip on Windows]
  EC6   Blob + Exec   — regular file(s) alongside executable(s) [skip on Windows]
  EC7   All three     — blob + executable + subtree in one tree [skip on Windows]
  EC8   Empty index   — nothing staged; must produce the empty-tree SHA

Note: EC3*, EC4, EC5, EC7 require the implementation to recursively build
sub-tree objects from nested index paths.  If that feature is not yet
implemented those tests will fail and document what is still missing.
"""

import os
import stat
import subprocess
import sys

import pytest


# ---------------------------------------------------------------------------
# Helpers / fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def git_repo(tmp_path):
    subprocess.run(["git", "init"], cwd=tmp_path, check=True,
                   capture_output=True)
    subprocess.run(["git", "config", "user.name", "Tester"], cwd=tmp_path,
                   check=True, capture_output=True)
    subprocess.run(["git", "config", "user.email", "test@test.com"],
                   cwd=tmp_path, check=True, capture_output=True)
    yield tmp_path


def my_git(args: list[str], cwd):
    return subprocess.run(
        ["./build/git"] + args,
        capture_output=True, text=True, cwd=cwd,
    )


def real_git(args: list[str], cwd):
    return subprocess.run(
        ["git"] + args,
        capture_output=True, text=True, cwd=cwd,
    )


def assert_write_tree_matches(git_repo):
    """Core assertion: SHA on stdout and exit code must match real git."""
    my   = my_git(["write-tree"], git_repo)
    real = real_git(["write-tree"], git_repo)
    assert my.returncode == real.returncode, (
        f"exit code mismatch: got {my.returncode}, expected {real.returncode}\n"
        f"stderr: {my.stderr}"
    )
    assert my.stdout.strip() == real.stdout.strip(), (
        f"SHA mismatch:\n  got:      {my.stdout.strip()}\n"
        f"  expected: {real.stdout.strip()}"
    )


# ---------------------------------------------------------------------------
# EC1 — Only blobs (regular files, mode 100644)
# ---------------------------------------------------------------------------

def test_write_tree_single_blob(git_repo):
    """EC1a: index contains exactly one regular file."""
    (git_repo / "hello.txt").write_text("hello world\n")
    subprocess.run(["git", "add", "hello.txt"], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


def test_write_tree_multiple_blobs(git_repo):
    """EC1b: index contains several regular files at the root level."""
    for i in range(5):
        (git_repo / f"file{i}.txt").write_text(f"content of file {i}\n")
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


# ---------------------------------------------------------------------------
# EC2 — Only executables (mode 100755)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(sys.platform == "win32",
                    reason="Windows does not honour the executable bit")
def test_write_tree_single_executable(git_repo):
    """EC2a: index contains exactly one executable file."""
    exe = git_repo / "script.sh"
    exe.write_text("#!/bin/sh\necho hello\n")
    exe.chmod(stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP |
              stat.S_IROTH | stat.S_IXOTH)
    subprocess.run(["git", "add", "script.sh"], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


@pytest.mark.skipif(sys.platform == "win32",
                    reason="Windows does not honour the executable bit")
def test_write_tree_multiple_executables(git_repo):
    """EC2b: index contains several executable files at the root level."""
    for name in ["run.sh", "build.sh", "test.sh"]:
        exe = git_repo / name
        exe.write_text(f"#!/bin/sh\necho {name}\n")
        exe.chmod(stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP)
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


# ---------------------------------------------------------------------------
# EC3 — Only trees (subdirectories; root tree has no blob/exec at top level)
# ---------------------------------------------------------------------------

def test_write_tree_single_subtree(git_repo):
    """EC3a: all staged files live inside a single subdirectory."""
    (git_repo / "src").mkdir()
    (git_repo / "src" / "main.c").write_text("int main() { return 0; }\n")
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


def test_write_tree_nested_subtrees(git_repo):
    """EC3b: files live two levels deep, producing a tree -> tree -> blob chain."""
    (git_repo / "a" / "b").mkdir(parents=True)
    (git_repo / "a" / "file.txt").write_text("in a\n")
    (git_repo / "a" / "b" / "file.txt").write_text("in a/b\n")
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


# ---------------------------------------------------------------------------
# EC4 — Blob + Tree
# ---------------------------------------------------------------------------

def test_write_tree_blobs_and_subtree(git_repo):
    """EC4: root contains both regular files and a subdirectory."""
    (git_repo / "README.md").write_text("# My Project\n")
    (git_repo / "LICENSE").write_text("MIT\n")
    (git_repo / "src").mkdir()
    (git_repo / "src" / "main.c").write_text("int main() {}\n")
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


# ---------------------------------------------------------------------------
# EC5 — Executable + Tree
# ---------------------------------------------------------------------------

@pytest.mark.skipif(sys.platform == "win32",
                    reason="Windows does not honour the executable bit")
def test_write_tree_executables_and_subtree(git_repo):
    """EC5: root contains executable files and a subdirectory."""
    exe = git_repo / "build.sh"
    exe.write_text("#!/bin/sh\nmake\n")
    exe.chmod(stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP)
    (git_repo / "src").mkdir()
    (git_repo / "src" / "main.c").write_text("int main() {}\n")
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


# ---------------------------------------------------------------------------
# EC6 — Blob + Executable
# ---------------------------------------------------------------------------

@pytest.mark.skipif(sys.platform == "win32",
                    reason="Windows does not honour the executable bit")
def test_write_tree_blobs_and_executables(git_repo):
    """EC6: root contains both regular files and executable files."""
    (git_repo / "README.md").write_text("# Readme\n")
    (git_repo / "notes.txt").write_text("some notes\n")
    exe = git_repo / "run.sh"
    exe.write_text("#!/bin/sh\necho run\n")
    exe.chmod(stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP)
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


# ---------------------------------------------------------------------------
# EC7 — All three types (blob + executable + tree)
# ---------------------------------------------------------------------------

@pytest.mark.skipif(sys.platform == "win32",
                    reason="Windows does not honour the executable bit")
def test_write_tree_all_three_types(git_repo):
    """EC7: root tree contains entries of all three kinds simultaneously."""
    (git_repo / "README.md").write_text("# Project\n")
    exe = git_repo / "build.sh"
    exe.write_text("#!/bin/sh\nmake\n")
    exe.chmod(stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP)
    (git_repo / "src").mkdir()
    (git_repo / "src" / "main.c").write_text("int main() {}\n")
    subprocess.run(["git", "add", "."], cwd=git_repo, check=True,
                   capture_output=True)
    assert_write_tree_matches(git_repo)


# ---------------------------------------------------------------------------
# EC8 — Empty index
# ---------------------------------------------------------------------------

def test_write_tree_empty_index(git_repo):
    """EC8: nothing has been staged; must print the well-known empty-tree SHA."""
    assert_write_tree_matches(git_repo)
