//===-- IA64ExclusivePredicates.h - Predicates that exclude each other ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A compare writes its result to one predicate and the complement to the
// other, so until either is written again, at most one of them is true, and
// of two instructions qualified by them at most one takes effect. They may
// then share an instruction group even if they write or read the same
// register (SDM volume 1, section 3.4), and may run in either order.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_IA64_IA64EXCLUSIVEPREDICATES_H
#define LLVM_LIB_TARGET_IA64_IA64EXCLUSIVEPREDICATES_H

#include "llvm/MC/MCRegister.h"
#include <memory>
#include <vector>

namespace llvm {

class MachineInstr;
class ScheduleDAGMutation;
class TargetRegisterInfo;

/// Tracks, along a block, which predicates exclude each other.
class IA64ExclusivePredicates {
  struct Entry {
    MCRegister Partner;
    // Which compare made the pair, so that two instructions can tell they
    // saw the same one.
    unsigned Compare = 0;
  };
  std::vector<Entry> Entries;
  unsigned NumCompares = 0;

public:
  explicit IA64ExclusivePredicates(const TargetRegisterInfo &TRI);

  /// Forget every pair, as at the start of a block.
  void clear();

  /// Account for \p MI's writes of predicates.
  void step(const MachineInstr &MI);

  /// The predicate that cannot be true at the same time as \p P, if any.
  MCRegister partner(MCRegister P) const { return Entries[P.id()].Partner; }

  /// Which compare made \p P's pair.
  unsigned compare(MCRegister P) const { return Entries[P.id()].Compare; }

  /// The predicate that qualifies all of \p MI's effects, or P0 if some of
  /// them do not depend on one, as a branch's or an .unc compare's.
  static MCRegister qualifyingPredicate(const MachineInstr &MI);

  /// Whether \p MI always writes a predicate to its first target and the
  /// complement to its second, so that exactly one of them is then true.
  static bool writesComplement(const MachineInstr &MI);
};

/// A post-RA scheduling mutation that drops the dependencies between
/// instructions under predicates that exclude each other.
std::unique_ptr<ScheduleDAGMutation> createIA64ExclusivePredicatesMutation();

} // end namespace llvm

#endif
