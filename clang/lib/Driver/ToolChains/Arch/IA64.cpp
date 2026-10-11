//===--- IA64.cpp - IA64 Helpers for Tools ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "clang/Driver/Driver.h"
#include "clang/Options/Options.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Option/ArgList.h"

using namespace clang::driver;
using namespace clang::driver::tools;
using namespace clang;
using namespace llvm::opt;

// The registers that can be kept from the compiler: the static general
// registers but the two that are constant, and the floating-point registers
// but the two that are constant.
static bool parseRegister(StringRef Name, char &Kind, unsigned &Number) {
  if (Name.size() < 2 || (Name[0] != 'r' && Name[0] != 'f'))
    return false;
  Kind = Name[0];
  if (!llvm::all_of(Name.substr(1), llvm::isDigit) ||
      Name.substr(1).getAsInteger(10, Number))
    return false;
  return Number >= 2 && Number <= (Kind == 'r' ? 31u : 127u);
}

// -mfixed-range=f12-f15,f32-f127 names ranges of registers, each written as
// its first and its last.
static void addFixedRanges(const Driver &D, const ArgList &Args, const Arg *A,
                           std::vector<StringRef> &Features) {
  SmallVector<StringRef, 4> Ranges;
  StringRef(A->getValue()).split(Ranges, ',');
  for (StringRef Range : Ranges) {
    auto [First, Last] = Range.split('-');
    char FirstKind, LastKind;
    unsigned FirstNumber, LastNumber;
    if (!parseRegister(First, FirstKind, FirstNumber) ||
        !parseRegister(Last, LastKind, LastNumber) || FirstKind != LastKind ||
        FirstNumber > LastNumber) {
      D.Diag(diag::err_drv_invalid_value) << A->getSpelling() << Range;
      continue;
    }
    for (unsigned I = FirstNumber; I <= LastNumber; ++I)
      Features.push_back(
          Args.MakeArgString("+reserve-" + Twine(FirstKind) + Twine(I)));
  }
}

void ia64::getIA64TargetFeatures(const Driver &D, const llvm::Triple &Triple,
                                 const ArgList &Args,
                                 std::vector<StringRef> &Features) {
  static const struct {
    options::ID Option;
    const char *Feature;
  } FixedRegisters[] = {
      {options::OPT_ffixed_r4, "+reserve-r4"},
      {options::OPT_ffixed_r5, "+reserve-r5"},
      {options::OPT_ffixed_r6, "+reserve-r6"},
      {options::OPT_ffixed_r7, "+reserve-r7"},
      {options::OPT_ffixed_r8, "+reserve-r8"},
      {options::OPT_ffixed_r9, "+reserve-r9"},
      {options::OPT_ffixed_r10, "+reserve-r10"},
      {options::OPT_ffixed_r11, "+reserve-r11"},
      {options::OPT_ffixed_r12, "+reserve-r12"},
      {options::OPT_ffixed_r13, "+reserve-r13"},
      {options::OPT_ffixed_r14, "+reserve-r14"},
      {options::OPT_ffixed_r15, "+reserve-r15"},
      {options::OPT_ffixed_r16, "+reserve-r16"},
      {options::OPT_ffixed_r17, "+reserve-r17"},
      {options::OPT_ffixed_r18, "+reserve-r18"},
      {options::OPT_ffixed_r19, "+reserve-r19"},
      {options::OPT_ffixed_r20, "+reserve-r20"},
      {options::OPT_ffixed_r21, "+reserve-r21"},
      {options::OPT_ffixed_r22, "+reserve-r22"},
      {options::OPT_ffixed_r23, "+reserve-r23"},
      {options::OPT_ffixed_r24, "+reserve-r24"},
      {options::OPT_ffixed_r25, "+reserve-r25"},
      {options::OPT_ffixed_r26, "+reserve-r26"},
      {options::OPT_ffixed_r27, "+reserve-r27"},
      {options::OPT_ffixed_r28, "+reserve-r28"},
      {options::OPT_ffixed_r29, "+reserve-r29"},
      {options::OPT_ffixed_r30, "+reserve-r30"},
      {options::OPT_ffixed_r31, "+reserve-r31"},
  };
  for (const auto &R : FixedRegisters)
    if (Args.hasArg(R.Option))
      Features.push_back(R.Feature);

  if (Args.hasArg(options::OPT_mconstant_gp))
    Features.push_back("+constant-gp");

  for (const Arg *A : Args.filtered(options::OPT_mfixed_range_EQ)) {
    A->claim();
    addFixedRanges(D, Args, A, Features);
  }
}
