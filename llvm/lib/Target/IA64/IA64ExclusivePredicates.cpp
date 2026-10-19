//===-- IA64ExclusivePredicates.cpp - Predicates that exclude each other --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64ExclusivePredicates.h"
#include "IA64InstrInfo.h"
#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"

using namespace llvm;

// Compares that write the complement of their first target to the second.
// The .unc forms write both even under a false qualifying predicate,
// clearing them.
static bool isExclusiveCompare(unsigned Opcode, bool &Unc) {
  switch (Opcode) {
  default:
    return false;
  case IA64::CMP_LT:
  case IA64::CMP_LTi:
  case IA64::CMP_LTU:
  case IA64::CMP_LTUi:
  case IA64::CMP_EQ:
  case IA64::CMP_EQi:
  case IA64::CMP4_LT:
  case IA64::CMP4_LTi:
  case IA64::CMP4_LTU:
  case IA64::CMP4_LTUi:
  case IA64::CMP4_EQ:
  case IA64::CMP4_EQi:
  case IA64::TBIT_Z:
  case IA64::TNAT_Z:
  case IA64::FCMP_EQ:
  case IA64::FCMP_LT:
  case IA64::FCMP_LE:
  case IA64::FCMP_UNORD:
  case IA64::FCLASS_M:
    Unc = false;
    return true;
  case IA64::CMP_LT_UNC:
  case IA64::CMP_LT_UNCi:
  case IA64::CMP_LTU_UNC:
  case IA64::CMP_LTU_UNCi:
  case IA64::CMP_EQ_UNC:
  case IA64::CMP_EQ_UNCi:
  case IA64::CMP4_LT_UNC:
  case IA64::CMP4_LT_UNCi:
  case IA64::CMP4_LTU_UNC:
  case IA64::CMP4_LTU_UNCi:
  case IA64::CMP4_EQ_UNC:
  case IA64::CMP4_EQ_UNCi:
  case IA64::TBIT_Z_UNC:
  case IA64::TNAT_Z_UNC:
  case IA64::FCMP_EQ_UNC:
  case IA64::FCMP_LT_UNC:
  case IA64::FCMP_LE_UNC:
  case IA64::FCMP_UNORD_UNC:
  case IA64::FCLASS_M_UNC:
    Unc = true;
    return true;
  }
}

IA64ExclusivePredicates::IA64ExclusivePredicates(const TargetRegisterInfo &TRI)
    : Entries(TRI.getNumRegs()) {}

void IA64ExclusivePredicates::clear() {
  std::fill(Entries.begin(), Entries.end(), Entry());
}

void IA64ExclusivePredicates::step(const MachineInstr &MI) {
  // These write predicates without naming them.
  if (MI.isCall() || MI.isInlineAsm() || MI.getOpcode() == IA64::MOV_PR ||
      MI.getOpcode() == IA64::MOV_PR_ROT) {
    clear();
    return;
  }
  for (const MachineOperand &MO : MI.operands()) {
    if (!MO.isReg() || !MO.isDef() || !MO.getReg().isPhysical())
      continue;
    Entry &E = Entries[MO.getReg().id()];
    if (E.Partner)
      Entries[E.Partner.id()] = Entry();
    E = Entry();
  }
  // A compare under a false predicate other than .unc writes nothing.
  bool Unc;
  if (!isExclusiveCompare(MI.getOpcode(), Unc))
    return;
  int QPIdx = MI.findFirstPredOperandIdx();
  if (!Unc && QPIdx >= 0 && MI.getOperand(QPIdx).getReg() != IA64::P0)
    return;
  MCRegister P1 = MI.getOperand(0).getReg(), P2 = MI.getOperand(1).getReg();
  if (P1 == IA64::P0 || P2 == IA64::P0 || P1 == P2)
    return;
  ++NumCompares;
  Entries[P1.id()] = {P2, NumCompares};
  Entries[P2.id()] = {P1, NumCompares};
}

MCRegister
IA64ExclusivePredicates::qualifyingPredicate(const MachineInstr &MI) {
  bool Unc;
  int QPIdx = MI.findFirstPredOperandIdx();
  if (QPIdx < 0 || MI.isCall() || MI.isBranch() || MI.isReturn() ||
      MI.isInlineAsm() ||
      IA64II::getUnit(MI.getDesc().TSFlags) == IA64II::UnitB ||
      (isExclusiveCompare(MI.getOpcode(), Unc) && Unc))
    return IA64::P0;
  return MI.getOperand(QPIdx).getReg();
}
