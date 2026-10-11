// The frame pointer is never needed to unwind a frame, so none is kept unless
// one is asked for, and the unwind tables are on as they are for gcc.

// RUN: %clang --target=ia64-unknown-linux-gnu -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CC1
// CC1:     "-cc1" "-triple" "ia64-unknown-linux-gnu"
// CC1-NOT: "-mframe-pointer=all"
// CC1:     "-funwind-tables=1"

// RUN: %clang --target=ia64-unknown-linux-gnu -fno-omit-frame-pointer -### \
// RUN:   -c %s 2>&1 | FileCheck %s --check-prefix=FP
// FP: "-mframe-pointer=all"

// The compiler writes the stops and the assembler packs the bundles, and is
// told that the stops in the file are all there are.

// RUN: %clang --target=ia64-unknown-linux-gnu -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=AS
// AS: "-no-integrated-as"
// AS: as{{(.exe)?}}" "-x"

// RUN: %clang --target=ia64-unknown-linux-gnu -### %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LD
// LD: ld{{(.exe)?}}"
// LD: "-m" "elf64_ia64"
// LD: "-dynamic-linker" "{{.*}}/lib/ld-linux-ia64.so.2"

// gcc builds no crtbeginT.o for a static link.

// RUN: %clang --target=ia64-unknown-linux-gnu -static -### %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=STATIC
// STATIC:     "-static"
// STATIC:     crtbegin.o"
// STATIC-NOT: crtbeginT.o

// RUN: not %clang --target=ia64-unknown-linux-gnu -fsanitize=memory -### %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=MSAN
// MSAN: unsupported option '-fsanitize=memory' for target 'ia64-unknown-linux-gnu'

// An object of eight bytes or fewer is short data, and -G and -mno-sdata
// say otherwise.

// RUN: %clang --target=ia64-unknown-linux-gnu -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SDATA8
// RUN: %clang --target=ia64-unknown-linux-gnu -mno-sdata -msdata -### -c %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=SDATA8
// SDATA8: "-msmall-data-limit" "8"
// RUN: %clang --target=ia64-unknown-linux-gnu -mno-sdata -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SDATA0
// RUN: %clang --target=ia64-unknown-linux-gnu -G0 -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SDATA0
// SDATA0: "-msmall-data-limit" "0"
// RUN: %clang --target=ia64-unknown-linux-gnu -mno-sdata -G 16 -### -c %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=SDATA16
// SDATA16: "-msmall-data-limit" "16"

// A program that is one module says so with -mconstant-gp.

// RUN: %clang --target=ia64-unknown-linux-gnu -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=NO-CONSTANT-GP
// NO-CONSTANT-GP-NOT: constant-gp
// RUN: %clang --target=ia64-unknown-linux-gnu -mconstant-gp -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CONSTANT-GP
// CONSTANT-GP: "-target-feature" "+constant-gp"

// -mtune= names the processor the code is to be fastest on, by the name it
// is sold under or the name of its design.

// RUN: %clang --target=ia64-unknown-linux-gnu -mtune=itanium2 -### -c %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=TUNE2
// RUN: %clang --target=ia64-unknown-linux-gnu -mtune=mckinley -### -c %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=TUNE2
// TUNE2: "-tune-cpu" "itanium2"
// RUN: %clang --target=ia64-unknown-linux-gnu -mtune=merced -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TUNE1
// TUNE1: "-tune-cpu" "itanium"
// RUN: %clang --target=ia64-unknown-linux-gnu -mtune=mckinley -Werror \
// RUN:   -fsyntax-only %s
// RUN: not %clang --target=ia64-unknown-linux-gnu -mtune=pentium \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=TUNE-BAD
// TUNE-BAD: error: unknown target CPU 'pentium'
