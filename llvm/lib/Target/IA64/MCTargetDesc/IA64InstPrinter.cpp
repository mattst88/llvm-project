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

#include "IA64GenAsmWriter.inc"

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
    printInstruction(MI, Address, OS);
    O << "\t(" << getRegisterName(MI->getOperand(QP).getReg()) << ") "
      << StringRef(Buf).ltrim('\t');
  } else {
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

void IA64InstPrinter::printMBType(const MCInst *MI, int OpNum, raw_ostream &O) {
  O << '@' << IA64II::getMBTypeName(MI->getOperand(OpNum).getImm());
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
    O << '.' << (Disp < 0 ? '-' : '+') << (Disp < 0 ? -Disp : Disp);
}
