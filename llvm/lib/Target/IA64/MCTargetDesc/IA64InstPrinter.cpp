//===- IA64InstPrinter.cpp - Convert IA64 MCInst to asm syntax ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64InstPrinter.h"
#include "IA64BaseInfo.h"
#include "IA64MCTargetDesc.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#define PRINT_ALIAS_INSTR
#include "IA64GenAsmWriter.inc"

// A dep.z that reaches the top bit is a shift left by a constant, and an
// extr that reaches the top bit a shift right: such shifts are encoded that
// way and printed as shifts.
bool IA64InstPrinter::printShift(const MCInst *MI, raw_ostream &O) {
  const char *Mnemonic;
  switch (MI->getOpcode()) {
  default:
    return false;
  case IA64::DEP_Z:
    Mnemonic = "shl";
    break;
  case IA64::EXTR:
    Mnemonic = "shr";
    break;
  case IA64::EXTR_U:
    Mnemonic = "shr.u";
    break;
  }
  const MCOperand &Pos = MI->getOperand(2), &Len = MI->getOperand(3);
  if (!Pos.isImm() || !Len.isImm() || Pos.getImm() + Len.getImm() != 64)
    return false;
  O << '\t' << Mnemonic << '\t' << getRegisterName(MI->getOperand(0).getReg())
    << " = " << getRegisterName(MI->getOperand(1).getReg()) << ", "
    << Pos.getImm();
  return true;
}

void IA64InstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                StringRef Annot, const MCSubtargetInfo &STI,
                                raw_ostream &O) {
  // The qualifying predicate is written in front of the mnemonic, and left
  // out when it is p0.
  const MCInstrDesc &Desc = MII.get(MI->getOpcode());
  int QP = Desc.findFirstPredOperandIdx();
  if (QP >= 0 && MI->getOperand(QP).getReg() != IA64::P0) {
    SmallString<64> Buf;
    raw_svector_ostream OS(Buf);
    if (!printShift(MI, OS) && !printAliasInstr(MI, Address, OS))
      printInstruction(MI, Address, OS);
    O << "\t(" << getRegisterName(MI->getOperand(QP).getReg()) << ") "
      << StringRef(Buf).ltrim('\t');
  } else if (!printShift(MI, O) && !printAliasInstr(MI, Address, O)) {
    printInstruction(MI, Address, O);
  }
  if (MI->getFlags() & IA64II::StopAfter)
    O << " ;;";
  printAnnotation(O, Annot);
}

void IA64InstPrinter::printRegName(raw_ostream &O, MCRegister Reg) {
  O << getRegisterName(Reg);
}

void IA64InstPrinter::printOperand(const MCInst *MI, int OpNum,
                                   raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  if (MO.isReg())
    O << getRegisterName(MO.getReg());
  else if (MO.isImm())
    O << MO.getImm();
  else if (MO.isExpr())
    MAI.printExpr(O, *MO.getExpr());
  else
    llvm_unreachable("Invalid operand");
}

void IA64InstPrinter::printHint(const MCInst *MI, int OpNum, raw_ostream &O) {
  O << IA64II::getHintName(MI->getOperand(OpNum).getImm());
}

void IA64InstPrinter::printStatusField(const MCInst *MI, int OpNum,
                                       raw_ostream &O) {
  // The main status field, .s0, is what an instruction without one uses.
  if (int64_t SF = MI->getOperand(OpNum).getImm())
    O << ".s" << SF;
}

void IA64InstPrinter::printMBType(const MCInst *MI, int OpNum, raw_ostream &O) {
  O << '@' << IA64II::getMBTypeName(MI->getOperand(OpNum).getImm());
}

void IA64InstPrinter::printBranchWhether(const MCInst *MI, int OpNum,
                                         raw_ostream &O) {
  O << IA64II::getBranchWhetherName(MI->getOperand(OpNum).getImm());
}

void IA64InstPrinter::printBranchPrefetch(const MCInst *MI, int OpNum,
                                          raw_ostream &O) {
  O << (MI->getOperand(OpNum).getImm() ? ".many" : ".few");
}

void IA64InstPrinter::printBranchDealloc(const MCInst *MI, int OpNum,
                                         raw_ostream &O) {
  if (MI->getOperand(OpNum).getImm())
    O << ".clr";
}

void IA64InstPrinter::printPCRel(const MCInst *MI, uint64_t Address, int OpNum,
                                 raw_ostream &O) {
  const MCOperand &MO = MI->getOperand(OpNum);
  if (!MO.isImm())
    return printOperand(MI, OpNum, O);
  // The displacement is from the bundle that holds the instruction.
  int64_t Disp = MO.getImm();
  if (PrintBranchImmAsAddress)
    O << formatHex(alignDown(Address, IA64II::BundleBytes) + Disp);
  else
    // A long branch reaches all of the address space, so the magnitude
    // may not fit.
    O << '.' << (Disp < 0 ? '-' : '+')
      << (Disp < 0 ? -static_cast<uint64_t>(Disp)
                   : static_cast<uint64_t>(Disp));
}
