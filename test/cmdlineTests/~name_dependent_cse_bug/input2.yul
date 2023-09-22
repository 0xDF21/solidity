object "C" {
    code {}

    object "C_deployed" {
        code {
            sstore(0, main(sload(0), sload(0), sload(0), sload(0)))

            function main(a, d, b, c) -> v {
                for {} 1 {}
                {
                    if iszero(b) { break }

                    let mid := avg(b, c)
                    switch gt(1, c)
                    case 0 {
                        b := cadd(mid, 0)
                    }
                    default {
                        sstore(0x20, mid)
                    }
                }
            }

            function f(x) -> r {}

            function avg(x, y) -> var {
                // In the other file `_2` is called `_1`.
                let _2 := add(x, y)
                var := add(_2, _2)
            }

            function cadd(x, y) -> sum {
                sum := add(x, y)

                if gt(0, sum) {
                    mstore(0, 0)
                    mstore(0, 0)
                    revert(0, 0)
                }
            }
        }
    }
}
