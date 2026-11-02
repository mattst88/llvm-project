// RUN: %clang -### --target=ia64-unknown-linux-gnu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FAST %s
// RUN: %clang -### --target=ia64-unknown-linux-gnu -ffp-contract=on -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ON %s
// RUN: %clang -### --target=ia64-unknown-linux-gnu -ffp-contract=off -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=OFF %s
// RUN: %clang -### --target=ia64-unknown-linux-gnu -funsafe-math-optimizations \
// RUN:   -fno-unsafe-math-optimizations -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=FAST %s
// RUN: %clang -### --target=x86_64-unknown-linux-gnu -c %s 2>&1 \
// RUN:   | FileCheck --check-prefix=ON %s

// FAST: "-ffp-contract=fast"
// ON: "-ffp-contract=on"
// OFF: "-ffp-contract=off"
