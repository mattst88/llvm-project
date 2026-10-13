//===- IA64UnwindTableRegistrationPlugin.h - IA-64 unwind tables -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Register each graph's IA-64 unwind table with the executor's unwinder so
// that exceptions can propagate through JIT'd frames.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_IA64UNWINDTABLEREGISTRATIONPLUGIN_H
#define LLVM_EXECUTIONENGINE_ORC_IA64UNWINDTABLEREGISTRATIONPLUGIN_H

#include "llvm/ExecutionEngine/Orc/LinkGraphLinkingLayer.h"
#include "llvm/Support/Compiler.h"

namespace llvm::orc {

/// Adds AllocationActions to register and deregister the unwind table that
/// JITLink places in an IA-64 graph. IA-64 code has no .eh_frame.

class LLVM_ABI IA64UnwindTableRegistrationPlugin
    : public LinkGraphLinkingLayer::Plugin {
public:
  static Expected<std::unique_ptr<IA64UnwindTableRegistrationPlugin>>
  Create(ExecutionSession &ES);

  IA64UnwindTableRegistrationPlugin(ExecutorAddr RegisterTable,
                                    ExecutorAddr DeregisterTable)
      : RegisterTable(RegisterTable), DeregisterTable(DeregisterTable) {}

  void modifyPassConfig(MaterializationResponsibility &MR,
                        jitlink::LinkGraph &G,
                        jitlink::PassConfiguration &PassConfig) override;
  Error notifyFailed(MaterializationResponsibility &MR) override {
    return Error::success();
  }
  Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
    return Error::success();
  }
  void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                   ResourceKey SrcKey) override {}

private:
  ExecutorAddr RegisterTable;
  ExecutorAddr DeregisterTable;
};

} // namespace llvm::orc

#endif // LLVM_EXECUTIONENGINE_ORC_IA64UNWINDTABLEREGISTRATIONPLUGIN_H
