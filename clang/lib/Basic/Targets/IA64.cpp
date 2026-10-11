//===--- IA64.cpp - Implement IA-64 target feature support
//-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements IA-64 TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

const char *const IA64TargetInfo::GCCRegNames[] = {
    "r0",     "r1",    "r2",    "r3",     "r4",      "r5",   "r6",   "r7",
    "r8",     "r9",    "r10",   "r11",    "r12",     "r13",  "r14",  "r15",
    "r16",    "r17",   "r18",   "r19",    "r20",     "r21",  "r22",  "r23",
    "r24",    "r25",   "r26",   "r27",    "r28",     "r29",  "r30",  "r31",
    "r32",    "r33",   "r34",   "r35",    "r36",     "r37",  "r38",  "r39",
    "r40",    "r41",   "r42",   "r43",    "r44",     "r45",  "r46",  "r47",
    "r48",    "r49",   "r50",   "r51",    "r52",     "r53",  "r54",  "r55",
    "r56",    "r57",   "r58",   "r59",    "r60",     "r61",  "r62",  "r63",
    "r64",    "r65",   "r66",   "r67",    "r68",     "r69",  "r70",  "r71",
    "r72",    "r73",   "r74",   "r75",    "r76",     "r77",  "r78",  "r79",
    "r80",    "r81",   "r82",   "r83",    "r84",     "r85",  "r86",  "r87",
    "r88",    "r89",   "r90",   "r91",    "r92",     "r93",  "r94",  "r95",
    "r96",    "r97",   "r98",   "r99",    "r100",    "r101", "r102", "r103",
    "r104",   "r105",  "r106",  "r107",   "r108",    "r109", "r110", "r111",
    "r112",   "r113",  "r114",  "r115",   "r116",    "r117", "r118", "r119",
    "r120",   "r121",  "r122",  "r123",   "r124",    "r125", "r126", "r127",
    "f0",     "f1",    "f2",    "f3",     "f4",      "f5",   "f6",   "f7",
    "f8",     "f9",    "f10",   "f11",    "f12",     "f13",  "f14",  "f15",
    "f16",    "f17",   "f18",   "f19",    "f20",     "f21",  "f22",  "f23",
    "f24",    "f25",   "f26",   "f27",    "f28",     "f29",  "f30",  "f31",
    "f32",    "f33",   "f34",   "f35",    "f36",     "f37",  "f38",  "f39",
    "f40",    "f41",   "f42",   "f43",    "f44",     "f45",  "f46",  "f47",
    "f48",    "f49",   "f50",   "f51",    "f52",     "f53",  "f54",  "f55",
    "f56",    "f57",   "f58",   "f59",    "f60",     "f61",  "f62",  "f63",
    "f64",    "f65",   "f66",   "f67",    "f68",     "f69",  "f70",  "f71",
    "f72",    "f73",   "f74",   "f75",    "f76",     "f77",  "f78",  "f79",
    "f80",    "f81",   "f82",   "f83",    "f84",     "f85",  "f86",  "f87",
    "f88",    "f89",   "f90",   "f91",    "f92",     "f93",  "f94",  "f95",
    "f96",    "f97",   "f98",   "f99",    "f100",    "f101", "f102", "f103",
    "f104",   "f105",  "f106",  "f107",   "f108",    "f109", "f110", "f111",
    "f112",   "f113",  "f114",  "f115",   "f116",    "f117", "f118", "f119",
    "f120",   "f121",  "f122",  "f123",   "f124",    "f125", "f126", "f127",
    "p0",     "p1",    "p2",    "p3",     "p4",      "p5",   "p6",   "p7",
    "p8",     "p9",    "p10",   "p11",    "p12",     "p13",  "p14",  "p15",
    "p16",    "p17",   "p18",   "p19",    "p20",     "p21",  "p22",  "p23",
    "p24",    "p25",   "p26",   "p27",    "p28",     "p29",  "p30",  "p31",
    "p32",    "p33",   "p34",   "p35",    "p36",     "p37",  "p38",  "p39",
    "p40",    "p41",   "p42",   "p43",    "p44",     "p45",  "p46",  "p47",
    "p48",    "p49",   "p50",   "p51",    "p52",     "p53",  "p54",  "p55",
    "p56",    "p57",   "p58",   "p59",    "p60",     "p61",  "p62",  "p63",
    "b0",     "b1",    "b2",    "b3",     "b4",      "b5",   "b6",   "b7",
    "ar.pfs", "ar.lc", "ar.ec", "ar.ccv", "ar.unat",
};

// The names the software conventions give some of the registers. The local
// and the output registers have names too, but which registers those are is
// for each function to say.
const TargetInfo::GCCRegAlias IA64TargetInfo::GCCRegAliases[] = {
    {{"gp"}, "r1"},   {{"sp"}, "r12"},  {{"tp"}, "r13"},  {{"rp"}, "b0"},
    {{"in0"}, "r32"}, {{"in1"}, "r33"}, {{"in2"}, "r34"}, {{"in3"}, "r35"},
    {{"in4"}, "r36"}, {{"in5"}, "r37"}, {{"in6"}, "r38"}, {{"in7"}, "r39"},
};

ArrayRef<const char *> IA64TargetInfo::getGCCRegNames() const {
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> IA64TargetInfo::getGCCRegAliases() const {
  return llvm::ArrayRef(GCCRegAliases);
}

bool IA64TargetInfo::validateAsmConstraint(
    const char *&Name, TargetInfo::ConstraintInfo &Info) const {
  switch (*Name) {
  default:
    return false;
  case 'a': // r0-r3, the source registers addl accepts.
  case 'b': // A branch register.
  case 'f': // A floating-point register.
    Info.setAllowsRegister();
    return true;
  case 'I': // 14-bit signed immediate (adds).
    Info.setRequiresImmediate(-8192, 8191);
    return true;
  case 'J': // 22-bit signed immediate (addl).
    Info.setRequiresImmediate(-2097152, 2097151);
    return true;
  case 'K': // 8-bit signed immediate (logical instructions).
    Info.setRequiresImmediate(-128, 127);
    return true;
  case 'L': // 8-bit signed immediate for a compare, after subtracting one.
    Info.setRequiresImmediate(-127, 128);
    return true;
  case 'M': // A shift count.
    Info.setRequiresImmediate(0, 63);
    return true;
  case 'N': // 9-bit signed load/store base-update immediate.
    Info.setRequiresImmediate(-256, 255);
    return true;
  case 'O':
    Info.setRequiresImmediate(0);
    return true;
  case 'P': // Immediate source of dep.
    Info.setRequiresImmediate(-1, 0);
    return true;
  case 'R': // shladd shift count.
    Info.setRequiresImmediate(1, 4);
    return true;
  case 'Q': // Non-volatile memory.
  case 'S': // Memory without base-register update.
    Info.setAllowsMemory();
    return true;
  }
}

static constexpr llvm::StringLiteral ValidCPUNames[] = {
    {"generic"}, {"itanium"}, {"itanium2"}};

bool IA64TargetInfo::isValidCPUName(StringRef Name) const {
  return llvm::is_contained(ValidCPUNames, Name);
}

void IA64TargetInfo::fillValidCPUList(
    SmallVectorImpl<StringRef> &Values) const {
  Values.append(std::begin(ValidCPUNames), std::end(ValidCPUNames));
}

void IA64TargetInfo::getTargetDefines(const LangOptions &Opts,
                                      MacroBuilder &Builder) const {
  Builder.defineMacro("__ia64");
  Builder.defineMacro("__ia64__");
  Builder.defineMacro("__REGISTER_PREFIX__", "");
  Builder.defineMacro("__itanium__");
  Builder.defineMacro("_LP64");
  Builder.defineMacro("__LP64__");
  Builder.defineMacro("_LONGLONG");
  Builder.defineMacro("__SIZEOF_FLOAT128__", "16");

  // fma is a single instruction in every precision.

  Builder.defineMacro("__FP_FAST_FMA", "1");
  Builder.defineMacro("__FP_FAST_FMAF", "1");
  Builder.defineMacro("__FP_FAST_FMAL", "1");

  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_1");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_2");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_4");
  Builder.defineMacro("__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8");
}
