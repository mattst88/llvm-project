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

// The compiler writes the object itself. When it is asked to leave that to
// the assembler, the assembler is told that the stops in the file are all
// there are.

// RUN: %clang --target=ia64-unknown-linux-gnu -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=IAS
// IAS-NOT: "-no-integrated-as"
// IAS:     "-emit-obj"
// IAS-NOT: as{{(.exe)?}}"

// RUN: %clang --target=ia64-unknown-linux-gnu -fno-integrated-as -### -c %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=AS
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

// A pointer to a function is to its descriptor, so the check of the type of
// the function called has nowhere to find it, and -fsanitize=undefined does
// without.

// RUN: not %clang --target=ia64-unknown-linux-gnu -fsanitize=function -### %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=FSAN
// FSAN: unsupported option '-fsanitize=function' for target 'ia64-unknown-linux-gnu'
// RUN: %clang --target=ia64-unknown-linux-gnu -fsanitize=undefined -### %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=UBSAN
// UBSAN: "-fsanitize=alignment,
// UBSAN-NOT: {{[=,]}}function

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

// What gcc is told about how to write the code, and that changes nothing
// about what the code does, is taken and no more is said of it. It is not
// taken for another target.

// RUN: %clang --target=ia64-unknown-linux-gnu -Werror -fsyntax-only %s \
// RUN:   -mgnu-as -mgnu-ld -mregister-names -mno-register-names \
// RUN:   -mdwarf2-asm -mno-dwarf2-asm -mearly-stop-bits -mno-early-stop-bits \
// RUN:   -mvolatile-asm-stop -mno-volatile-asm-stop \
// RUN:   -minline-float-divide-min-latency \
// RUN:   -minline-float-divide-max-throughput -mno-inline-float-divide \
// RUN:   -minline-int-divide-min-latency -minline-int-divide-max-throughput \
// RUN:   -mno-inline-int-divide -minline-sqrt-min-latency \
// RUN:   -minline-sqrt-max-throughput -mno-inline-sqrt
// RUN: %clang --target=ia64-unknown-linux-gnu -Werror -fsyntax-only %s \
// RUN:   -msched-br-data-spec -mno-sched-br-data-spec -msched-ar-data-spec \
// RUN:   -mno-sched-ar-data-spec -msched-control-spec \
// RUN:   -mno-sched-control-spec -msched-br-in-data-spec \
// RUN:   -mno-sched-br-in-data-spec -msched-ar-in-data-spec \
// RUN:   -mno-sched-ar-in-data-spec -msched-in-control-spec \
// RUN:   -mno-sched-in-control-spec -msched-spec-ldc -mno-sched-spec-ldc \
// RUN:   -msched-spec-control-ldc -mno-sched-spec-control-ldc \
// RUN:   -msched-prefer-non-data-spec-insns \
// RUN:   -mno-sched-prefer-non-data-spec-insns \
// RUN:   -msched-prefer-non-control-spec-insns \
// RUN:   -mno-sched-prefer-non-control-spec-insns \
// RUN:   -msched-count-spec-in-critical-path \
// RUN:   -mno-sched-count-spec-in-critical-path \
// RUN:   -msched-stop-bits-after-every-cycle \
// RUN:   -mno-sched-stop-bits-after-every-cycle \
// RUN:   -msched-fp-mem-deps-zero-cost -mno-sched-fp-mem-deps-zero-cost \
// RUN:   -msched-max-memory-insns=2 -msched-max-memory-insns-hard-limit \
// RUN:   -mno-sched-max-memory-insns-hard-limit \
// RUN:   -msel-sched-dont-check-control-spec \
// RUN:   -mno-sel-sched-dont-check-control-spec
// RUN: not %clang --target=x86_64-unknown-linux-gnu -fsyntax-only %s \
// RUN:   -msched-max-memory-insns=2 2>&1 \
// RUN:   | FileCheck %s --check-prefix=IGNORED-SCHED
// IGNORED-SCHED: error: unsupported option '-msched-max-memory-insns=' for target
// RUN: not %clang --target=x86_64-unknown-linux-gnu -fsyntax-only %s \
// RUN:   -mno-inline-int-divide 2>&1 | FileCheck %s --check-prefix=IGNORED
// IGNORED: error: unsupported option '-mno-inline-int-divide' for target

// -mtls-size= says how many bits the offset of what a thread has may take.

// RUN: %clang --target=ia64-unknown-linux-gnu -mtls-size=14 -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TLS14
// TLS14: "-mtls-size=14"
// RUN: %clang --target=ia64-unknown-linux-gnu -mtls-size=64 -### -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=TLS64
// TLS64: "-mtls-size=64"
// RUN: not %clang --target=ia64-unknown-linux-gnu -mtls-size=24 -### -c %s \
// RUN:   2>&1 | FileCheck %s --check-prefix=TLS-BAD
// TLS-BAD: error: invalid integral value '24' in 'mtls-size='
