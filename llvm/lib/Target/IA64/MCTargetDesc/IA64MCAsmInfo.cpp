//===-- IA64MCAsmInfo.cpp - IA64 Asm Properties ---------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64MCAsmInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

IA64::Specifier IA64::parseSpecifierName(StringRef Name) {
  return StringSwitch<IA64::Specifier>(Name.lower())
      .Case("gprel", S_GPREL)
      .Case("ltoff", S_LTOFF)
      .Case("ltoffx", S_LTOFFX)
      .Case("pltoff", S_PLTOFF)
      .Case("pcrel", S_PCREL)
      .Case("fptr", S_FPTR)
      .Case("secrel", S_SECREL)
      .Case("segrel", S_SEGREL)
      .Case("ltv", S_LTV)
      .Case("tprel", S_TPREL)
      .Case("dtpmod", S_DTPMOD)
      .Case("dtprel", S_DTPREL)
      .Case("iplt", S_IPLT)
      .Default(S_None);
}

IA64::Specifier IA64::getLtoffSpecifier(Specifier Inner) {
  switch (Inner) {
  case S_FPTR:
    return S_LTOFF_FPTR;
  case S_TPREL:
    return S_LTOFF_TPREL;
  case S_DTPMOD:
    return S_LTOFF_DTPMOD;
  case S_DTPREL:
    return S_LTOFF_DTPREL;
  default:
    return S_None;
  }
}

// Whether Name is Prefix and then a number below Limit, written without
// leading zeros.
static bool isNumbered(StringRef Name, StringRef Prefix, unsigned Limit) {
  unsigned Number;
  if (!Name.consume_front(Prefix) || Name.empty() ||
      (Name.size() > 1 && Name[0] == '0') ||
      !all_of(Name, [](char C) { return isDigit(C); }))
    return false;
  return !Name.getAsInteger(10, Number) && Number < Limit;
}

// An operand or a data directive reads a register name as the register, so a
// symbol of the same name has to say that it is one. This takes every "ar."
// and "cr." name rather than list the ones that exist; a '#' too many does no
// harm. The names of the bits of psr are read as their masks, and so every
// "psr." name is taken as well.
bool IA64MCAsmInfo::needsHashAfterName(StringRef Name) const {
  std::string Lower = Name.lower();
  StringRef N = Lower;
  if (isNumbered(N, "r", 128) || isNumbered(N, "f", 128) ||
      isNumbered(N, "p", 64) || isNumbered(N, "b", 8) ||
      isNumbered(N, "ar", 128) || isNumbered(N, "cr", 128) ||
      isNumbered(N, "in", 96) || isNumbered(N, "loc", 96) ||
      isNumbered(N, "out", 96) || isNumbered(N, "ret", 4) ||
      isNumbered(N, "farg", 8) || isNumbered(N, "fret", 8))
    return true;
  if (N.starts_with("ar.") || N.starts_with("cr.") || N.starts_with("psr."))
    return true;
  return StringSwitch<bool>(N)
      .Cases({"sp", "gp", "tp", "rp", "ip", "pr", "pr.rot"}, true)
      .Case("psr", true)
      .Cases({"rr", "dbr", "ibr", "pkr", "pmc", "pmd"}, true)
      .Cases({"cpuid", "dtr", "itr", "msr"}, true)
      .Default(false);
}

void IA64MCAsmInfo::printSpecifierExpr(raw_ostream &OS,
                                       const MCSpecifierExpr &Expr) const {
  StringRef Open, Close = ")";
  switch (Expr.getSpecifier()) {
  case IA64::S_GPREL:
    Open = "@gprel(";
    break;
  case IA64::S_LTOFF:
    Open = "@ltoff(";
    break;
  case IA64::S_LTOFFX:
    Open = "@ltoffx(";
    break;
  case IA64::S_PLTOFF:
    Open = "@pltoff(";
    break;
  case IA64::S_PCREL:
    Open = "@pcrel(";
    break;
  case IA64::S_FPTR:
    Open = "@fptr(";
    break;
  case IA64::S_SECREL:
    Open = "@secrel(";
    break;
  case IA64::S_SEGREL:
    Open = "@segrel(";
    break;
  case IA64::S_LTV:
    Open = "@ltv(";
    break;
  case IA64::S_TPREL:
    Open = "@tprel(";
    break;
  case IA64::S_DTPMOD:
    Open = "@dtpmod(";
    break;
  case IA64::S_DTPREL:
    Open = "@dtprel(";
    break;
  case IA64::S_IPLT:
    Open = "@iplt(";
    break;
  case IA64::S_LTOFF_FPTR:
    Open = "@ltoff(@fptr(";
    Close = "))";
    break;
  case IA64::S_LTOFF_TPREL:
    Open = "@ltoff(@tprel(";
    Close = "))";
    break;
  case IA64::S_LTOFF_DTPMOD:
    Open = "@ltoff(@dtpmod(";
    Close = "))";
    break;
  case IA64::S_LTOFF_DTPREL:
    Open = "@ltoff(@dtprel(";
    Close = "))";
    break;
  default:
    llvm_unreachable("unknown specifier");
  }
  OS << Open;
  printExpr(OS, *Expr.getSubExpr());
  OS << Close;
}

IA64MCAsmInfo::IA64MCAsmInfo(const Triple &TT, const MCTargetOptions &Options)
    : MCAsmInfoELF(Options) {
  CodePointerSize = 8;
  CalleeSaveStackSlotSize = 8;
  IsLittleEndian = true;
  CommentString = "//";

  // GNU as aligns data2, data4 and data8 to their own width; the .ua
  // spellings do not, which is what sections packing items of mixed width
  // need.
  Data8bitsDirective = "\tdata1\t";
  Data16bitsDirective = "\tdata2.ua\t";
  Data32bitsDirective = "\tdata4.ua\t";
  Data64bitsDirective = "\tdata8.ua\t";

  // GCC writes every symbol as "name#" in case it is called r4.
  AllowHashAfterName = true;

  UsesELFSectionDirectiveForBSS = true;
  SupportsDebugInformation = true;
}
