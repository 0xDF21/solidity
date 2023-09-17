#!/usr/bin/env python3

import os
import sys
import subprocess
from pathlib import Path
from shutil import which
from tempfile import mkdtemp

PROJECT_ROOT = Path(__file__).parents[3]
CMDLINE_TEST_DIR = PROJECT_ROOT / "test/cmdlineTests"

sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
#pylint: disable=wrong-import-position
from bytecodecompare.prepare_report import FileReport
from bytecodecompare.prepare_report import parse_cli_output
from splitSources import split_sources


class FileMismatchError(Exception):
    def __str__(self):
        return f"Git diff mismatch:\n {self.args[0]}"


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


def run_solc(report_file_path: Path, solc_binary_path: Path, args = None) -> FileReport:
    if args is None:
        args = []
    output = subprocess.check_output(
        [solc_binary_path] + args,
        shell=False,
        encoding="utf8"
    )
    return parse_cli_output(report_file_path, output)


def save_bytecode_report(contract: str, bytecode_path: Path, reports: FileReport):
    for report in reports.contract_reports:
        if report.contract_name == contract:
            with open(bytecode_path, 'w', encoding='utf8', newline='') as f:
                f.write(report.bytecode if report.bytecode is not None else "<NO BYTECODE>")


def test_bytecode_equivalence(tmp_dir: Path):
    previous_dir = os.getcwd()
    os.chdir(tmp_dir)

    source_file_path = CMDLINE_TEST_DIR / "~bytecode_equivalence_multiple_sources/inputs.sol"
    split_sources(source_file_path)

    solc_binary = os.environ.get("SOLC")
    if solc_binary is None:
        raise RuntimeError("solc compiler not found.")

    # Compiling multiple files at same time should not affect bytecode generation although it changes AST IDs.
    a_bytecode_path = tmp_dir / "A.bytecode"
    a_report_output = run_solc(tmp_dir / "A.report", Path(solc_binary), ["--via-ir", "--bin", "A.sol"])
    save_bytecode_report("A", a_bytecode_path, a_report_output)

    ab_bytecode_path = tmp_dir / "AB.bytecode"
    ab_report_output = run_solc(tmp_dir / "AB.report", Path(solc_binary), ["--via-ir", "--bin", "A.sol", "B.sol"])
    save_bytecode_report("A", ab_bytecode_path, ab_report_output)

    diff(a_bytecode_path, ab_bytecode_path)

    # Restore directory
    os.chdir(previous_dir)

def main():
    try:
        tmp_dir = mkdtemp(prefix="cmdline-test-bytecode-equivalence-multiple-sources")
        test_bytecode_equivalence(Path(tmp_dir))
        return 0
    except (FileMismatchError, RuntimeError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
