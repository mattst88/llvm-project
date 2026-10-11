// RUN: not llvm-mc -triple ia64 -filetype=obj %s -o /dev/null 2>&1 \
// RUN:   | FileCheck %s --implicit-check-not=error: --implicit-check-not=warning:

	.text
// No template has three slots for the memory unit.
{
	nop.m 1
	nop.m 2
// CHECK: :[[#@LINE+1]]:2: error: instruction does not fit in the bundle
	nop.m 3
}

// Nor are there four slots.
{
	nop.m 1
	nop.i 2
	nop.i 3
// CHECK: :[[#@LINE+1]]:2: error: instruction does not fit in the bundle
	nop.i 4
}

// The template that is named is the one that it is to be.
{ .mii
	nop.m 1
// CHECK: :[[#@LINE+1]]:2: error: instruction does not fit in the bundle
	nop.m 2
}

// "movl" is two of the slots.
{
	nop.m 1
	nop.i 2
// CHECK: :[[#@LINE+1]]:2: error: instruction does not fit in the bundle
	movl r4 = 5
}

// A label is the address of a bundle.
{
	nop.m 1
// CHECK: :[[#@LINE+1]]:1: error: label is not the first of the bundle
l:
}

// An instruction that changes the translations ends its group, and of the
// templates only MMI has a stop after its first slot.
{ .mii
// CHECK: :[[#@LINE+1]]:2: error: instruction has to end its group, and the MII template has no stop after its first slot
	itc.d r4
	nop 0
	nop 0
}
{ .mfi
// CHECK: :[[#@LINE+1]]:2: error: instruction has to end its group, and the MFI template has no stop after its first slot
	ptc.g r4, r5 ;;
}
{ .mmi
	itc.i r4
	nop 0
	nop 0
}
// A template that is not in braces is advice, and gives way.
	.mii
	itc.i r4
	nop 0
	nop 0 ;;

// CHECK: :[[#@LINE+1]]:1: warning: no bundle is open
}
{
// CHECK: :[[#@LINE+1]]:1: warning: a bundle is already open
{
}

// CHECK: :[[#@LINE+1]]:1: error: bundle is not closed
{
	nop.m 1
