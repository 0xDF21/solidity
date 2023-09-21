import os
import subprocess
from pathlib import Path
from tempfile import mkdtemp

from bytecodecompare.prepare_report import FileReport
from bytecodecompare.prepare_report import parse_cli_output

def enter_tmptest_dir(prefix):
    """
        Creates a temporary directory, enters the directory and
        execute the function inside it.
        It restores the previous directory after execute the function.
    """
    def tmptest_dir_decorator(fn):
        previous_dir = os.getcwd()
        def f(*args, **kwargs):
            try:
                tmp_dir = mkdtemp(prefix=prefix)
                os.chdir(tmp_dir)
                return fn(*args, **kwargs)
            finally:
                os.chdir(previous_dir)
        return f
    return tmptest_dir_decorator


def run_solc(report_file_path: Path, args = None) -> FileReport:
    """
        Runs the solidity compiler binary
        specified by the SOLC environment variable
    """

    solc_binary_path = os.environ.get("SOLC")
    if solc_binary_path is None:
        raise RuntimeError("""
        `solc` compiler not found.
        Please ensure you set the SOLC environment variable
        with the correct path to the compiler's binary.
        """
        )
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
