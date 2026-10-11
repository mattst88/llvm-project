// RUN: llvm-mc -triple ia64 %s | FileCheck %s
// RUN: llvm-mc -triple ia64 -filetype=obj %s | llvm-readelf -s - | FileCheck %s --check-prefix=SYM

// The directives that tell a checker of dependency violations what it cannot
// work out are taken and dropped, as there is no such checker.

// CHECK:      .text
// CHECK-NEXT: nop 0
// SYM-NOT: foo
	.text
	.pred.safe_across_calls p1-p5, p16-p63
	.PRED.SAFE_ACROSS_CALLS p1
	.pred.rel "mutex", p1, p2
	.pred.rel "imply", p1, p2
	.pred.rel "clear", p1, p2
	.pred.rel @mutex, p1, p2
	.pred.rel "mutex" p1, p2
	.pred.rel @imply p1, p2
	.pred.rel.mutex p1, p2, p3
	.pred.rel.mutex p1-p3
	.pred.rel.imply p1, p2
	.pred.rel.clear p1
	.pred.rel.clear p1, p2
	.mem.offset 8, 1
	.reg.val r4, 5
	.reg.val ar.lc, 5
	.entry
	.entry foo, bar
	.explicit
	.default
	nop 0
