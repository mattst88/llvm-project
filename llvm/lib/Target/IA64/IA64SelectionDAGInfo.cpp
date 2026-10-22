//===-- IA64SelectionDAGInfo.cpp - IA64 SelectionDAG Info -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IA64SelectionDAGInfo.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/IR/DataLayout.h"

#define GET_SDNODE_DESC
#include "IA64GenSDNodeInfo.inc"

using namespace llvm;

IA64SelectionDAGInfo::IA64SelectionDAGInfo()
    : SelectionDAGGenTargetInfo(IA64GenSDNodeInfo) {}

IA64SelectionDAGInfo::~IA64SelectionDAGInfo() = default;

// A copy of 8n bytes stores a byte, a halfword and a word at the first
// addresses aligned for each, every aligned doubleword, then a word, a halfword
// and a byte ending at the last aligned addresses, leaving out the pieces
// smaller than the destination's alignment. Each store holds the bytes that
// belong at its own address, so the stores may overlap. The generic expansion
// moves pieces as large as the smaller alignment, so it copies a byte at a time
// from a source of unknown alignment.
bool IA64SelectionDAGInfo::expandsCopy(uint64_t Size, Align DstAlign,
                                       Align SrcAlign) {
  if (Size < 8 || Size > 64 || Size % 8)
    return false;
  uint64_t A = std::min(DstAlign.value(), uint64_t(8));
  uint64_t Pieces = Size / std::min(A, SrcAlign.value());
  uint64_t Stores = Size / 8;
  if (A < 8)
    Stores += Size == 8 ? 2 * Log2_64(8 / A) - 1 : 2 * Log2_64(8 / A);
  return Stores < Pieces;
}

namespace {
// Address and shift arithmetic on doublewords.
struct Arith {
  SelectionDAG &DAG;
  const SDLoc &DL;

  SDValue imm(int64_t V) const { return DAG.getSignedConstant(V, DL, MVT::i64); }
  SDValue op(unsigned Opc, SDValue A, SDValue B) const {
    return DAG.getNode(Opc, DL, MVT::i64, A, B);
  }
  SDValue add(SDValue A, int64_t V) const { return op(ISD::ADD, A, imm(V)); }
  SDValue alignDown(SDValue A, int64_t V) const {
    return op(ISD::AND, A, imm(-V));
  }
  // The bits in the bytes of A below a multiple of M.
  SDValue bitsBelow(SDValue A, int64_t M) const {
    return op(ISD::SHL, op(ISD::AND, A, imm(M - 1)), imm(3));
  }
  SDValue srl(SDValue V, SDValue Amt) const { return op(ISD::SRL, V, Amt); }
  // V >> Amt | Next << (64 - Amt), which is V when Amt is 0.
  SDValue funnel(SDValue V, SDValue Next, SDValue Amt) const {
    return op(ISD::OR, srl(V, Amt),
              op(IA64ISD::SHL_WIDE, Next, op(ISD::SUB, imm(64), Amt)));
  }
};
} // end anonymous namespace

SDValue IA64SelectionDAGInfo::emitStores(SelectionDAG &DAG, const SDLoc &DL,
                                         SDValue Chain, SDValue Dst,
                                         ArrayRef<SDValue> V, Align DstAlign,
                                         MachinePointerInfo DstPtrInfo,
                                         MachineMemOperand::Flags Flags,
                                         const AAMDNodes &AAInfo) {
  Arith A{DAG, DL};
  uint64_t Words = V.size(), N = 8 * Words;
  SmallVector<SDValue, 14> Stores;
  if (DstAlign >= Align(8)) {
    for (uint64_t I = 0; I != Words; ++I)
      Stores.push_back(DAG.getStore(Chain, DL, V[I], A.add(Dst, 8 * I),
                                    DstPtrInfo.getWithOffset(8 * I), DstAlign,
                                    Flags, AAInfo));
    return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
  }

  MachinePointerInfo Info(DstPtrInfo.getAddrSpace());
  auto Store = [&](SDValue Addr, SDValue Val, unsigned Bytes) {
    Stores.push_back(DAG.getTruncStore(Chain, DL, Val, Addr, Info,
                                       MVT::getIntegerVT(8 * Bytes),
                                       Align(Bytes), Flags, AAInfo));
  };
  // The first byte, halfword and word, from V[0]. Each starts as many bytes
  // into it as -Dst has below its size.
  SDValue NegDst = A.op(ISD::SUB, A.imm(0), Dst);
  if (DstAlign == Align(1))
    Store(Dst, V[0], 1);
  for (unsigned Bytes : {2, 4}) {
    if (Align(Bytes) < DstAlign)
      continue;
    Store(A.alignDown(A.add(Dst, Bytes - 1), Bytes),
          A.srl(V[0], A.bitsBelow(NegDst, Bytes)), Bytes);
  }
  // Doublewords starting K bytes into V[I], for K the bytes up to the first
  // aligned address.
  SDValue First = A.alignDown(A.add(Dst, 7), 8);
  SDValue K = A.bitsBelow(NegDst, 8);
  for (uint64_t I = 0; I + 1 < Words; ++I)
    Store(A.add(First, 8 * I), A.funnel(V[I], V[I + 1], K), 8);
  // The last aligned doubleword starts 8 - T bytes into V[n-2], for T the
  // destination's misalignment, which is all of V[n-1] when T is 0. It
  // repeats the store above otherwise.
  if (Words >= 2) {
    SDValue T = A.bitsBelow(Dst, 8);
    SDValue Lo = A.srl(A.srl(V[Words - 2], A.imm(8)),
                       A.op(ISD::SUB, A.imm(56), T));
    SDValue Hi = A.op(ISD::SHL, V[Words - 1], T);
    Store(A.add(A.alignDown(Dst, 8), N - 8), A.op(ISD::OR, Lo, Hi), 8);
  }
  // The last word, halfword and byte, from V[n-1]. Each ends as many bytes
  // before its end as Dst has below its size.
  for (unsigned Bytes : {4, 2}) {
    if (Align(Bytes) < DstAlign)
      continue;
    Store(A.alignDown(A.add(Dst, N - Bytes), Bytes),
          A.srl(V[Words - 1], A.op(ISD::SUB, A.imm(64 - 8 * Bytes),
                                   A.bitsBelow(Dst, Bytes))),
          Bytes);
  }
  if (DstAlign == Align(1))
    Store(A.add(Dst, N - 1), A.srl(V[Words - 1], A.imm(56)), 1);
  return DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Stores);
}

// Read the source as doublewords V[0..n), aligned or shifted together from
// the aligned doublewords holding them, then store them as expandsCopy
// describes. Every load comes before every store, so this also moves.
static SDValue emitCopy(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain,
                        SDValue Dst, SDValue Src, SDValue Size, Align DstAlign,
                        Align SrcAlign, bool IsVolatile,
                        MachinePointerInfo DstPtrInfo,
                        MachinePointerInfo SrcPtrInfo) {
  auto *C = dyn_cast<ConstantSDNode>(Size);
  if (!C || IsVolatile || !DAG.getDataLayout().isLittleEndian() ||
      !IA64SelectionDAGInfo::expandsCopy(C->getZExtValue(), DstAlign,
                                         SrcAlign))
    return SDValue();
  Arith A{DAG, DL};
  uint64_t N = C->getZExtValue(), Words = N / 8;
  SmallVector<SDValue, 9> Loads;
  auto Load = [&](SDValue Addr, MachinePointerInfo Info) {
    SDValue L = DAG.getLoad(MVT::i64, DL, Chain, Addr, Info, Align(8));
    Loads.push_back(L.getValue(1));
    return L;
  };
  SmallVector<SDValue, 8> V;
  if (SrcAlign >= Align(8)) {
    for (uint64_t I = 0; I != Words; ++I)
      V.push_back(Load(A.add(Src, 8 * I), SrcPtrInfo.getWithOffset(8 * I)));
  } else {
    // The last doubleword is the one holding the last byte: past the others
    // when the source is unaligned, and shifted out entirely when it is not.
    MachinePointerInfo Info(SrcPtrInfo.getAddrSpace());
    SDValue Base = A.alignDown(Src, 8);
    SmallVector<SDValue, 9> W;
    for (uint64_t I = 0; I != Words; ++I)
      W.push_back(Load(A.add(Base, 8 * I), Info));
    W.push_back(Load(A.alignDown(A.add(Src, N - 1), 8), Info));
    SDValue Shift = A.bitsBelow(Src, 8);
    for (uint64_t I = 0; I != Words; ++I)
      V.push_back(A.funnel(W[I], W[I + 1], Shift));
  }
  Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, Loads);
  return IA64SelectionDAGInfo::emitStores(DAG, DL, Chain, Dst, V, DstAlign,
                                          DstPtrInfo);
}

SDValue IA64SelectionDAGInfo::EmitTargetCodeForMemcpy(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    bool AlwaysInline, MachinePointerInfo DstPtrInfo,
    MachinePointerInfo SrcPtrInfo) const {
  return emitCopy(DAG, DL, Chain, Dst, Src, Size, DstAlign, SrcAlign,
                  IsVolatile, DstPtrInfo, SrcPtrInfo);
}

SDValue IA64SelectionDAGInfo::EmitTargetCodeForMemmove(
    SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst, SDValue Src,
    SDValue Size, Align DstAlign, Align SrcAlign, bool IsVolatile,
    MachinePointerInfo DstPtrInfo, MachinePointerInfo SrcPtrInfo) const {
  return emitCopy(DAG, DL, Chain, Dst, Src, Size, DstAlign, SrcAlign,
                  IsVolatile, DstPtrInfo, SrcPtrInfo);
}
