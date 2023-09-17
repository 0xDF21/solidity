==== Source: A.sol ====
// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.0;

import "D.sol";
import "B.sol";

contract A is B {
    function a() public pure {
        f();
    }
}

==== Source: B.sol ====
// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.0;

import "C.sol";

abstract contract B is C {}

==== Source: C.sol ====
// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.0;

abstract contract C {
    function c() public pure returns (uint) {
        return 0;
    }
}

==== Source: D.sol ====
// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.0;

import "F.sol";

==== Source: F.sol ====
// SPDX-License-Identifier: UNLICENSED
pragma solidity >=0.0;

function f() pure returns (bytes memory returndata) {
    return "";
}
