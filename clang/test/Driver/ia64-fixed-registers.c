// A register that -ffixed-rN or -mfixed-range= names is kept from the
// compiler.

// RUN: %clang --target=ia64-unknown-linux-gnu -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NONE
// NONE-NOT: "+reserve-

// RUN: %clang --target=ia64-unknown-linux-gnu -ffixed-r4 -ffixed-r13 \
// RUN:   -ffixed-r31 -### -c %s 2>&1 | FileCheck %s --check-prefix=FIXED
// FIXED: "-target-feature" "+reserve-r4"
// FIXED: "-target-feature" "+reserve-r13"
// FIXED: "-target-feature" "+reserve-r31"

// RUN: %clang --target=ia64-unknown-linux-gnu \
// RUN:   -mfixed-range=f12-f15,f126-f127,r5-r5 -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=RANGE
// RANGE-NOT: "+reserve-f11"
// RANGE:     "-target-feature" "+reserve-f12"
// RANGE:     "-target-feature" "+reserve-f13"
// RANGE:     "-target-feature" "+reserve-f14"
// RANGE:     "-target-feature" "+reserve-f15"
// RANGE-NOT: "+reserve-f16"
// RANGE:     "-target-feature" "+reserve-f126"
// RANGE:     "-target-feature" "+reserve-f127"
// RANGE:     "-target-feature" "+reserve-r5"

// The branch registers that a function preserves can be kept as well, and
// the ones a call needs cannot.

// RUN: %clang --target=ia64-unknown-linux-gnu -mfixed-range=b1-b5 -### -c %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=BRANCH
// BRANCH: "-target-feature" "+reserve-b1"
// BRANCH: "-target-feature" "+reserve-b2"
// BRANCH: "-target-feature" "+reserve-b3"
// BRANCH: "-target-feature" "+reserve-b4"
// BRANCH: "-target-feature" "+reserve-b5"
// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=b0-b5 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BRANCH0
// BRANCH0: error: invalid value 'b0-b5' in '-mfixed-range='
// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=b5-b6 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BRANCH6
// BRANCH6: error: invalid value 'b5-b6' in '-mfixed-range='

// The option is used, and is no other target's.

// RUN: %clang --target=ia64-unknown-linux-gnu -mfixed-range=f12-f15 -Werror \
// RUN:   -fsyntax-only %s
// RUN: not %clang --target=x86_64-unknown-linux-gnu -mfixed-range=f12-f15 \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=OTHER
// OTHER: error: unsupported option '-mfixed-range=' for target

// A second option adds to the first.

// RUN: %clang --target=ia64-unknown-linux-gnu -mfixed-range=f2-f3 \
// RUN:   -mfixed-range=f16-f16 -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TWICE
// TWICE: "-target-feature" "+reserve-f2"
// TWICE: "-target-feature" "+reserve-f3"
// TWICE: "-target-feature" "+reserve-f16"

// A range that is not two registers of a kind, the first no later than the
// last and neither of them constant or stacked, is refused.

// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=f12 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BAD1
// BAD1: error: invalid value 'f12' in '-mfixed-range='
// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=f15-f12 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BAD2
// BAD2: error: invalid value 'f15-f12' in '-mfixed-range='
// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=r4-f12 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BAD3
// BAD3: error: invalid value 'r4-f12' in '-mfixed-range='
// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=f0-f5 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BAD4
// BAD4: error: invalid value 'f0-f5' in '-mfixed-range='
// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=r30-r32 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BAD5
// BAD5: error: invalid value 'r30-r32' in '-mfixed-range='
// RUN: not %clang --target=ia64-unknown-linux-gnu -mfixed-range=f2-f3,,f4-f5 \
// RUN:   -### -c %s 2>&1 | FileCheck %s --check-prefix=BAD6
// BAD6: error: invalid value '' in '-mfixed-range='
