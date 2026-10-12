//===-- ABISysV_ia64.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ABISysV_ia64.h"

#include "lldb/Core/PluginManager.h"
#include "lldb/Core/Value.h"
#include "lldb/Symbol/UnwindPlan.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/Thread.h"
#include "lldb/Utility/RegisterValue.h"
#include "lldb/ValueObject/ValueObjectConstResult.h"

using namespace lldb;
using namespace lldb_private;

LLDB_PLUGIN_DEFINE_ADV(ABISysV_ia64, ABIIA64)

// IA-64 software conventions: integer results are returned in r8, the return
// address arrives in b0, r12 is sp and r1 is gp. A function's inputs and
// locals live in the stacked registers (r32 and up), which are renamed on
// every call, so the caller's r32 is not the callee's. Of the static
// registers, r4-r7, b1-b5, sp and tp are preserved.

size_t ABISysV_ia64::GetRedZoneSize() const {
  // The 16 bytes above sp are scratch space for the callee, not for the
  // current function, and nothing below sp is safe.
  return 0;
}

ABISP ABISysV_ia64::CreateInstance(ProcessSP process_sp, const ArchSpec &arch) {
  if (arch.GetTriple().getArch() == llvm::Triple::ia64)
    return ABISP(
        new ABISysV_ia64(std::move(process_sp), MakeMCRegisterInfo(arch)));
  return ABISP();
}

bool ABISysV_ia64::PrepareTrivialCall(Thread &thread, addr_t sp,
                                      addr_t func_addr, addr_t return_addr,
                                      llvm::ArrayRef<addr_t> args) const {
  // Calling an inferior function requires reading its function descriptor
  // for the entry point and gp, and allocating output registers on the
  // register stack for the arguments. Neither is implemented.
  return false;
}

bool ABISysV_ia64::GetArgumentValues(Thread &thread, ValueList &values) const {
  return false;
}

Status ABISysV_ia64::SetReturnValueObject(StackFrameSP &frame_sp,
                                          ValueObjectSP &new_value_sp) {
  return Status::FromErrorString(
      "Setting a return value is not supported on IA-64");
}

ValueObjectSP ABISysV_ia64::GetReturnValueObjectImpl(
    Thread &thread, CompilerType &return_compiler_type) const {
  ValueObjectSP return_valobj_sp;
  if (!return_compiler_type)
    return return_valobj_sp;

  RegisterContext *reg_ctx = thread.GetRegisterContext().get();
  if (!reg_ctx)
    return return_valobj_sp;

  // Only values returned in r8 are handled: integers, enums and pointers of
  // up to 8 bytes. FP values are returned in f8, which the register contexts
  // do not describe yet.
  bool is_signed = false;
  if (!return_compiler_type.IsIntegerOrEnumerationType(is_signed) &&
      !return_compiler_type.IsPointerType())
    return return_valobj_sp;

  const RegisterInfo *r8_info = reg_ctx->GetRegisterInfoByName("r8", 0);
  if (!r8_info)
    return return_valobj_sp;

  auto byte_size_or_err = return_compiler_type.GetByteSize(&thread);
  if (!byte_size_or_err) {
    llvm::consumeError(byte_size_or_err.takeError());
    return return_valobj_sp;
  }

  // Bits above the type's width are undefined, so truncate to it.
  const uint64_t raw = reg_ctx->ReadRegisterAsUnsigned(r8_info, 0);
  Value value;
  value.SetCompilerType(return_compiler_type);
  switch (*byte_size_or_err) {
  case 1:
    if (is_signed)
      value.GetScalar() = static_cast<int8_t>(raw);
    else
      value.GetScalar() = static_cast<uint8_t>(raw);
    break;
  case 2:
    if (is_signed)
      value.GetScalar() = static_cast<int16_t>(raw);
    else
      value.GetScalar() = static_cast<uint16_t>(raw);
    break;
  case 4:
    if (is_signed)
      value.GetScalar() = static_cast<int32_t>(raw);
    else
      value.GetScalar() = static_cast<uint32_t>(raw);
    break;
  case 8:
    if (is_signed)
      value.GetScalar() = static_cast<int64_t>(raw);
    else
      value.GetScalar() = static_cast<uint64_t>(raw);
    break;
  default:
    return return_valobj_sp;
  }
  value.SetValueType(Value::ValueType::Scalar);

  return ValueObjectConstResult::Create(thread.GetStackFrameAtIndex(0).get(),
                                        value, ConstString(""));
}

UnwindPlanSP ABISysV_ia64::CreateFunctionEntryUnwindPlan() {
  UnwindPlan::Row row;

  // At the entry to a function nothing has been allocated on the memory
  // stack, and the return address is in b0.
  row.GetCFAValue().SetIsRegisterPlusOffset(LLDB_REGNUM_GENERIC_SP, 0);
  row.SetRegisterLocationToRegister(LLDB_REGNUM_GENERIC_PC,
                                    LLDB_REGNUM_GENERIC_RA, true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindGeneric);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("ia64 at-func-entry default");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolNo);
  return plan_sp;
}

UnwindPlanSP ABISysV_ia64::CreateDefaultUnwindPlan() {
  UnwindPlan::Row row;

  // There is no frame pointer to follow, and no fixed place a function keeps
  // its return address: one that makes a call moves b0 to a stacked register
  // of its choosing, and only the unwind table says which. Without the table
  // the one guess to make is that of a leaf function, which leaves b0 and
  // the stack pointer as they arrived. It is right for the innermost frame
  // of most faults and for nothing above it, so everything else is given as
  // unknown and the walk ends at the caller.
  row.GetCFAValue().SetIsRegisterPlusOffset(LLDB_REGNUM_GENERIC_SP, 0);
  row.SetRegisterLocationToRegister(LLDB_REGNUM_GENERIC_PC,
                                    LLDB_REGNUM_GENERIC_RA, true);
  row.SetRegisterLocationToIsCFAPlusOffset(LLDB_REGNUM_GENERIC_SP, 0, true);
  // The unwinder asserts on a default plan that leaves this unset.
  row.SetUnspecifiedRegistersAreUndefined(true);

  auto plan_sp = std::make_shared<UnwindPlan>(eRegisterKindGeneric);
  plan_sp->AppendRow(std::move(row));
  plan_sp->SetSourceName("ia64 default unwind plan");
  plan_sp->SetSourcedFromCompiler(eLazyBoolNo);
  plan_sp->SetUnwindPlanValidAtAllInstructions(eLazyBoolNo);
  plan_sp->SetUnwindPlanForSignalTrap(eLazyBoolNo);
  return plan_sp;
}

bool ABISysV_ia64::RegisterIsVolatile(const RegisterInfo *reg_info) {
  if (!reg_info || !reg_info->name)
    return true;

  // r4-r7, the stack pointer and the thread pointer are preserved, and so
  // are b1-b5. The stacked registers are preserved too, but by renaming: a
  // caller's r32 is another register than the callee's, so the value seen in
  // one frame says nothing of the frame above it.
  llvm::StringRef name(reg_info->name);
  uint32_t num = 0;
  if (name.consume_front("r") && !name.getAsInteger(10, num))
    return !((num >= 4 && num <= 7) || num == 12 || num == 13);
  name = reg_info->name;
  if (name.consume_front("b") && !name.getAsInteger(10, num))
    return !(num >= 1 && num <= 5);
  return true;
}

const RegisterInfo *ABISysV_ia64::GetRegisterInfoArray(uint32_t &count) {
  count = 0;
  return nullptr;
}

void ABISysV_ia64::Initialize() {
  PluginManager::RegisterPlugin(
      GetPluginNameStatic(), "System V ABI for IA-64 targets", CreateInstance);
}

void ABISysV_ia64::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}
