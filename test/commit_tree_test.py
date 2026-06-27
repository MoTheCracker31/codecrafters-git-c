"""
Black-box test suite for the `commit-tree` command (src/commit-tree.c).

Real git is the test oracle. Each test drives our binary the same way a user
would (`git commit-tree <tree> [-p <parent>] [-m <msg>]`) and checks the result
against real git.

--------------------------------------------------------------------------
Why not a plain SHA-equality oracle (like write_tree_test.py uses)?
--------------------------------------------------------------------------
A commit object embeds a timestamp and a UTC offset (the `author`/`committer`
lines). Our implementation reads the clock with time(NULL) at the moment it
runs and ignores GIT_AUTHOR_DATE / GIT_COMMITTER_DATE, so its timestamp can
never be pinned to match real git's. The SHA is a hash of the whole object, so
two commits made a second apart -- or in different DST offsets -- get different
SHAs even when everything we control is identical. A byte/SHA-equality oracle
is therefore infeasible here.

--------------------------------------------------------------------------
Test design
--------------------------------------------------------------------------
Input selection: EQUIVALENCE PARTITIONING for the three parameters (tree SHA,
parent SHA, message), plus BOUNDARY VALUE ANALYSIS on the one numeric boundary
the code actually checks -- the 40-character SHA length (39 / 40 / 41).

Oracle (per successful run) -- a DIFFERENTIAL + ROUND-TRIP oracle:
  1. Round-trip / integrity: real git must accept the object we wrote.
     `git cat-file -t <sha>` == "commit" proves our SHA, object header and zlib
     stream are all correct (git recomputes the hash when it reads the object).
  2. Differential structure: `git cat-file -p` of our commit, with the volatile
     "<timestamp> <offset>" tail stripped off the author/committer lines, must
     equal the same view of a commit real git's own `commit-tree` produces from
     identical inputs. This compares every field we can control (tree, parent,
     identity, blank-line separator, message) while ignoring only the clock.

For invalid input we assert both implementations reject it (non-zero exit).

--------------------------------------------------------------------------
Divergences from real git
--------------------------------------------------------------------------
Four divergences were found while building this suite. D1-D3 have since been
fixed and now have ordinary passing tests that assert the real-git behaviour:

  D1  Empty / omitted message -> no longer appends an extra trailing blank line.
  D2  Multiple `-p` parents   -> all parents are now recorded (merge commit).
  D3  Non-hex / non-existent 40-char tree -> now rejected, as real git rejects.

D4 remains open and is documented as a strict-xfail test: it asserts the
*correct* (real-git) behaviour, so it currently fails and is marked
xfail(strict=True). When the implementation is fixed it will xpass, which
pytest reports as a failure -- a reminder to drop the marker.

  D4  Distinct committer identity -> we reuse GIT_AUTHOR_* for the committer
                                     and ignore GIT_COMMITTER_*.
"""

import re
import subprocess
from pathlib import Path

import pytest

# Absolute path to our binary, resolved from this file's location so the suite
# works no matter what directory pytest is launched from.
BIN = Path(__file__).resolve().parent.parent / "build" / "git.exe"

# A fixed identity shared by author and committer. We set the COMMITTER vars
# too so real git's committer line matches ours (ours reuses the author
# identity for the committer -- see divergence D4).
ENV_IDENTITY = {
    "GIT_AUTHOR_NAME": "Tester",
    "GIT_AUTHOR_EMAIL": "tester@testing.com",
    "GIT_COMMITTER_NAME": "Tester",
    "GIT_COMMITTER_EMAIL": "tester@testing.com",
}


# ---------------------------------------------------------------------------
# Fixtures / helpers
# ---------------------------------------------------------------------------

@pytest.fixture
def repo(tmp_path, monkeypatch):
    """A fresh git repo with a deterministic identity in the environment."""
    for k, v in ENV_IDENTITY.items():
        monkeypatch.setenv(k, v)
    subprocess.run(["git", "init"], cwd=tmp_path, check=True, capture_output=True)
    subprocess.run(["git", "config", "user.name", "Tester"], cwd=tmp_path,
                   check=True, capture_output=True)
    subprocess.run(["git", "config", "user.email", "tester@testing.com"],
                   cwd=tmp_path, check=True, capture_output=True)
    return tmp_path


def mine(args, cwd):
    return subprocess.run([str(BIN), "commit-tree"] + args, cwd=cwd,
                          capture_output=True, text=True)


def real(args, cwd):
    return subprocess.run(["git"] + args, cwd=cwd,
                          capture_output=True, text=True)


def make_tree(repo, files=None):
    """Stage `files` ({name: content}) and return the resulting tree SHA."""
    files = files or {"a.txt": "hello\n"}
    for name, content in files.items():
        p = repo / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content)
    subprocess.run(["git", "add", "."], cwd=repo, check=True, capture_output=True)
    out = subprocess.run(["git", "write-tree"], cwd=repo, check=True,
                         capture_output=True, text=True)
    return out.stdout.strip()


def make_commit(repo, tree, message="base"):
    """Create a real commit object and return its SHA (used as a parent)."""
    out = real(["commit-tree", tree, "-m", message], repo)
    assert out.returncode == 0, out.stderr
    return out.stdout.strip()


_TS_TAIL = re.compile(r"^(author|committer) (.*>) \d+ [+-]\d{4}$")


def show_normalized(repo, sha):
    """`git cat-file -p <sha>` with the volatile timestamp/offset removed from
    the author/committer lines. Uses real git as the reader (oracle)."""
    out = real(["cat-file", "-p", sha], repo)
    assert out.returncode == 0, f"git could not read object {sha}: {out.stderr}"
    lines = []
    for line in out.stdout.split("\n"):
        m = _TS_TAIL.match(line)
        lines.append(f"{m.group(1)} {m.group(2)}" if m else line)
    return "\n".join(lines)


def assert_object_is_valid_commit(repo, sha):
    """Round-trip oracle: our printed SHA is 40 hex and real git accepts the
    stored object as a commit (proves hashing/header/zlib are correct)."""
    assert re.fullmatch(r"[0-9a-f]{40}", sha), f"not a 40-hex SHA: {sha!r}"
    t = real(["cat-file", "-t", sha], repo)
    assert t.returncode == 0 and t.stdout.strip() == "commit", (
        f"real git did not accept object {sha} as a commit: "
        f"type={t.stdout.strip()!r} err={t.stderr!r}")


def assert_matches_real(repo, args, message):
    """Full oracle for a successful commit-tree run: build the commit with our
    binary and with real git from identical args, then assert integrity +
    structural equality (timestamp-normalized)."""
    ours = mine(args, repo)
    assert ours.returncode == 0, f"our commit-tree failed: {ours.stderr}"
    our_sha = ours.stdout.strip()
    assert_object_is_valid_commit(repo, our_sha)

    real_run = real(["commit-tree"] + args, repo)
    assert real_run.returncode == 0, f"real commit-tree failed: {real_run.stderr}"
    real_sha = real_run.stdout.strip()

    assert show_normalized(repo, our_sha) == show_normalized(repo, real_sha), (
        f"commit structure differs for message {message!r}\n"
        f"--- ours ---\n{show_normalized(repo, our_sha)}\n"
        f"--- real ---\n{show_normalized(repo, real_sha)}")


# ===========================================================================
# Message equivalence classes (valid tree, no parent)
# ===========================================================================

def test_single_line_message(repo):
    """EC-M1: a normal one-line message."""
    tree = make_tree(repo)
    assert_matches_real(repo, [tree, "-m", "first commit"], "first commit")


def test_multiline_message(repo):
    """EC-M2: a message spanning several lines."""
    tree = make_tree(repo)
    msg = "summary line\n\nbody paragraph\nsecond body line"
    assert_matches_real(repo, [tree, "-m", msg], msg)


def test_message_with_special_characters(repo):
    """EC-M3: punctuation / shell-significant characters pass through verbatim."""
    tree = make_tree(repo)
    msg = "fix: handle 'quotes', \"dquotes\" & <brackets> 100% (done)"
    assert_matches_real(repo, [tree, "-m", msg], msg)


# ===========================================================================
# Parent equivalence classes
# ===========================================================================

def test_root_commit_no_parent(repo):
    """EC-P1: no -p -> root commit, no `parent` line emitted."""
    tree = make_tree(repo)
    out = mine([tree, "-m", "root"], repo)
    assert out.returncode == 0
    body = show_normalized(repo, out.stdout.strip())
    assert "parent " not in body, f"unexpected parent line:\n{body}"
    assert_matches_real(repo, [tree, "-m", "root"], "root")


def test_commit_with_one_parent(repo):
    """EC-P2: a single valid -p parent -> exactly one `parent` line."""
    tree = make_tree(repo)
    parent = make_commit(repo, tree)
    out = mine([tree, "-p", parent, "-m", "child"], repo)
    assert out.returncode == 0
    body = show_normalized(repo, out.stdout.strip())
    assert f"parent {parent}" in body, f"missing parent line:\n{body}"
    assert_matches_real(repo, [tree, "-p", parent, "-m", "child"], "child")


def test_commit_chain(repo):
    """EC-C1: tree + parent + message together, two commits deep (realistic)."""
    tree = make_tree(repo)
    c1 = make_commit(repo, tree, "c1")
    out = mine([tree, "-p", c1, "-m", "c2"], repo)
    assert out.returncode == 0
    assert_object_is_valid_commit(repo, out.stdout.strip())
    assert_matches_real(repo, [tree, "-p", c1, "-m", "c2"], "c2")


# ===========================================================================
# Identity equivalence classes
# ===========================================================================

def test_identity_from_environment(repo):
    """EC-ID1: GIT_AUTHOR_NAME/EMAIL are honoured on the author line."""
    monkey_env = {"GIT_AUTHOR_NAME": "Alice Example",
                  "GIT_AUTHOR_EMAIL": "alice@example.com"}
    tree = make_tree(repo)
    out = subprocess.run([str(BIN), "commit-tree", tree, "-m", "hi"], cwd=repo,
                         capture_output=True, text=True,
                         env={**_os_environ(), **ENV_IDENTITY, **monkey_env,
                              "GIT_COMMITTER_NAME": "Alice Example",
                              "GIT_COMMITTER_EMAIL": "alice@example.com"})
    assert out.returncode == 0, out.stderr
    body = show_normalized(repo, out.stdout.strip())
    assert "author Alice Example <alice@example.com>" in body, body


def test_default_identity_when_env_unset(repo, monkeypatch):
    """EC-ID2: with no GIT_AUTHOR_* in the environment, the documented
    fallback identity (Tester <tester@testing.com>) is used."""
    for k in ("GIT_AUTHOR_NAME", "GIT_AUTHOR_EMAIL"):
        monkeypatch.delenv(k, raising=False)
    tree = make_tree(repo)
    out = mine([tree, "-m", "hi"], repo)
    assert out.returncode == 0, out.stderr
    body = show_normalized(repo, out.stdout.strip())
    assert "author Tester <tester@testing.com>" in body, body
    assert "committer Tester <tester@testing.com>" in body, body


# ===========================================================================
# Invalid input (boundary value analysis on the 40-char SHA length)
# ===========================================================================

@pytest.mark.parametrize("bad_tree", [
    "",                                            # empty
    "0" * 39,                                       # boundary: one short
    "0" * 41,                                       # boundary: one long
    "abc123",                                       # clearly too short
])
def test_invalid_tree_sha_length_is_rejected(repo, bad_tree):
    """EC-T2 + BVA: a tree SHA that is not 40 chars must be rejected. Both our
    binary and real git reject it (exit codes differ -- ours 1, git 128 -- so
    we assert only that both fail)."""
    ours = mine([bad_tree, "-m", "x"], repo)
    theirs = real(["commit-tree", bad_tree, "-m", "x"], repo)
    assert ours.returncode != 0, f"we accepted bad tree {bad_tree!r}"
    assert theirs.returncode != 0, f"real git accepted bad tree {bad_tree!r}"


def test_invalid_parent_sha_length_is_rejected(repo):
    """EC-P3: a parent SHA that is not 40 chars must be rejected."""
    tree = make_tree(repo)
    ours = mine([tree, "-p", "0" * 39, "-m", "x"], repo)
    assert ours.returncode != 0, "we accepted a 39-char parent SHA"


def test_missing_tree_argument_is_rejected(repo):
    """EC-T0: no tree argument at all -> usage error, non-zero exit."""
    ours = mine(["-m", "x"], repo)
    assert ours.returncode != 0, "we accepted commit-tree with no tree SHA"


# ===========================================================================
# Known divergences from real git -- documented, expected to fail (strict)
# ===========================================================================

def test_empty_message_matches_real(repo):
    """D1 (fixed): `-m ''` produces the same body real git does -- no extra
    trailing blank line."""
    tree = make_tree(repo)
    assert_matches_real(repo, [tree, "-m", ""], "")


def test_multiple_parents_matches_real(repo):
    """D2 (fixed): two `-p` flags yield a two-parent merge commit, matching the
    structure (and parent order) real git produces."""
    tree = make_tree(repo)
    p1 = make_commit(repo, tree, "p1")
    files2 = {"b.txt": "world\n"}
    tree2 = make_tree(repo, files2)
    p2 = make_commit(repo, tree2, "p2")
    args = [tree, "-p", p1, "-p", p2, "-m", "merge"]
    ours = mine(args, repo)
    assert ours.returncode == 0, ours.stderr
    body = show_normalized(repo, ours.stdout.strip())
    assert body.count("parent ") == 2, (
        f"expected 2 parent lines, got {body.count('parent ')}:\n{body}")
    assert_matches_real(repo, args, "merge")


def test_nonexistent_tree_is_rejected(repo):
    """D3 (fixed): a syntactically-40-char but non-hex/non-existent tree is
    rejected, the way real git rejects it."""
    make_tree(repo)  # give the repo an objects dir to write into
    bogus = "z" * 40
    ours = mine([bogus, "-m", "x"], repo)
    assert ours.returncode != 0, "we accepted a non-existent tree SHA"


def test_valid_hex_but_absent_tree_is_rejected(repo):
    """D3 (fixed): a well-formed hex SHA that names no stored object is rejected
    -- existence is checked, not just the 40-hex shape."""
    make_tree(repo)
    absent = "0" * 40  # valid hex, but no such object exists
    ours = mine([absent, "-m", "x"], repo)
    assert ours.returncode != 0, "we accepted a hex SHA with no stored object"


@pytest.mark.xfail(strict=True, reason="D4: we reuse GIT_AUTHOR_* for the "
                   "committer and ignore GIT_COMMITTER_*")
def test_distinct_committer_identity(repo):
    """D4: a committer identity distinct from the author should appear on the
    committer line (real git honours GIT_COMMITTER_*)."""
    tree = make_tree(repo)
    env = {**_os_environ(),
           "GIT_AUTHOR_NAME": "Alice", "GIT_AUTHOR_EMAIL": "alice@example.com",
           "GIT_COMMITTER_NAME": "Bob", "GIT_COMMITTER_EMAIL": "bob@example.com"}
    out = subprocess.run([str(BIN), "commit-tree", tree, "-m", "hi"], cwd=repo,
                         capture_output=True, text=True, env=env)
    assert out.returncode == 0, out.stderr
    body = show_normalized(repo, out.stdout.strip())
    assert "committer Bob <bob@example.com>" in body, body


# ---------------------------------------------------------------------------

def _os_environ():
    import os
    return dict(os.environ)
