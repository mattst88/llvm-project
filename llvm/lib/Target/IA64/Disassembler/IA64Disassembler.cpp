//===-- IA64Disassembler.cpp - Disassembler for IA64 ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the IA64Disassembler class.
//
// IA-64 code is a sequence of 16-byte bundles of three 41-bit slots, and a
// slot has no address of its own. The disassembler follows the convention of
// GNU objdump and of the slot number in a relocation's offset: the slots of
// the bundle at address A are instructions of six, six and four bytes at A,
// A + 6 and A + 12.
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDecoder.h"
#include "llvm/MC/MCDecoderOps.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;
using namespace llvm::MCD;

#define DEBUG_TYPE "ia64-disassembler"

typedef MCDisassembler::DecodeStatus DecodeStatus;

namespace {

class IA64Disassembler : public MCDisassembler {
  // The bundle being decoded. Slot 0 is decoded with the whole bundle; slots
  // 1 and 2 see only its tail, so the bundle is cached.
  mutable uint64_t BundleAddress = ~uint64_t(0);
  mutable uint64_t BundleLo = 0;
  mutable uint64_t BundleHi = 0;

  uint64_t getSlot(unsigned N) const;

public:
  /// The L slot of the MLX bundle being decoded, for the X-unit operands.
  uint64_t getLongSlot() const { return getSlot(1); }

  IA64Disassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &MI, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // end anonymous namespace

static MCDisassembler *createIA64Disassembler(const Target &T,
                                              const MCSubtargetInfo &STI,
                                              MCContext &Ctx) {
  return new IA64Disassembler(STI, Ctx);
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64Disassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheIA64Target(),
                                         createIA64Disassembler);
}

template <unsigned First, unsigned Count>
static DecodeStatus decodeRegister(MCInst &MI, uint64_t RegNo) {
  if (RegNo >= Count)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createReg(First + RegNo));
  return MCDisassembler::Success;
}

static DecodeStatus DecodeGRRegisterClass(MCInst &MI, uint64_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  return decodeRegister<IA64::R0, 128>(MI, RegNo);
}

static DecodeStatus DecodeGR0_3RegisterClass(MCInst &MI, uint64_t RegNo,
                                             uint64_t Address,
                                             const MCDisassembler *Decoder) {
  return decodeRegister<IA64::R0, 4>(MI, RegNo);
}

static DecodeStatus DecodePRRegisterClass(MCInst &MI, uint64_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  return decodeRegister<IA64::P0, 64>(MI, RegNo);
}

static DecodeStatus DecodeBRRegisterClass(MCInst &MI, uint64_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  return decodeRegister<IA64::B0, 8>(MI, RegNo);
}

static DecodeStatus DecodeARRegisterClass(MCInst &MI, uint64_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  return decodeRegister<IA64::AR0, 128>(MI, RegNo);
}

static DecodeStatus DecodeCRRegisterClass(MCInst &MI, uint64_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  return decodeRegister<IA64::CR0, 128>(MI, RegNo);
}

static DecodeStatus DecodeFRRegisterClass(MCInst &MI, uint64_t RegNo,
                                          uint64_t Address,
                                          const MCDisassembler *Decoder) {
  return decodeRegister<IA64::F0, 128>(MI, RegNo);
}

template <unsigned N>
static DecodeStatus decodeSImm(MCInst &MI, uint64_t Imm, uint64_t Address,
                               const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(SignExtend64<N>(Imm)));
  return MCDisassembler::Success;
}

static DecodeStatus decodeImmPlus1(MCInst &MI, uint64_t Imm, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm + 1));
  return MCDisassembler::Success;
}

template <int64_t N>
static DecodeStatus decodeReversedImm(MCInst &MI, uint64_t Imm,
                                      uint64_t Address,
                                      const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(N - static_cast<int64_t>(Imm)));
  return MCDisassembler::Success;
}

static DecodeStatus decodePmpyshrCount(MCInst &MI, uint64_t Imm,
                                       uint64_t Address,
                                       const MCDisassembler *Decoder) {
  static const uint8_t Counts[] = {0, 7, 15, 16};
  MI.addOperand(MCOperand::createImm(Counts[Imm & 3]));
  return MCDisassembler::Success;
}

static DecodeStatus decodeMBType(MCInst &MI, uint64_t Imm, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  if (!IA64II::getMBTypeName(Imm))
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeFeatureBit(MCInst &MI, uint64_t Imm, uint64_t Address,
                                     const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm + 32));
  return MCDisassembler::Success;
}

/// An IP-relative displacement of N bits, counted in bundles.
template <unsigned N>
static DecodeStatus decodePCRel(MCInst &MI, uint64_t Imm, uint64_t Address,
                                const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(SignExtend64<N>(Imm) * 16));
  return MCDisassembler::Success;
}

/// The X-unit operands. Imm holds the X slot's bits in place; the rest are
/// in the bundle's L slot.
static uint64_t getLongSlot(const MCDisassembler *Decoder) {
  return static_cast<const IA64Disassembler *>(Decoder)->getLongSlot();
}

static DecodeStatus decodeImm64(MCInst &MI, uint64_t Imm, uint64_t Address,
                                const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm | getLongSlot(Decoder) << 22));
  return MCDisassembler::Success;
}

static DecodeStatus decodeUImm62(MCInst &MI, uint64_t Imm, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm | getLongSlot(Decoder) << 21));
  return MCDisassembler::Success;
}

static DecodeStatus decodeLongPCRel(MCInst &MI, uint64_t Imm, uint64_t Address,
                                    const MCDisassembler *Decoder) {
  // The L slot holds bits 58:20 of the displacement above two unused bits.
  // Scaling the 60-bit bundle displacement to bytes fills all 64 bits.
  Imm |= (getLongSlot(Decoder) >> 2) << 20;
  MI.addOperand(MCOperand::createImm(static_cast<int64_t>(Imm << 4)));
  return MCDisassembler::Success;
}

/// The locality hint of an access that does not update its base; any hint.
static DecodeStatus decodeDataHint(MCInst &MI, uint64_t Imm, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

/// The stride of a counted prefetch: five signed bits, in units of 64.
static DecodeStatus decodeStride(MCInst &MI, uint64_t Imm, uint64_t Address,
                                 const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(SignExtend64<5>(Imm) * 64));
  return MCDisassembler::Success;
}

/// The locality hint of a base-updating load, which cannot be .nt2.
static DecodeStatus decodeLoadHint(MCInst &MI, uint64_t Imm, uint64_t Address,
                                   const MCDisassembler *Decoder) {
  if (Imm == 2)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

/// The locality hint of a store, which can only be .nta.
static DecodeStatus decodeStoreHint(MCInst &MI, uint64_t Imm, uint64_t Address,
                                    const MCDisassembler *Decoder) {
  if (Imm != 0 && Imm != 3)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

static DecodeStatus decodeFetchHint(MCInst &MI, uint64_t Imm, uint64_t Address,
                                    const MCDisassembler *Decoder) {
  MI.addOperand(MCOperand::createImm(Imm));
  return MCDisassembler::Success;
}

/// The increment of fetchadd: a sign, and 16, 8, 4 or 1.
static DecodeStatus decodeInc3(MCInst &MI, uint64_t Imm, uint64_t Address,
                               const MCDisassembler *Decoder) {
  static const int64_t Magnitude[] = {16, 8, 4, 1};
  int64_t Inc = Magnitude[Imm & 3];
  MI.addOperand(MCOperand::createImm(Imm & 4 ? -Inc : Inc));
  return MCDisassembler::Success;
}

/// alloc encodes the frame size, the size of its local part and the number
/// of rotating registers in units of eight. The local part covers inputs and
/// locals, which the encoding does not distinguish; it is decoded as locals.
static DecodeStatus decodeAlloc(MCInst &MI, uint64_t Insn, uint64_t Address,
                                const MCDisassembler *Decoder) {
  uint64_t Sof = (Insn >> 13) & 0x7f;
  uint64_t Sol = (Insn >> 20) & 0x7f;
  uint64_t Rotating = ((Insn >> 27) & 0xf) * 8;
  if (Sof > IA64II::MaxFrameSize || Sol > Sof || Rotating > Sof)
    return MCDisassembler::Fail;
  // A qualifying predicate other than p0 is an illegal operation.
  if ((Insn & 0x3f) != 0)
    return MCDisassembler::Fail;
  if (DecodeGRRegisterClass(MI, (Insn >> 6) & 0x7f, Address, Decoder) ==
      MCDisassembler::Fail)
    return MCDisassembler::Fail;
  MI.addOperand(MCOperand::createImm(0));
  MI.addOperand(MCOperand::createImm(Sol));
  MI.addOperand(MCOperand::createImm(Sof - Sol));
  MI.addOperand(MCOperand::createImm(Rotating));
  return DecodePRRegisterClass(MI, Insn & 0x3f, Address, Decoder);
}

#include "IA64GenDisassemblerTables.inc"

uint64_t IA64Disassembler::getSlot(unsigned N) const {
  // The template takes bits 4:0, and slot N the 41 bits from 5 + 41 * N.
  switch (N) {
  case 0:
    return (BundleLo >> 5) & IA64II::SlotMask;
  case 1:
    return ((BundleLo >> 46) | (BundleHi << 18)) & IA64II::SlotMask;
  default:
    return BundleHi >> 23;
  }
}

DecodeStatus IA64Disassembler::getInstruction(MCInst &MI, uint64_t &Size,
                                              ArrayRef<uint8_t> Bytes,
                                              uint64_t Address,
                                              raw_ostream &CS) const {
  unsigned SlotNo;
  switch (Address % IA64II::BundleBytes) {
  case 0:
    SlotNo = 0;
    Size = 6;
    break;
  case 6:
    SlotNo = 1;
    Size = 6;
    break;
  case 12:
    SlotNo = 2;
    Size = 4;
    break;
  default:
    Size = 1;
    return Fail;
  }

  uint64_t Base = Address - Address % IA64II::BundleBytes;
  if (SlotNo == 0) {
    if (Bytes.size() < IA64II::BundleBytes) {
      Size = Bytes.size();
      return Fail;
    }
    BundleAddress = Base;
    BundleLo = support::endian::read64le(Bytes.data());
    BundleHi = support::endian::read64le(Bytes.data() + 8);
  } else if (BundleAddress != Base) {
    // Decoding did not start at this bundle's first byte.
    Size = std::min<uint64_t>(Size, Bytes.size());
    return Fail;
  }

  unsigned Template = BundleLo & 0x1f;
  const IA64II::BundleTemplate &T = IA64II::BundleTemplates[Template >> 1];
  if (!T.Name)
    return Fail;
  if (SlotNo == 0)
    CS << '[' << T.Name << ']';

  uint64_t Insn = getSlot(SlotNo);
  DecodeStatus Result = Fail;
  switch (T.Slots[SlotNo]) {
  case IA64II::UnitL:
    // The L and X slots are one instruction, decoded from the X slot.
    Insn = getSlot(2);
    SlotNo = 2;
    Size = 10;
    Result = decodeInstruction(DecoderTableX48, MI, Insn, Address, this, STI);
    break;
  case IA64II::UnitI:
    Result = decodeInstruction(DecoderTableI48, MI, Insn, Address, this, STI);
    if (Result == Fail)
      Result = decodeInstruction(DecoderTableA48, MI, Insn, Address, this, STI);
    break;
  case IA64II::UnitM:
    Result = decodeInstruction(DecoderTableM48, MI, Insn, Address, this, STI);
    if (Result == Fail)
      Result = decodeInstruction(DecoderTableA48, MI, Insn, Address, this, STI);
    break;
  case IA64II::UnitB:
    Result = decodeInstruction(DecoderTableB48, MI, Insn, Address, this, STI);
    break;
  case IA64II::UnitF:
    Result = decodeInstruction(DecoderTableF48, MI, Insn, Address, this, STI);
    break;
  default:
    break;
  }
  if (Result == Fail)
    return Fail;

  if (T.MidStop == static_cast<int>(SlotNo) || (SlotNo == 2 && (Template & 1)))
    MI.setFlags(MI.getFlags() | IA64II::StopAfter);
  return Result;
}
