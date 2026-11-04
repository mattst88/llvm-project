//===-- IA64LocalDescriptors.cpp - Descriptors beside their users ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A function pointer is the address of a function descriptor. The linker
// makes the descriptor of each function in .opd, in an order of its own, so
// the descriptors a program uses together are spread over all of .opd's
// pages. Each one that a call through a table of functions loads may cost a
// DTLB miss.
//
// A function local to this module has no address but the ones taken here.
// This pass gives it a descriptor of its own, in this module's .data beside
// the tables that point to it, and makes every use of its address but a
// direct call use that descriptor. As there is no other, functions still
// compare equal.
//
//===----------------------------------------------------------------------===//

#include "IA64.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-local-descriptors"

STATISTIC(NumDescriptors, "Number of function descriptors made locally");

namespace {
class IA64LocalDescriptors : public ModulePass {
public:
  static char ID;
  IA64LocalDescriptors() : ModulePass(ID) {}
  bool runOnModule(Module &M) override;
  StringRef getPassName() const override {
    return "IA64 local function descriptors";
  }
};
} // end anonymous namespace

char IA64LocalDescriptors::ID = 0;

INITIALIZE_PASS(IA64LocalDescriptors, DEBUG_TYPE,
                "IA64 local function descriptors", false, false)

ModulePass *llvm::createIA64LocalDescriptors() {
  return new IA64LocalDescriptors();
}

// Whether a use of a function's address may name a descriptor instead.
static bool isAddressUse(const Use &U) {
  const User *Usr = U.getUser();
  if (const auto *CB = dyn_cast<CallBase>(Usr))
    return !CB->isCallee(&U);
  if (isa<Instruction>(Usr))
    return true;
  // Initializers, but not an alias, a block address or the like, which name
  // the code.
  return isa<ConstantAggregate>(Usr) || isa<ConstantExpr>(Usr);
}

bool IA64LocalDescriptors::runOnModule(Module &M) {
  SmallVector<GlobalValue *, 8> UsedList;
  collectUsedGlobalVariables(M, UsedList, /*CompilerUsed=*/false);
  collectUsedGlobalVariables(M, UsedList, /*CompilerUsed=*/true);
  SmallPtrSet<GlobalValue *, 8> Used(llvm::from_range, UsedList);
  // Constructors and destructors stay as the AsmPrinter expects them.
  for (const char *Name : {"llvm.global_ctors", "llvm.global_dtors"})
    if (const GlobalVariable *GV = M.getNamedGlobal(Name))
      if (const auto *CA = dyn_cast_or_null<ConstantArray>(
              GV->hasInitializer() ? GV->getInitializer() : nullptr))
        for (const Use &Elt : CA->operands())
          if (const auto *CS = dyn_cast<ConstantStruct>(Elt))
            if (auto *Fn = dyn_cast<GlobalValue>(CS->getOperand(1)))
              Used.insert(Fn);
  // A table of descriptors (a virtual table) holds the code's address in a
  // descriptor of its own, which must stay.
  SmallVector<const Constant *, 16> Worklist;
  SmallPtrSet<const Constant *, 16> Visited;
  for (const GlobalVariable &GV : M.globals())
    if (GV.hasAttribute("function-descriptors") && GV.hasInitializer())
      Worklist.push_back(GV.getInitializer());
  while (!Worklist.empty()) {
    const Constant *C = Worklist.pop_back_val();
    if (!Visited.insert(C).second)
      continue;
    if (const auto *Fn = dyn_cast<Function>(C)) {
      Used.insert(const_cast<Function *>(Fn));
      continue;
    }
    if (isa<GlobalValue>(C))
      continue;
    for (const Use &Op : C->operands())
      Worklist.push_back(cast<Constant>(Op));
  }

  LLVMContext &Ctx = M.getContext();
  PointerType *PtrTy = PointerType::getUnqual(Ctx);
  ArrayType *DescTy = ArrayType::get(PtrTy, 2);
  bool Changed = false;
  for (Function &F : M) {
    if (F.isDeclaration() || !F.hasLocalLinkage() || Used.count(&F) ||
        F.hasSection() || F.hasComdat())
      continue;
    if (!any_of(F.uses(), isAddressUse))
      continue;
    // A use inside a constant that also names the code some other way
    // (an alias, a block address) can't be told apart in an expression, so
    // leave such functions alone.
    if (any_of(F.users(), [](const User *U) {
          return isa<GlobalAlias>(U) || isa<BlockAddress>(U) ||
                 isa<DSOLocalEquivalent>(U) || isa<NoCFIValue>(U) ||
                 isa<GlobalIFunc>(U);
        }))
      continue;
    auto *Desc = new GlobalVariable(M, DescTy, /*isConstant=*/false,
                                    GlobalValue::PrivateLinkage, nullptr,
                                    F.getName() + ".desc");
    Desc->setAlignment(Align(16));
    Desc->setSection(".data");
    Desc->addAttribute("function-descriptors");
    F.replaceUsesWithIf(Desc, isAddressUse);
    Desc->setInitializer(
        ConstantArray::get(DescTy, {&F, ConstantPointerNull::get(PtrTy)}));
    ++NumDescriptors;
    Changed = true;
  }
  return Changed;
}
