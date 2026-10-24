/// Test that lives in a static library, referenced by nothing.
///
/// Used to prove that discovery does not depend on the linker keeping the
/// object for symbol reasons, and to document the `--whole-archive`
/// requirement for archives whose tests nobody calls.
#include <ward/ward.h>

Test(from_lib, discovered) {
    ward_assert_eq(42, 42);
}

Test(from_lib, another) {
    ward_assert(1 != 2);
}
