//===-- ArchitectureIA64.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_ARCHITECTURE_IA64_ARCHITECTUREIA64_H
#define LLDB_SOURCE_PLUGINS_ARCHITECTURE_IA64_ARCHITECTUREIA64_H

#include "lldb/Core/Architecture.h"

namespace lldb_private {

class ArchitectureIA64 : public Architecture {
public:
  static llvm::StringRef GetPluginNameStatic() { return "ia64"; }
  static void Initialize();
  static void Terminate();

  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  void OverrideStopInfo(Thread &thread) const override {}

  /// Adds the locations of the caller's stacked registers to a plan. They
  /// are in the register backing store starting at the caller's bsp, and
  /// their number is the frame size in the caller's CFM; neither is known
  /// until the plan's frame is being unwound.
  lldb::UnwindPlanSP GetArchitectureUnwindPlan(
      Thread &thread, RegisterContextUnwind *regctx,
      std::shared_ptr<const UnwindPlan> current_unwindplan) override;

  /// A function with no frame on the memory stack has one in the register
  /// backing store, and the plans give where the caller's starts as the
  /// alternate frame address.
  bool AlternateFrameAddressIdentifiesFrame() const override { return true; }

private:
  static std::unique_ptr<Architecture> Create(const ArchSpec &arch);
  ArchitectureIA64() = default;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_ARCHITECTURE_IA64_ARCHITECTUREIA64_H
