//===---- RegisterIA64UnwindTables.cpp - Register IA-64 unwind tables -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The IA-64 unwinders have no API for registering an unwind table. libgcc
// finds the table for an address only via dl_iterate_phdr: it looks for the
// object with a PT_LOAD segment containing the address and reads that
// object's PT_IA_64_UNWIND segment, treating entries as offsets from its
// lowest PT_LOAD segment and taking GP from DT_PLTGOT. LLVM's libunwind does
// the same.
//
// So the tables are registered there. dl_iterate_phdr is defined here,
// interposing the C library's. After the objects the C library reports, it
// reports one per registered table: a PT_LOAD segment covering the code, a
// PT_IA_64_UNWIND segment for the table, and a PT_DYNAMIC segment holding
// GP.
//
//===----------------------------------------------------------------------===//

#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterIA64UnwindTables.h"

#include "llvm/Support/FormatVariadic.h"

using namespace llvm;
using namespace llvm::orc;
using namespace llvm::orc::shared;

#if defined(__ia64__) && defined(__linux__)

#include <dlfcn.h>
#include <link.h>
#include <map>
#include <memory>
#include <mutex>

namespace {

// The program headers and dynamic section reported for one table.
struct JITObject {
  ElfW(Phdr) Phdrs[3];
  ElfW(Dyn) Dynamic[2];
};

struct JITObjects {
  std::mutex Mutex;
  // Keyed by table address.
  std::map<uint64_t, std::unique_ptr<JITObject>> Objects;
  uint64_t Added = 0;
  uint64_t Removed = 0;
};

// Never destroyed: an exception may be thrown while the process exits.
JITObjects &getJITObjects() {
  static JITObjects *Objects = new JITObjects();
  return *Objects;
}

using IterateCallback = int (*)(struct dl_phdr_info *, size_t, void *);
using IterateFn = int (*)(IterateCallback, void *);

} // namespace

// The C library's internal name for dl_iterate_phdr. A static libc defines
// it; a shared one does not export it.
extern "C" int __dl_iterate_phdr(IterateCallback, void *) __attribute__((weak));

extern "C" LLVM_ATTRIBUTE_USED LLVM_ATTRIBUTE_VISIBILITY_DEFAULT int
dl_iterate_phdr(IterateCallback Callback, void *Data) {
  static IterateFn Next = [] {
    if (&__dl_iterate_phdr)
      return static_cast<IterateFn>(&__dl_iterate_phdr);
    return reinterpret_cast<IterateFn>(dlsym(RTLD_NEXT, "dl_iterate_phdr"));
  }();
  if (Next)
    if (int Result = Next(Callback, Data))
      return Result;

  JITObjects &JO = getJITObjects();
  std::lock_guard<std::mutex> Lock(JO.Mutex);
  for (auto &KV : JO.Objects) {
    struct dl_phdr_info Info = {};
    Info.dlpi_name = "";
    Info.dlpi_phdr = KV.second->Phdrs;
    Info.dlpi_phnum = std::size(KV.second->Phdrs);
    Info.dlpi_adds = JO.Added;
    Info.dlpi_subs = JO.Removed;
    if (int Result = Callback(&Info, sizeof(Info), Data))
      return Result;
  }
  return 0;
}

Error llvm::orc::registerIA64UnwindTable(ExecutorAddrRange Table,
                                         ExecutorAddrRange Segment,
                                         ExecutorAddr GP) {
  auto Object = std::make_unique<JITObject>();
  *Object = {};

  // Use runtime addresses, as for an object loaded at base 0.

  ElfW(Phdr) &Load = Object->Phdrs[0];
  Load.p_type = PT_LOAD;
  Load.p_flags = PF_R | PF_W | PF_X;
  Load.p_vaddr = Load.p_paddr = Segment.Start.getValue();
  Load.p_filesz = Load.p_memsz = Segment.size();

  ElfW(Phdr) &Unwind = Object->Phdrs[1];
  Unwind.p_type = PT_IA_64_UNWIND;
  Unwind.p_flags = PF_R;
  Unwind.p_vaddr = Unwind.p_paddr = Table.Start.getValue();
  Unwind.p_filesz = Unwind.p_memsz = Table.size();

  ElfW(Phdr) &Dynamic = Object->Phdrs[2];
  Dynamic.p_type = PT_DYNAMIC;
  Dynamic.p_flags = PF_R;
  Dynamic.p_vaddr = Dynamic.p_paddr =
      reinterpret_cast<uintptr_t>(Object->Dynamic);
  Dynamic.p_filesz = Dynamic.p_memsz = sizeof(Object->Dynamic);
  Object->Dynamic[0].d_tag = DT_PLTGOT;
  Object->Dynamic[0].d_un.d_ptr = GP.getValue();
  Object->Dynamic[1].d_tag = DT_NULL;

  JITObjects &JO = getJITObjects();
  std::lock_guard<std::mutex> Lock(JO.Mutex);
  if (!JO.Objects.try_emplace(Table.Start.getValue(), std::move(Object)).second)
    return make_error<StringError>(
        formatv("An IA-64 unwind table at {0:x} is already registered",
                Table.Start.getValue()),
        inconvertibleErrorCode());
  ++JO.Added;
  return Error::success();
}

Error llvm::orc::deregisterIA64UnwindTable(ExecutorAddrRange Table) {
  JITObjects &JO = getJITObjects();
  std::lock_guard<std::mutex> Lock(JO.Mutex);
  if (!JO.Objects.erase(Table.Start.getValue()))
    return make_error<StringError>(
        formatv("No IA-64 unwind table at {0:x} is registered",
                Table.Start.getValue()),
        inconvertibleErrorCode());
  ++JO.Removed;
  return Error::success();
}

#else

Error llvm::orc::registerIA64UnwindTable(ExecutorAddrRange Table,
                                         ExecutorAddrRange Segment,
                                         ExecutorAddr GP) {
  return make_error<StringError>(
      "IA-64 unwind tables can be registered in an IA-64 Linux process only",
      inconvertibleErrorCode());
}

Error llvm::orc::deregisterIA64UnwindTable(ExecutorAddrRange Table) {
  return make_error<StringError>(
      "IA-64 unwind tables can be registered in an IA-64 Linux process only",
      inconvertibleErrorCode());
}

#endif

static Error registerWrapper(ExecutorAddrRange Table, ExecutorAddrRange Segment,
                             ExecutorAddr GP) {
  return registerIA64UnwindTable(Table, Segment, GP);
}

static Error deregisterWrapper(ExecutorAddrRange Table) {
  return deregisterIA64UnwindTable(Table);
}

extern "C" CWrapperFunctionBuffer
llvm_orc_registerIA64UnwindTableAllocAction(const char *ArgData,
                                            size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddrRange, SPSExecutorAddrRange,
                                  SPSExecutorAddr)>::handle(ArgData, ArgSize,
                                                            registerWrapper)
      .release();
}

extern "C" CWrapperFunctionBuffer
llvm_orc_deregisterIA64UnwindTableAllocAction(const char *ArgData,
                                              size_t ArgSize) {
  return WrapperFunction<SPSError(SPSExecutorAddrRange)>::handle(
             ArgData, ArgSize, deregisterWrapper)
      .release();
}
