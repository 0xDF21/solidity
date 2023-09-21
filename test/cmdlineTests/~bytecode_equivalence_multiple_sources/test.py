#!/usr/bin/env python3

import os
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).parents[3]
CMDLINE_TEST_DIR = PROJECT_ROOT / "test/cmdlineTests"

sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
#pylint: disable=wrong-import-position
from common.cmdline_helpers import run_solc
from common.cmdline_helpers import save_bytecode_report
from common.cmdline_helpers import enter_tmptest_dir
from common.git_helpers import diff
from common.git_helpers import FileMismatchError
from splitSources import split_sources


@enter_tmptest_dir("cmdline-test-bytecode-equivalence-multiple-sources")
def test_bytecode_equivalence():
    tmp_dir = Path(os.getcwd())

    source_file_path = CMDLINE_TEST_DIR / "~bytecode_equivalence_multiple_sources/inputs.sol"
    split_sources(source_file_path, True)

    # Compiling multiple files at same time should not affect bytecode generation although it changes AST IDs.
    a_bytecode_path = tmp_dir / "A.bytecode"
    a_report_output = run_solc(tmp_dir / "A.report", ["--via-ir", "--bin", "A.sol"])
    save_bytecode_report("A", a_bytecode_path, a_report_output)

    ab_bytecode_path = tmp_dir / "AB.bytecode"
    ab_report_output = run_solc(tmp_dir / "AB.report", ["--via-ir", "--bin", "A.sol", "B.sol"])
    save_bytecode_report("A", ab_bytecode_path, ab_report_output)

    diff(a_bytecode_path, ab_bytecode_path)


def main():
    try:
        test_bytecode_equivalence()
        return 0
    except (FileMismatchError, RuntimeError) as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
