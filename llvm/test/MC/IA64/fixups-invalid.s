// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 | FileCheck %s

	.text
f:
	nop.i 0
.La:
	nop.i 0
	nop.i 0
	nop.i 0
// A branch is to a bundle, or to a slot of one.
// CHECK: :[[#@LINE+1]]:18: error: target is not the start of a bundle
	br.sptk.many .La+4
// CHECK: :[[#@LINE+1]]:16: error: target is out of range
	br.sptk.many f+0x2000000
// CHECK: :[[#@LINE+1]]:16: error: value does not fit in 14 bits
	adds r4 = .Lb - f, r5
// CHECK: :[[#@LINE+1]]:16: error: value does not fit in 22 bits
	addl r4 = .Lc - f, r1

// There is no relocation for a tag.
// CHECK: :[[#@LINE+1]]:15: error: tag must be a location in the same section
	mov b6 = r2, x
// CHECK: :[[#@LINE+1]]:14: error: tag must be a location in the same section
	brp.sptk x, x

// Not every field has a relocation for every specifier.
// CHECK: :[[#@LINE+1]]:12: error: relocation specifier is not supported here
	adds r4 = @gprel(x), r5
// CHECK: :[[#@LINE+1]]:12: error: relocation specifier is not supported here
	movl r4 = @ltoffx(x)
// CHECK: :[[#@LINE+1]]:12: error: relocation specifier is not supported here
	addl r4 = @secrel(x), r1
// CHECK: :[[#@LINE+1]]:25: error: relocation specifier is not supported here
	br.call.sptk.many b0 = @pltoff(x)
// CHECK: :[[#@LINE+1]]:9: error: relocation specifier is not supported here
	.8byte @ltoff(x)
// CHECK: :[[#@LINE+1]]:9: error: relocation specifier is not supported here
	.4byte @tprel(x)
// CHECK: :[[#@LINE+1]]:9: error: unsupported relocation type
	.2byte x

// A function descriptor is for the function.
// CHECK: :[[#@LINE+1]]:12: error: @fptr cannot take an offset
	addl r4 = @ltoff(@fptr(x+8)), r1
// CHECK: :[[#@LINE+1]]:9: error: @fptr cannot take an offset
	.8byte @fptr(x+8)
// The symbol of ld8.mov is the one the linkage table entry is for.
// CHECK: :[[#@LINE+1]]:21: error: relocation specifier is not supported here
	ld8.mov r1 = [r2], @ltoff(x)

	.space 0x2000
.Lb:
	.space 0x200000
.Lc:

// A difference from a place in the section is relative to the IP, and only
// some fields have such a relocation.
// CHECK: :[[#@LINE+1]]:14: error: relocation is not supported here
	adds r4 = x - f, r5
// CHECK: :[[#@LINE+1]]:22: error: a difference cannot take a relocation specifier
	addl r4 = @gprel(x) - f, r1
