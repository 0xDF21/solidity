import subprocess
from pathlib import Path
from shutil import which


def run_git_command(command):
    process = subprocess.run(
        command,
        encoding='utf8',
        capture_output=True,
        check=True,
    )
    return process.stdout.strip()


def git_current_branch():
    return run_git_command(['git', 'symbolic-ref', 'HEAD', '--short'])


def git_commit_hash(ref: str = 'HEAD'):
    return run_git_command(['git', 'rev-parse', '--verify', ref])


class FileMismatchError(Exception):
    def __str__(self):
        return f"Diff mismatch:\n {self.args[0]}"


def diff(file_a: Path, file_b: Path):
    """
    Returns with no output if there are no differences between the two inputs.
    Otherwise (i.e. if the process exits with a non-zero exit code) throws FileMismatchError.
    """

    if which("git") is None:
        raise RuntimeError("git not found.")

    try:
        subprocess.check_output(
            f"""\
            git diff \
                --color \
                --word-diff=plain \
                --word-diff-regex=. \
                --ignore-space-change \
                --ignore-blank-lines \
                {file_a} {file_b}
            """,
            encoding="utf8",
            shell=True
        )
    except subprocess.CalledProcessError as e:
        raise FileMismatchError(e.output) from e
