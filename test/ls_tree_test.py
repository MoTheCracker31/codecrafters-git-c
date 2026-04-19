import pytest
import subprocess
import os
import sys
import stat
import pathlib
import tempfile


@pytest.fixture
def git_repo(tmp_path):

    subprocess.run(["git", "init"], cwd=tmp_path)
    subprocess.run(["git", "config", "user.name", '"Tester"'], cwd=tmp_path)
    subprocess.run(["git", "config", "user.email",
                   "test@testmail.com"], cwd=tmp_path)
    yield tmp_path


def my_git(args: list[str], cwd: str):

    return subprocess.run(args=["./build/git"] + args,
                          capture_output=True, text=True, cwd=cwd)


def real_git(args: list[str], cwd: str):

    return subprocess.run(args=["git"] + args, capture_output=True, text=True, cwd=cwd)


def test_valid_tree_empty(git_repo):

    emptyTreeHash = subprocess.run(
        ["git", "write-tree"], cwd=git_repo, text=True, capture_output=True).stdout.strip()
    my_git_result = my_git(
        ["ls-tree", emptyTreeHash], git_repo).stdout.strip()
    real_git_result = real_git(
        ["ls-tree", emptyTreeHash], git_repo).stdout.strip()
    assert my_git_result == real_git_result


def test_valid_tree_single_file(git_repo):
    with open(git_repo / "test_object", "w+") as fd:
        fd.write("This is file should be a blob in git")

    subprocess.run(["git", "add", "test_object"], cwd=git_repo)
    subprocess.run(["git", "commit", "-m", '"One file test"'], cwd=git_repo)
    shaHash = subprocess.run(
        ["git", "rev-parse", "HEAD^{tree}"], cwd=git_repo, text=True, capture_output=True).stdout.strip()

    my_git_result = my_git(["ls-tree", shaHash], git_repo).stdout.strip()
    real_git_result = real_git(["ls-tree", shaHash], git_repo).stdout.strip()

    assert my_git_result == real_git_result


def test_valid_tree_many_files(git_repo):
    for i in range(100):
        if i % 2:
            (git_repo / f"file_{i}.txt").write_text(f"content_{i}")
        else:
            os.makedirs(git_repo / f"dir_{i}")
            (git_repo / f"dir_{i}" /
             "file_{i}").write_text(f"content_{i}")

    subprocess.run(["git", "add", "."], cwd=git_repo)
    subprocess.run(["git", "commit", "-m", "many file tests"], cwd=git_repo)
    sha = subprocess.run(
        ["git", "rev-parse", "HEAD^{tree}"], cwd=git_repo, text=True, capture_output=True).stdout.strip()

    my_git_result = my_git(["ls-tree", sha], git_repo).stdout.strip()
    real_git_result = real_git(["ls-tree", sha], git_repo).stdout.strip()

    assert my_git_result == real_git_result


@pytest.mark.skipif(sys.platform == "win32", reason="Windows doesn't support executable bit")
def test_executable_object(git_repo):

    (git_repo / "test_exec").write_text("executable content")

    os.chmod(git_repo / "test_exec", stat.S_IRUSR |
             stat.S_IWUSR | stat.S_IXUSR)

    subprocess.run(["git", "add", "test_exec"], cwd=git_repo)
    subprocess.run(["git", "commit", "-m", "executable test"], cwd=git_repo)
    sha = subprocess.run(
        ["git", "rev-parse", "HEAD^{tree}"], cwd=git_repo, capture_output=True, text=True).stdout.strip()

    my_git_result = my_git(["ls-tree", sha], git_repo).stdout.strip()
    real_git_result = real_git(["ls-tree", sha], git_repo).stdout.strip()

    assert my_git_result == real_git_result


def _symlinks_supported():
    try:
        test = pathlib.Path(tempfile.mkdtemp()) / "test_link"
        os.symlink("target", test)
        return True
    except OSError:
        return False


@pytest.mark.skipif(not _symlinks_supported(), reason="Symlink is not activated on windows without activating developer mode")
def test_sym_link_object(git_repo):
    with open(git_repo / f"target.txt", "w") as fd:
        fd.write("SymLink test target")

    os.symlink(git_repo / f"target.txt", git_repo / "link_to_target")

    subprocess.run(["git", "add", "target.txt",
                   "link_to_target"], cwd=git_repo)
    subprocess.run(["git", "commit", "-m", "symlink test"], cwd=git_repo)

    sha = subprocess.run(
        ["git", "rev-parse", "HEAD^{tree}"], cwd=git_repo, capture_output=True, text=True).stdout.strip()
    my_git_result = my_git(["ls-tree", sha], git_repo).stdout.strip()
    real_git_result = real_git(["ls-tree", sha], git_repo).stdout.strip()

    assert real_git_result == my_git_result


def test_nonexistent_object(git_repo):

    random_sha1 = "56775bb445a29feedf7c0875f80678e82c0ef777"
    real_git_result = real_git(["ls-tree", random_sha1], git_repo)
    my_git_result = my_git(["ls-tree", random_sha1], git_repo)

    assert my_git_result.stderr.strip() == real_git_result.stderr.strip()
    assert my_git_result.returncode == real_git_result.returncode


def test_nontree_object(git_repo):
    (git_repo / f"test.txt").write_text("This is a blob object")
    blob = subprocess.run(["git", "hash-object", "-w", "test.txt"],
                          cwd=git_repo, capture_output=True, text=True).stdout.strip()
    my_git_result = my_git(["ls-tree", blob], git_repo)
    real_git_result = real_git(["ls-tree", blob], git_repo)

    assert my_git_result.stderr.strip() == real_git_result.stderr.strip()
    assert my_git_result.returncode == real_git_result.returncode
