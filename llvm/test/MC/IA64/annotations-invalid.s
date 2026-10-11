// RUN: not llvm-mc -triple ia64 %s 2>&1 | FileCheck %s

// CHECK: :[[#@LINE+1]]:25: error: expected a predicate register
	.pred.safe_across_calls
// CHECK: :[[#@LINE+1]]:26: error: range of registers is the wrong way around
	.pred.safe_across_calls p5-p1
// CHECK: :[[#@LINE+1]]:26: error: expected a predicate register
	.pred.safe_across_calls r1
// CHECK: :[[#@LINE+1]]:29: error: expected newline
	.pred.safe_across_calls p1 p2
// CHECK: :[[#@LINE+1]]:29: error: expected a predicate register
	.pred.safe_across_calls p1,
// CHECK: :[[#@LINE+1]]:31: error: expected newline
	.pred.safe_across_calls p1-p2-p3
// CHECK: :[[#@LINE+1]]:12: error: unknown predicate relation
	.pred.rel "bogus", p1, p2
// CHECK: :[[#@LINE+1]]:12: error: expected a predicate relation
	.pred.rel mutex, p1, p2
// CHECK: :[[#@LINE+1]]:19: error: expected a predicate register
	.pred.rel "clear"
// CHECK: :[[#@LINE+1]]:18: error: expected at least two predicate registers
	.pred.rel.mutex p1
// CHECK: :[[#@LINE+1]]:18: error: expected two predicate registers
	.pred.rel.imply p1
// CHECK: :[[#@LINE+1]]:18: error: expected two predicate registers
	.pred.rel.imply p1-p3
// CHECK: :[[#@LINE+1]]:17: error: expected a predicate register
	.pred.rel.clear
// CHECK: :[[#@LINE+1]]:15: error: expected comma
	.mem.offset 8
// CHECK: :[[#@LINE+1]]:14: error: expected absolute expression
	.mem.offset x, 0
// CHECK: :[[#@LINE+1]]:13: error: expected comma
	.reg.val r4
// CHECK: :[[#@LINE+1]]:13: error: unexpected token
	.entry foo bar
// CHECK: :[[#@LINE+1]]:9: error: expected a symbol
	.entry 5
// CHECK: :[[#@LINE+1]]:12: error: expected newline
	.explicit 1
// The assembler leaves the stops to the source.
// CHECK: :[[#@LINE+1]]:7: error: the assembler does not place stops by itself
	.auto
