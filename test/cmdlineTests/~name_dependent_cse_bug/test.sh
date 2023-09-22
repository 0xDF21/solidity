#!/usr/bin/env bash
set -eo pipefail

# This is a regression test against https://github.com/ethereum/solidity/issues/14494
# Note that the two input files in this test differ only by the name of a single variable.
# Due to the bug, a decision about which variable to use to replace a subexpression in CSE would
# depend on sorting order of variable names. A variable not being used as a replacement could lead
# to it being unused in general and removed by Unused Pruner. This would show up as a difference
# in the bytecode.

# shellcheck source=scripts/common.sh
source "${REPO_ROOT}/scripts/common.sh"
# shellcheck source=scripts/common_cmdline.sh
source "${REPO_ROOT}/scripts/common_cmdline.sh"

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

function assemble {
    local input_file="$1"
    msg_on_error --no-stderr \
        "$SOLC" --strict-assembly "$input_file" --optimize --debug-info none |
            stripCLIDecorations
}

diff_values \
    "$(assemble "${SCRIPT_DIR}/input1.yul")" \
    "$(assemble "${SCRIPT_DIR}/input2.yul")"
