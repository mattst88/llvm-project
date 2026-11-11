# IA-64 backend capabilities

This document describes the IA-64 (Itanium) code generator on the `ia64`
branch. It covers the transforms that use the architecture's EPIC features
and the measured effect of each. Every number comes from a commit message on
the branch or from a benchmark table recorded alongside it. Every assembly
listing is real output from the branch's `clang`/`llc`, compiled at `-O2` on
2026-11-11. The only edits are dropped bundle-template directives (`.mmi`,
`.mlx`, …) and dropped prologue directives.

Commit hashes refer to the `ia64` branch as of `00f9c554eacd`, which is 637
commits on top of `main`.

## Contents

1. [Results at a glance](#results-at-a-glance)
2. [How performance is measured](#how-performance-is-measured)
3. [Data speculation: `ld.sa`, `ld.c.clr` and the ALAT](#1-data-speculation-ldsa-ldcclr-and-the-alat)
4. [Control speculation: `ld.s`, NaT bits and `tnat`](#2-control-speculation-lds-nat-bits-and-tnat)
5. [Counted loops, software pipelining and register rotation](#3-counted-loops-software-pipelining-and-register-rotation)
6. [Predication and parallel compares](#4-predication-and-parallel-compares)
7. [Instruction groups, bundles and issue ports](#5-instruction-groups-bundles-and-issue-ports)
8. [Post-increment addressing](#6-post-increment-addressing)
9. [Integer and FP arithmetic in the FP unit](#7-integer-and-fp-arithmetic-in-the-fp-unit)
10. [Prefetching and branch hints](#8-prefetching-and-branch-hints)
11. [The register stack](#9-the-register-stack)
12. [Case study: CPython](#10-case-study-cpython)
13. [What was tried and rejected](#what-was-tried-and-rejected)
14. [Where the code lives](#where-the-code-lives)

---

## Results at a glance

The suite has 23 workloads: bzip2 compress and decompress, zstd compress and
decompress, Lua, SQLite, CPython, CoreMark, AES, SHA, the Olden benchmarks (bh,
health, mst), SciMark, LINPACK, n-body, spectral-norm, tramp3d, and the
Polybench kernels cholesky, gemm, fdtd-2d, jacobi-2d and seidel-2d.

These are hardware cycles on the rx2800 i2 (Itanium 9340), best of 5, from
2026-11-10. Every compiler was run at `-O2`. Clang is the `ia64` branch at
`2ec0d321096b`, and the other two are gcc 16 and icc 11.1.

| workload | clang | gcc 16 | icc 11.1 | gcc/clang | icc/clang |
|---|---:|---:|---:|---:|---:|
| aes | 127.3M | 152.3M | 533.7M | +19.6% | +319.2% |
| bh | 282.5M | 464.5M | 367.7M | +64.4% | +30.1% |
| bzip2 -c | 70.6M | 79.7M | 78.2M | +12.8% | +10.7% |
| bzip2 -d | 25.0M | 36.1M | 27.8M | +44.6% | +11.4% |
| cholesky | 14.7M | 42.4M | 18.5M\* | +188.5% | +25.8% |
| coremark | 42.3M | 102.1M | 100.8M | +141.2% | +138.2% |
| fdtd-2d | 4.9M | 30.4M | 8.1M\* | +525.6% | +67.2% |
| gemm | 2.1M | 14.1M | 3.6M\* | +561.6% | +67.2% |
| health | 433.7M | 433.6M | 452.7M | −0.0% | +4.4% |
| jacobi-2d | 5.8M | 46.7M | 131.4M | +712.0% | +2185.2% |
| linpack | 176.4M | 1212.5M | 324.9M | +587.4% | +84.2% |
| lua | 16.8M | 20.3M | 19.0M | +21.1% | +13.7% |
| mst | 701.3M | 806.5M | 709.3M | +15.0% | +1.1% |
| nbody | 52.5M | 358.7M | — | +582.7% | — |
| python | 1114.9M | — | — | — | — |
| scimark | 159.9M | 360.5M | — | +125.4% | — |
| seidel-2d | 48.8M | 83.5M | 80.2M | +71.1% | +64.4% |
| sha | 26.5M | 101.7M | 62.6M | +283.0% | +135.9% |
| spectral | 61.6M | 662.8M | 72.2M | +976.4% | +17.2% |
| sqlite | 569.4M | 661.6M | 652.6M | +16.2% | +14.6% |
| tramp3d | 554.1M | — | — | — | — |
| zstd -c | 86.3M | 123.6M | 198.6M | +43.2% | +130.1% |
| zstd -d | 37.6M | 82.7M | 86.9M | +119.9% | +130.9% |
| **geomean** | | | | **2.53×** | **1.81×** |

\* icc's output fails the suite's hash check because of FMA contraction, and
passes with `-no-IPF-fma`. Its cycles are still shown.

The missing entries are gaps in the runs, not compile failures, except
tramp3d with icc:

- **tramp3d, icc:** tramp3d is C++, and icpc 11.1 cannot parse a modern
  libstdc++.
- **tramp3d, gcc:** never built for the gcc baseline.
- **python, gcc:** built once and run only on the Madison cycle model, where
  it took 1341.0M cycles against clang's 1188.0M (+12.9%). It has not been
  timed on hardware.
- **python, icc:** never built.
- **nbody and scimark, icc:** both build and run. They were timed on
  hardware on 2026-10-23, when icc was +5.9% and +15.3% over clang. The icc
  run directory this table was taken from leaves them out.

How to read this table:

- **gcc does not pipeline at `-O2`.** It needs `-fmodulo-sched`, and its IA-64
  port has had little attention for many years, so the large FP-loop ratios
  (linpack, spectral, jacobi-2d) mostly show that. icc 11.1 is the stronger
  baseline: it was built for this machine and does pipeline and speculate.
- The comparison is with all three compilers at `-O2`, not at each
  compiler's best flags.
- Each column is a single session. On this machine, differences under about
  2% between single runs cannot be attributed to a change (see the next
  section).

---

## How performance is measured

1. **A cycle model.** This is a qemu-ia64 TCG plugin that models Itanium 2
   issue: dispersal onto ports, stops, split issue, result latencies that
   depend on the consumer, L1D/L2/L3 including L2 bank conflicts, the ALAT
   (32 entries, FIFO replacement, matched to hardware probes), and `lfetch`.
   It is deterministic, so it can resolve changes of 0.1%. Against the 9340,
   its mean absolute error over the suite is 3.4% for clang's code.
2. **A 1.3 GHz Madison.** This is a Dell PowerEdge 3250 with a 256 KB L2 and
   a 3 MB L3.
3. **An HP Integrity rx2800 i2** with two quad-core Itanium 9340s
   (Tukwila). Intel documents Tukwila's core as the Montecito core (Montecito
   reference manual update 308065-001); the 9340 has 5 MB of L3 per core.
   `perf` does not work on this machine, so runs are timed with `ar.itc`.

Lessons that shaped how results are reported:

- **Noise.** The 9340's L3 and memory are shared with other tenants. Identical
  binaries have measured ±1.8% apart. Hardware A/Bs are therefore run
  interleaved, over several rounds, and the object files are compared to
  establish which workloads changed at all.
- **Code layout.** In CPython, any change to the size of cold code moves all
  of `.text.hot`. That alone shifts results by ±0.7%. CPython results are
  therefore a mean over a sweep of padded layouts, not a single link.
- **The model is checked against the hardware.** A change that cuts
  instructions but costs cycles in the model is first checked for a modelling
  gap. Two transforms in this document (post-increment pointers and exclusive
  predicates) first measured as regressions because of model bugs.

---

## 1. Data speculation: `ld.sa`, `ld.c.clr` and the ALAT

An *advanced load* (`ld.a`/`ld.sa`) records its address in the ALAT. Any
later store to that address removes the entry. A *check load* (`ld.c.clr`)
at the load's original position does nothing if the entry survived, and
reloads if it did not. The compiler can therefore issue a load above stores
that might alias it, and above branches (with `.sa`, which also defers
faults), and still get the right value.

### The core transform: issue pointer chases early

Implemented in `IA64SpeculateLoads.cpp` (the passes IA64SpeculateLoads and
IA64ExpandSpeculation), in
19b7f92eac4a.

The pass runs before register allocation. It walks a load up through the
blocks that dominate it while the load is still reached at least 75% of the
time. It moves the instructions that compute the load's address along with
it, and leaves the check in place. After register allocation, a pair is kept
only if nothing between the two halves reads or writes the register or the
address, and there is no call between them. Otherwise the pair is undone:
the advanced load becomes `mov r, r0` and the check becomes a plain load.

Here the index is loaded, so `t[i]` is likely to miss. Its address
arithmetic and the load go above both the bounds check and a store that may
alias it (`llvm/test/CodeGen/IA64/speculate-loads.ll`, `@chase`):

```c
long i = *p;
if (i < lim) { *q = 0; return t[i] + 1; }   // q may point into t
return -1;
```

```asm
chase:
	ld8	r16 = [r33] ;;
	shladd	r14 = r16, 2, r32 ;;
	ld4.sa	r15 = [r14]            // issued above the branch and the store
	cmp.ltu	p7, p6 = r16, r35
	(p6) br.cond.dpnt.few	.LBB0_2 ;;
	st4	[r34] = r0             // may alias t[i]: invalidates the ALAT entry
	ld4.c.clr	r15 = [r14] ;;     // free if the entry survived, reloads otherwise
	adds	r8 = 1, r15
	br.ret.sptk.many	b0 ;;
```

Lessons from the first version:

- Hoisting the load alone gains nothing. The win comes from moving its
  address chain (`shladd`, `zxt4`) along with it.
- Speculating every load lost cycles in bzip2's `mainGtU` and
  `sendMTFValues`, whose array walks already hit in the cache. Only
  **pointer chases** are taken: loads whose address derives from another
  load in the same loop, within 5 levels.
- The pseudos must be `mayLoad` with no side effects. A first version that
  marked them `hasSideEffects` pinned the scheduler and cost 1.4%.

### Increments that made it pay

| commit | change | result |
|---|---|---|
| 19b7f92eac4a | Pointer chases issued with `ld.sa`. The win is bzip2's `tPos = tt[tPos]`. | bzip2 -d −3.4%, Lua −0.3% (model) |
| 17bf9852bc0e | For loads that are not chases, move only the address chain up, without the load. Integer ops cannot fault. | bzip2 -c −1.2%, -d −3.1%, Lua −0.9% (model) |
| c42801ac94fa | The same for store operands and branch-compare operands. A heap sift-down latch went from 4 bundles to 2. | bzip2 -c −0.81% (model) |
| 9081065789ec | Issue only the first of several loads near one address. The inlined 16-byte copy took 2 M slots per byte otherwise. | zstd -d −8.80% (model) |
| c3b22b818bba | Do not speculate past a branch that tests the address or something loaded near it. Lua's dispatch bounds-checks the opcode it indexes with. | Lua −2.37%, bzip2 -d −1.09% (model) |
| 40d957066951 | At a join, issue the load at the end of each predecessor and merge the results with phis. | **bzip2 -d −4.6% on hardware** (28.08M → 26.78M) |
| 09c2e47d0796 | Copied FP loads: `ld.sa` above the store of the previous element in unrolled copies. | tramp3d about −1.0% on hardware |
| 6bc621d78813 | Stop at a store into the *same* underlying object. zstd's histogram counters invalidated the ALAT constantly. | **zstd -c −1.4% on hardware** |
| ab3c15c674c2 | Issue an indirect call's entry-point load (from the function descriptor) before the rarely taken tracer branch in `_Py_Dealloc`. | **CPython (PGO) −0.64% on Madison** |

The row for 6bc621d78813 shows a failure mode specific to speculation. An
`ld.sa` placed above a store that usually *does* alias it is not free: the
check load reloads behind the store every time. The fix stops hoisting at the
first may-alias store whose underlying object is the same as the load's.

### What the ALAT actually does

The ALAT on the 9340 was characterized with micro-probes. It has 32 entries.
A failed `chk.a` costs 21 cycles against 7 for one that passes. Replacement
does not follow a deterministic rule that can be matched, and FIFO fits the
measurements best. The model uses FIFO. One measurement shows why getting
the ALAT model right matters: icc 10.1 keeps about 35 `ldfd.a` in flight in
jacobi-2d, so a model that rejected new entries when the table was full would
have rated that code 73% faster than it is.

---

## 2. Control speculation: `ld.s`, NaT bits and `tnat`

A *speculative load* (`ld.s`) that would fault instead sets the target
register's NaT bit. NaT propagates through arithmetic, and the program tests
it with `tnat` (or `chk.s`) before relying on the value. This lets a load go
above the branch that guards its pointer against NULL.

### Searching linked lists ahead

Implemented in `IA64SpeculateChases.cpp` (an IR pass) and the intrinsics
`llvm.ia64.ld{1,2,4,8}.s` and `llvm.ia64.tnat`, in 9d7f5f97b88c and
6e05e7d8265e.

A loop that follows `next` and tests each node can test the next node only
after it has loaded the next node. The pass builds a speculative copy of the
loop that handles several nodes per pass. The first node's loads are
ordinary. Every later node's loads, *including the next pass's pointers*,
are `ld.s`, so dereferencing past the end of the list cannot fault. A pass
continues with **one** branch on the AND of every exit test and every NaT
test. On any NaT it falls back to the original loop at the pass's first
node.

CoreMark's `core_list_find` is an example:

```c
list_head *core_list_find(list_head *list, short idx) {
  while (list && (list->info->idx != idx))
    list = list->next;
  return list;
}
```

```asm
.LBB0_2:                                // %land.rhs.preheader
	ld8.s	r19 = [r32]               // node 1's next, speculatively
	zxt2	r14 = r33 ;;
	ld8.s	r21 = [r19] ;;            // node 2's next
.LBB0_3:                                // %land.rhs.spec — three nodes a pass
	mov	r15 = r32
	adds	r23 = 8, r32
	adds	r24 = 8, r19
	adds	r25 = 8, r21
	mov	r8 = r19
	mov	r16 = r21 ;;
	ld8.s	r32 = [r21]               // next pass's first pointer
	ld8	r22 = [r23]               // node 0's info: an ordinary load
	ld8.s	r26 = [r24]               // nodes 1, 2: info pointers
	ld8.s	r27 = [r25] ;;
	adds	r30 = 2, r22
	ld8.s	r19 = [r32]               // next pass's second pointer
	adds	r28 = 2, r26
	adds	r29 = 2, r27 ;;
	ld2	r18 = [r30]               // node 0's idx
	ld2.s	r20 = [r28]               // nodes 1, 2: idx
	ld2.s	r17 = [r29]
	ld8.s	r21 = [r19]               // next pass's third pointer
	cmp.eq	p7, p6 = 0, r32 ;;        // p6 = next pass's first pointer is non-null
	cmp.ne.and	p6, p0 = r17, r14   // AND in "node 2 not found", ...
	cmp.ne.and	p6, p0 = 0, r16     // ... "node 2 not null", ...
	cmp.ne.and	p6, p0 = r20, r14   // ... "node 1 not found", ...
	cmp.ne.and	p6, p0 = r18, r14   // ... "node 0 not found", ...
	cmp.ne.and	p6, p0 = 0, r8      // ... "node 1 not null"
	(p6) br.cond.dptk.many	.LBB0_3   // one branch per three nodes
	...
.LBB0_7:                                // decide which node ended the pass
	cmp.eq	p8, p9 = 0, r8
	tnat.z	p7, p6 = r8               // did a speculative load defer a fault?
	tnat.z	p11, p10 = r20
	...
	(p6) br.cond.dpnt.many	.LBB0_17  // yes: redo the pass with ordinary loads
	...
.LBB0_17:                               // the original loop, kept as the fallback
	...
	ld8.sa	r16 = [r15] ;;            // ...where data speculation still applies
	cmp.eq	p26, p27 = r17, r14
	(p26) br.cond.dpnt.many	.LBB0_22
	ld8.c.clr	r16 = [r15] ;;
```

In this listing the five `cmp.ne.and` *parallel compares* share one
instruction group and all write `p6`. No NaT test appears for the values
they compare, because an and-type compare clears its predicate when an
operand is NaT (4f8a0a8fcd75, 44aeda1d8318). The fallback loop still gets
its `next` load issued early with `ld8.sa`/`ld8.c.clr` (§1), so a single
function uses both kinds of speculation.

| commit | change | CoreMark result |
|---|---|---|
| 6e05e7d8265e | Speculative search, 3 nodes a pass | model −5.31%, **hardware −6.2%**; sqlite −0.5% on hardware |
| 9369f6fdc492 | Loops that store (`core_list_reverse`) cannot search ahead, but load `next` with `ld8.s` an iteration early and unroll so the pointers rotate without copies. One node per issue group instead of three. | **hardware −3.07%** |
| 4f8a0a8fcd75, caff825a3d6b | Load only the tested bytes, and sink the xor/and below the NaT test | model −1.16%, hardware −0.8% |
| 44aeda1d8318 | Load the next pass's pointers in this pass, and continue with one branch | **hardware −3.20% / −3.34%** (two rounds) |
| a83fbf6b6326 | Let the speculated search's branch chain use the preserved predicates p16–p31 | **hardware −2.13% / −2.41%** |

Supporting changes made this safe. A value that might be NaT and is spilled
uses `st8.spill`/`ld8.fill`, because a plain `st8` of a NaT register faults.
If the frame is too large for that, every `ld.s` in the function is undone.
`shouldSink` refuses to sink an `ld.s`. The intrinsic carries a memory
operand, so it is not a scheduling barrier.

Scheduler-level `ld.s`/`chk.s` hoisting of single
loads in bzip2 and Lua had an *upper bound* of −0.12%: a dry run found only
11 and 24 candidates, and the integer load stalls were already small. The
gain is at the level of whole loops, not individual loads.

---

## 3. Counted loops, software pipelining and register rotation

IA-64 has dedicated loop hardware: the loop count
register `ar.lc`, the epilogue count `ar.ec`, rotating general, FP and
predicate registers (`r32+`, `f32+`, `p16+`), and `br.cloop` and `br.ctop`.
The backend uses all of them.

### Counted loops (c898d8b659d2)

HardwareLoops puts a computable trip count in `ar.lc` and closes the loop
with `br.cloop`, replacing an add, a compare and a branch. `ar.lc` is
callee-saved, so calls in the loop need nothing extra.

### Software pipelining

The generic MachinePipeliner first ran on IA-64 with its default expander,
which copies values between stages inside the kernel. A copy of a load's
result waits for the load, so pipelined loops ran no faster. Two expanders
followed:

1. **MVE** (a9c5ae2b643d) unrolls the kernel instead of copying. axpy went
   from 3.79 to 2.27 cycles per element.
2. **Rotating registers** (a7e0554815f0, `IA64RotatingLoops.cpp`). The loop
   becomes a *single kernel*. Each stage is predicated on a rotating
   predicate, and each value that crosses an iteration lives in a rotating
   register. `br.ctop` runs the kernel `ar.lc + ar.ec` times, so the
   prologue and epilogue come from the predicates and need no code.

This is daxpy, `y[i] += a * x[i]`:

```asm
	adds	r14 = -1, r32 ;;
	mov	ar.lc = r14               // n - 1 iterations after the first
	mov	r14 = r35                 // y gets separate load and store pointers
	mov	ar.ec = 11                // 11 stages to drain
	mov	pr.rot = 65536 ;;         // p16 = 1, p17..p63 = 0: only stage 0 is live
.LBB0_2:
	(p26) stfd	[r35] = f36, 8        // stage 10: store y[i-10]
	(p16) ldfd	f37 = [r34], 8        // stage 0:  load x[i]
	(p22) fma.d	f32 = f8, f43, f50    // stage 6:  a*x + y, operands rotated in
	(p16) ldfd	f44 = [r14], 8        // stage 0:  load y[i]
	br.ctop.sptk.few	.LBB0_2 ;;      // rotate registers and predicates, loop
	clrrrb ;;
```

The kernel is one instruction group: two loads, an FMA, a store and the
branch, one element per cycle at II = 1. The load→FMA→store latencies are
hidden by the 11 stages. Each `fma.d` reads `x` in `f43` and `y` in `f50`,
which the loads wrote six rotations earlier into `f37` and `f44`.

A dot product shows another refinement. The accumulator stays in the static
register `f8` rather than rotating (ca479483bfd4):

```asm
.LBB0_2:
	(p16) ldfd	f32 = [r33], 8
	(p16) ldfd	f39 = [r34], 8
	(p22) fma.d	f8 = f38, f45, f8     // the sum never leaves f8
	br.ctop.sptk.few	.LBB0_2 ;;
```

| commit | change | result |
|---|---|---|
| a7e0554815f0 | Rotating-register kernels | spectral −54.91%, jacobi-2d −22.40%, cholesky −11.11%, coremark −9.67%, fdtd-2d −4.88%, linpack −3.51% (model) |
| ca479483bfd4 | Keep an accumulator that does not rotate in one register, which removes the copy-out after `clrrrb` | scimark −2.30% (model); about −2.0% on hardware |
| 401d875ad079 | Put a stop at every cycle boundary of the pipeliner's schedule, so the post-RA schedulers cannot merge cycles into unbalanced groups | **9340: cholesky −8.8%, fdtd-2d −5%**, linpack −1.3% |
| 95208e0d89ee | Allow 16 stages instead of 10 | **9340: coremark −3.79%, fdtd-2d −4 to −8%**; Madison fdtd-2d −5.84% |
| 356be9951088 | Put each FP load of a pipelined loop in a cycle after the previous one. On Madison, two FP loads to one L2 bank in one cycle cost the second 6 cycles. | **Madison: gemm −7.9%, cholesky −4.6%**; 9340 linpack −7.58% |
| f8610f52c47e | Leave a slot for `br.ctop` in the kernel's last cycle (a generic `MachinePipelinerPolicy` hook). Without it, CPython's `k_mul` spilled into a third bundle. | CPython −0.38% on hardware, with the recurrence reshaping in `IA64ShortenRecurrences` |

Gates, each set by a measured regression:

- A loop needs at least 3 stages to rotate. A two-stage rotating loop drains
  with an extra kernel pass, which is worse than the MVE expander for loops
  of 1–2 iterations.
- A loop whose maximum trip count is no more than its stage count is not
  rotated.
- Kernel-only loops always run N + (stages − 1) passes. That is inherent and
  not worth chasing.

Supporting work in the generic pipeliner:

- Constant physical registers count as invariant, so the always-true `$p0`
  qualifying predicate does not hide every increment.
- Memory dependences carry iteration distances.
- Accesses are separated using SCEV.
- `FuncUnitSorter` resolves variant sched classes.

---

## 4. Predication and parallel compares

Every IA-64 instruction has a qualifying predicate, and a compare writes a
complementary pair of them.

### If-conversion (937d270767c9)

After register allocation, a short branch-around (at most 4 instructions per
side, no calls) becomes predicated code. A side taken less than one time in
eight keeps its branch. A select becomes a predicated copy:

```asm
pick:                                   // return c ? *p : *q;
	cmp4.eq	p6, p7 = 0, r34 ;;
	(p6) mov	r32 = r33 ;;
	ld8	r8 = [r32]
	br.ret.sptk.many	b0 ;;
```

### Parallel compares (8c6c69999480, ec1096e353ab, 61bfd73c9c06)

`cmp.*.or.andcm` and `cmp.*.and` write their predicates only for one
outcome, so any number of them can write the same pair in **one** group.
`a == b || c == d || e == 0` takes two groups and no branches:

```asm
match:
	cmp.eq	p6, p7 = 0, r36
	mov	r8 = r0 ;;
	cmp.eq.or.andcm	p6, p7 = r32, r33
	cmp.eq.or.andcm	p6, p7 = r34, r35 ;;
	(p6) mov	r8 = 1
	br.ret.sptk.many	b0 ;;
```

The branch expander also merges chains of conditional branches into a
single branch on a parallel-compare chain, repeatedly. busybox at `-O2` gets
1013 of these and 3.8 KiB less text.

### Exclusive predicates and predicate renaming

| commit | change | result |
|---|---|---|
| a38874df0d16, 8cf4f1186f15 | Instructions under `p6` and `p7` from one compare never both take effect. They may share a group even when both write one register (`(p6) ld4 r8 = [r33]` and `(p7) ld4 r8 = [r32]`), and the scheduler may order them either way. | Lua −0.38% (model) |
| 8a8106730a56 | Each conditional branch takes a free scratch pair from p6/p7 to p14/p15, so a chain of fall-through compares (a switch lowered to compares) no longer serializes on WAW of p6/p7. | **coremark −2.36%**, zstd -d −1.37%, bzip2 -c −1.07%, nothing slower (model) |
| e1655de94812 | `(q) mov d = t; x = f(d)` becomes `(¬q) x = f(d); (q) x = f(t)`, one group earlier | bzip2 -c −0.43% (model) |
| 3acdb22bfd66 | Anti-dependence breaking also renames predicated writes | gemm −0.77%, tramp3d −0.70% (model) |

---

## 5. Instruction groups, bundles and issue ports

The compiler marks the end of each instruction group with a stop (`;;`) and
packs instructions into three-slot bundles whose templates fix the unit of
each slot. The hardware issues a group together if the ports allow it.

| commit | change | result |
|---|---|---|
| 8512e8dc4f70 | Only a *taken* branch ends a group, so a group may continue into the fall-through block. Before this, 91% of executed conditional branches in bzip2 were followed by a stop, against 41% for gcc. | bzip2 -c −2.9%, -d −4.0%, Lua −4.1% (model) |
| a6e574fec295 | A `nop.m`/`nop.i`/`nop.f`/`nop.b` occupies a port of its unit, measured on the 9340, so the packer now counts the ports nops take. Split-issue stalls in Lua's `luaV_execute` had gone from 2,003 to 63,685 without it. | **9340: sha −7.39%, aes −2.60%, Lua −1.90%**, zstd -d −1.29%, mst −1.14% |
| d4bfc96a5196 | Shifts, extracts and bit tests can issue on either I port (Tukwila). They are I0-only on Madison, which is modelled separately under `-mtune=madison`. | up to −6.06% (model) |
| 5bc73f73dd7f | Latencies that depend on the consumer (for example a load feeding an address costs more than a load feeding the ALU) | up to −5.98% (model) |
| 36af139a06f1 | Rename FP registers reused within a block, removing false dependences | −4.61% (model) |

`-mtune=madison` (ca5f82980745) selects the Madison model. It differs from
Tukwila in I0-only shifts, a slower L2 bank conflict (10 cycles against 6),
smaller TLB granules and the FP-load banking described in §3.

---

## 6. Post-increment addressing

IA-64 loads and stores have no displacement. They can only add an immediate
to the base register *after* the access, so `a[i+1]` through the pointer for
`a[i]` costs an `add`.

`IA64PostIncPointers` (5302eb0d55c0) runs before the pipeliner. In a
single-block loop, each access at a constant offset from a stepping pointer
gets its own pointer, which the access post-increments itself. A daxpy
unrolled 4× went from 21 to 16 instructions per four elements and no longer
needs rotating GRs.

| workload | cycles | instructions |
|---|---:|---:|
| gemm | −6.27% | −12.87% |
| linpack | −7.74% | −8.73% |
| zstd -d | −0.72% | −0.97% |
| nbody | −0.70% | −3.48% |
| cholesky | +4.13% | −7.85% |

Model, against `-ia64-enable-postinc-pointers=false`.

When this pass first measured, linpack came out *slower*, +3.97%. The model
was charging a post-increment base the load's latency and did not model
rotation. After the model was fixed, the gains above appeared. cholesky's
regression was a separate issue: the packer could not see loop-carried uses
through rotated registers.

---

## 7. Integer and FP arithmetic in the FP unit

IA-64 has no integer or FP divide instruction. `frcpa` gives an 8-bit
reciprocal estimate and sets a predicate saying whether software may refine
it, and `fma` does the refining. `xma` is the only integer multiplier, and
it lives in the FP unit.

### Integer division inline (62a696eb82bd, c5597875145a)

Integer division used to be a call to `__divdi3`. It is now done in line in
the FP unit (`long q(long a, long b) { return a / b; }`):

```asm
q:
	setf.sig	f6 = r33
	setf.sig	f7 = r32 ;;
	fcvt.xf	f7 = f7
	fcvt.xf	f6 = f6 ;;
	frcpa.s1	f8, p8 = f7, f6 ;;
	fnma.s1	f9 = f6, f8, f1 ;;
	fma.s1	f10 = f9, f9, f0
	fma.s1	f8 = f8, f9, f8 ;;
	fma.s1	f8 = f8, f10, f8 ;;
	fma.s1	f9 = f8, f7, f0 ;;
	fnma.s1	f6 = f6, f9, f7 ;;
	fma.s1	f6 = f6, f8, f9 ;;
	fcvt.fx.trunc.s1	f6 = f6 ;;
	getf.sig	r8 = f6
	br.ret.sptk.many	b0 ;;
```

This was verified against the host over 56M 64-bit and 65M 32-bit operand
pairs. mst −11.86% (model).

The division sequences are expanded **before** register allocation
(c5597875145a), on virtual registers with an allocatable predicate class.
Independent divisions now overlap instead of serializing on fixed registers
and `p6`. Model results: spectral −47.32%, tramp3d −7.52%, gemm −5.83%,
jacobi-2d −5.68%, fdtd-2d −4.69%.

### FP division by a constant in three dependent steps (f7876a0d7a2e)

When the divisor's significand fits in 32 bits, `q = a*y; r = a - b*q;
q + r*y` (with `y = 1/b`), done in the 82-bit register format, is correctly
rounded. `frcpa` still runs, but only to catch zeros, infinities and NaNs.
Here is `x / 3.0`:

```asm
third:
	movl	r14 = 4599676419421066581     // 1/3
	movl	r15 = 4613937818241073152 ;;  // 3.0
	setf.d	f6 = r14
	setf.d	f7 = r15 ;;
	fma.s1	f10 = f8, f6, f0              // q = x * (1/3)
	frcpa	f9, p8 = f8, f7 ;;            // special-case filter only
	fnma.s1	f7 = f7, f10, f8              // r = x - 3q
	mov	f8 = f9 ;;
	(p8) fma.d	f8 = f7, f6, f10              // q + r/3, rounded once
	br.ret.sptk.many	b0 ;;
```

The dependence chain is three FMAs instead of seven. It was checked bit for
bit against the host for 12 divisors, in float and double, including
subnormal and near-midpoint operands. seidel-2d −23.41% (model).

### Multiplication without `xma` (a60d2f583bb6)

A multiply through `setf.sig`, `xma.l` and `getf.sig` takes 14 cycles. A
search by iterative deepening finds the shortest chain of `shl`, `shladd`,
subtract and negate for each constant, and uses it when the chain is at
most 12 operations long. Results in the model: gemm −17.14%, mst −9.13%,
sqlite −6.80%, tramp3d −6.76%. This made mst, the one workload where gcc had
been faster, 6.65% faster than gcc.

`x * 10000` becomes six integer operations in four groups:

```asm
m:
	shl	r14 = r32, 4
	shladd	r15 = r32, 2, r32 ;;     // 5x
	sub	r14 = r32, r14            // -15x
	shl	r15 = r15, 7 ;;           // 640x
	add	r14 = r15, r14 ;;         // 625x
	shl	r8 = r14, 4               // 10000x
	br.ret.sptk.many	b0 ;;
```

### 16-bit products with the multimedia multiply (c8d502ee9ebc)

When both operands of a 64-bit multiply are sign-extended halfwords, the
multimedia instruction `pmpy2.r` gives the 32-bit product directly in a
general register, and the FP unit is not involved:

```asm
mul16:                                  // return (long)a * b; a, b short
	pmpy2.r	r14 = r33, r32 ;;
	sxt4	r8 = r14
	br.ret.sptk.many	b0 ;;
```

`pmpy2.r` reads only halfwords 0 and 2 of each operand, so
SimplifyDemandedBits learns that the operands' upper bits are dead, and an
`ld2` feeds it without an `sxt2`. CoreMark's `matrix_mul_const` becomes a
rotating loop of one load, one multiply and one store:

```asm
.LBB0_4:
	(p19) pmpy2.r	r32 = r39, r14        // A[i][j] * val
	(p16) ld2	r36 = [r22], 2
	(p22) st4	[r23] = r35, 4
	br.ctop.sptk.few	.LBB0_4 ;;
```

CoreMark −8.24% cycles and −11.44% instructions (model). No other workload
changed.

---

## 8. Prefetching and branch hints

| commit | change | result |
|---|---|---|
| f41c1e197438, bfe5290cfaef | LoopDataPrefetch emits `lfetch`. `lfetch` is no longer a scheduling barrier: as one, it had made prefetching 1–2.6% *slower*. | bzip2 -c −0.67% (model), +1.4% gain kept on hardware |
| c1839345f586 | No `lfetch` for FP loads, which read L2 and not L1D. cholesky's column loop went from II 6 to II 5. | **9340: cholesky −10.1%** |
| 83a3a2bf5d80 | On Madison, `lfetch.nt1` 800 bytes ahead into L2 for up to two streams per innermost loop. It skips nests that fit in L2, small elements, and streams that restart at the same place on each outer iteration. | **Madison: tramp3d −6.2%, zstd -c −4.2%, -d −3.1%** |
| 617f49a3ac02 | `br.cond` uses the `.many` prefetch hint rather than `.few`. The code is byte-identical apart from the hint, and the model cannot see it. | **Madison: sqlite −1.4%**, CPython lower in 8/8 paired rounds |

The model
predicted that an `lfetch` of a line already in L2 would cost nothing. On
hardware it cost Polybench jacobi-2d 20%. icc 11.1's `-opt-prefetch` makes
LINPACK 24% *slower* on the 9340, which the model also missed.

---

## 9. The register stack

`alloc` gives each function a frame of up to 96 stacked registers, and the
Register Stack Engine spills and fills them to memory behind the program's
back as calls go deeper.

Greedy allocation spreads values over every register it is allowed to use,
so CPython's eval loop took a 92-register frame, against gcc's 55. Nearly
every call it made then spilled and refilled most of that frame. 8902c9659ae1
caps a function that makes calls at 32 stacked locals. Leaf functions and
loops that rotate keep the full frame: capping AES's rounds cost it 1.8%.

- **Madison: CPython −4.0%.** A cap of 24 gave the same, and caps of 40 and
  48 gave −3.3% and −2.6%.
- The cap applies only from register allocation on (2ec0d321096b). Applied
  in SSA form, it made MachineLICM see high pressure and refuse to hoist
  linpack's loop count out of the pivot loop. **9340: linpack −0.92%,
  cholesky −0.72%.**

---

## 10. Case study: CPython

CPython's time is in `_PyEval_EvalFrameDefault`: 189 KB of computed gotos
and indirect calls, with cold `_Py_Dealloc` paths inlined throughout.

| step | Madison hardware |
|---|---:|
| caller-locals cap (8902c9659ae1) | −3.88% |
| indirect-branch expansion | −1.01% |
| fastcc tail calls, `b7`, opcode `ld1` in dispatch, callee GP into `r1` | −4.56% |
| **cumulative, before PGO** | **−9.4%** (1330.6M → 1205.7M) |
| PGO (trained on a separate script) | **−13.8%** further |
| `_Py_Dealloc` entry-point `ld.sa` (§1) | −0.64% |
| `k_mul` recurrence and branch slot (§3) | −0.38% |
| `.many` branch hints (§8) | lower in 8/8 rounds |
| 64-byte function alignment | −1.03% |

Most of the PGO gain is in instruction fetch. Without a profile,
`_PyEval_EvalFrameDefault` spans 46 4 KB pages, more than the 32-entry L1
ITLB, and only 25% of its bundles run 1000 times or more. With PGO,
instruction-fetch stalls fell from 265M to 136M cycles and ITLB misses from
7.76M to 3.04M.

---

## What was tried and rejected

These did not pay off on this machine. They are recorded so that they are
not retried without new information.

- **Shrink-wrapping.** Neutral on CPython, including with PGO.
- **`-mconstant-gp`.** It removed 85% of the gp restores, but those restores
  were filling otherwise empty slots, and the result was +0.62%.
- **Placing `ld.sa` at the immediate dominator** when no better block exists.
  It was a win inside loops but cost 8–10M cycles outside them.
- **Data speculation of every integer load in copies.** Lua's `TValue`
  copies made it +0.37%.
- **Static heuristics for choosing between `br.cloop` and a plain loop.**
  Five shape heuristics were tried and none came out ahead. Only trip-count
  information (a profile) separates the loops.
- **Hot-first `.text.sorted` and call-table `ld.sa` chains.** Flat or worse
  on hardware.
- **A `BufferSize`-style in-order load-store model, and four FP loads per
  group.** Both lost on hardware.

---

## Where the code lives

Everything is under `llvm/lib/Target/IA64/`. The passes that use the
features described here:

| feature | files |
|---|---|
| Data speculation | `IA64SpeculateLoads.cpp` (IA64SpeculateLoads and IA64ExpandSpeculation) |
| Control speculation | `IA64SpeculateChases.cpp`, `ld.s`/`tnat` intrinsics in `IA64ISelLowering.cpp` |
| Loop pipelining and rotation | `IA64RotatingLoops.cpp`, `IA64ShortenRecurrences.cpp`, `IA64MulRecurrence.cpp`, `IA64LoadBanks.cpp`, `IA64InstrInfo.cpp` (`PipelinerLoopInfo`) |
| Predication | `IA64IfConversion.cpp`, `IA64ExclusivePredicates.cpp`, `IA64FoldPredicatedCopies.cpp`, `IA64ExpandBranches.cpp` |
| Groups and bundles | `IA64InsertStops.cpp`, `IA64PackGroups.cpp`, `IA64BreakAntiDeps.cpp` |
| Addressing | `IA64PostIncPointers.cpp`, `IA64WidenIndices.cpp`, `IA64ZextLoopSteps.cpp` |
| Memory | `IA64PrefetchStreams.cpp`, `IA64LoopAliasVersioning.cpp`, `IA64MemmoveVersioning.cpp`, `IA64MergeByteStores.cpp` |
| Register stack | `IA64RegisterInfo.cpp`, `IA64FrameLowering.cpp` |

Lit tests for each transform are in `llvm/test/CodeGen/IA64/`.
