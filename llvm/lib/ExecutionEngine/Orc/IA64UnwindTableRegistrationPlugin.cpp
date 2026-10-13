//===---- IA64UnwindTableRegistrationPlugin.cpp - IA-64 unwind tables -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/IA64UnwindTableRegistrationPlugin.h"

#include "llvm/ExecutionEngine/JITLink/ia64.h"
#include "llvm/ExecutionEngine/Orc/LookupAndApply.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"

#define DEBUG_TYPE "orc"

using namespace llvm::jitlink;

namespace llvm::orc {

Expected<std::unique_ptr<IA64UnwindTableRegistrationPlugin>>
IA64UnwindTableRegistrationPlugin::Create(ExecutionSession &ES) {
  ExecutorAddr RegisterTable;
  ExecutorAddr DeregisterTable;
  if (auto Err = lookupAndApply(
          ES.getBootstrapJITDylib(),
          {recordAddr(rt::RegisterIA64UnwindTableAllocActionName,
                      &RegisterTable),
           recordAddr(rt::DeregisterIA64UnwindTableAllocActionName,
                      &DeregisterTable)}))
    return std::move(Err);

  return std::make_unique<IA64UnwindTableRegistrationPlugin>(RegisterTable,
                                                             DeregisterTable);
}

void IA64UnwindTableRegistrationPlugin::modifyPassConfig(
    MaterializationResponsibility &MR, LinkGraph &LG,
    PassConfiguration &PassConfig) {
  if (LG.getTargetTriple().getArch() != Triple::ia64)
    return;

  PassConfig.PostFixupPasses.push_back([this](LinkGraph &G) -> Error {
    auto *Table = G.findSectionByName(ia64::getUnwindTableSectionName());
    if (!Table || Table->blocks().empty())
      return Error::success();

    using namespace shared;
    auto TableRange = SectionRange(*Table).getRange();
    G.allocActions().push_back(
        {cantFail(
             WrapperFunctionCall::Create<SPSArgList<
                 SPSExecutorAddrRange, SPSExecutorAddrRange, SPSExecutorAddr>>(
                 RegisterTable, TableRange, ia64::getSegmentRange(G),
                 ia64::getGlobalPointer(G))),
         cantFail(WrapperFunctionCall::Create<SPSArgList<SPSExecutorAddrRange>>(
             DeregisterTable, TableRange))});
    return Error::success();
  });
}

} // namespace llvm::orc
