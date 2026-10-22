//===-- IA64SelectionDAGInfo.h - IA64 SelectionDAG Info ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64SELECTIONDAGINFO_H
#define LLVM_LIB_TARGET_IA64_IA64SELECTIONDAGINFO_H

#include "llvm/CodeGen/SelectionDAGTargetInfo.h"

#define GET_SDNODE_ENUM
#include "IA64GenSDNodeInfo.inc"

namespace llvm {

class IA64SelectionDAGInfo : public SelectionDAGGenTargetInfo {
public:
  IA64SelectionDAGInfo();
  ~IA64SelectionDAGInfo() override;

  // Whether a copy of Size bytes goes through EmitTargetCodeForMemcpy/Memmove
  // rather than the generic expansion.
  static bool expandsCopy(uint64_t Size, Align DstAlign, Align SrcAlign);

  // Store the doublewords V to Dst as expandsCopy describes.
  static SDValue
  emitStores(SelectionDAG &DAG, const SDLoc &DL, SDValue Chain, SDValue Dst,
             ArrayRef<SDValue> V, Align DstAlign, MachinePointerInfo DstPtrInfo,
             MachineMemOperand::Flags Flags = MachineMemOperand::MONone,
             const AAMDNodes &AAInfo = AAMDNodes());

  SDValue EmitTargetCodeForMemcpy(SelectionDAG &DAG, const SDLoc &DL,
                                  SDValue Chain, SDValue Dst, SDValue Src,
                                  SDValue Size, Align DstAlign, Align SrcAlign,
                                  bool IsVolatile, bool AlwaysInline,
                                  MachinePointerInfo DstPtrInfo,
                                  MachinePointerInfo SrcPtrInfo) const override;

  SDValue EmitTargetCodeForMemmove(SelectionDAG &DAG, const SDLoc &DL,
                                   SDValue Chain, SDValue Dst, SDValue Src,
                                   SDValue Size, Align DstAlign,
                                   Align SrcAlign, bool IsVolatile,
                                   MachinePointerInfo DstPtrInfo,
                                   MachinePointerInfo SrcPtrInfo) const override;
};

} // end namespace llvm

#endif // LLVM_LIB_TARGET_IA64_IA64SELECTIONDAGINFO_H
