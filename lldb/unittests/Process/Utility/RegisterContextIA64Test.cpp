//===-- RegisterContextIA64Test.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "Plugins/Process/Utility/RegisterContextLinux_ia64.h"
#include "Plugins/Process/Utility/lldb-ia64-register-enums.h"
#include "lldb/Utility/ArchSpec.h"

using namespace lldb;
using namespace lldb_private;

static const RegisterInfo *findByName(const RegisterContextLinux_ia64 &ctx,
                                      llvm::StringRef name) {
  const RegisterInfo *infos = ctx.GetRegisterInfo();
  for (uint32_t i = 0, e = ctx.GetRegisterCount(); i != e; ++i)
    if (name == infos[i].name)
      return &infos[i];
  return nullptr;
}

TEST(RegisterContextIA64Test, RegisterCount) {
  RegisterContextLinux_ia64 ctx{ArchSpec("ia64-unknown-linux-gnu")};
  EXPECT_EQ(ctx.GetRegisterCount(),
            static_cast<uint32_t>(k_num_registers_ia64));

  // A register's place in the table is its number.
  const RegisterInfo *infos = ctx.GetRegisterInfo();
  for (uint32_t i = 0, e = ctx.GetRegisterCount(); i != e; ++i)
    EXPECT_EQ(infos[i].kinds[eRegisterKindLLDB], i);
}

TEST(RegisterContextIA64Test, DwarfNumbering) {
  RegisterContextLinux_ia64 ctx{ArchSpec("ia64-unknown-linux-gnu")};

  // The general registers are DWARF 0-127, the branch registers 320-327 and
  // application register N is 334 + N.  These follow the backend's
  // IA64RegisterInfo.td.
  const RegisterInfo *r0 = findByName(ctx, "r0");
  ASSERT_NE(r0, nullptr);
  EXPECT_EQ(r0->kinds[eRegisterKindDWARF], 0u);

  const RegisterInfo *r127 = findByName(ctx, "r127");
  ASSERT_NE(r127, nullptr);
  EXPECT_EQ(r127->kinds[eRegisterKindDWARF], 127u);

  const RegisterInfo *b0 = findByName(ctx, "b0");
  ASSERT_NE(b0, nullptr);
  EXPECT_EQ(b0->kinds[eRegisterKindDWARF], 320u);

  const RegisterInfo *b7 = findByName(ctx, "b7");
  ASSERT_NE(b7, nullptr);
  EXPECT_EQ(b7->kinds[eRegisterKindDWARF], 327u);

  const RegisterInfo *bsp = findByName(ctx, "bsp");
  ASSERT_NE(bsp, nullptr);
  EXPECT_EQ(bsp->kinds[eRegisterKindDWARF], 334u + 17);

  const RegisterInfo *pfs = findByName(ctx, "pfs");
  ASSERT_NE(pfs, nullptr);
  EXPECT_EQ(pfs->kinds[eRegisterKindDWARF], 334u + 64);

  const RegisterInfo *ec = findByName(ctx, "ec");
  ASSERT_NE(ec, nullptr);
  EXPECT_EQ(ec->kinds[eRegisterKindDWARF], 334u + 66);
}

TEST(RegisterContextIA64Test, GenericRegisters) {
  RegisterContextLinux_ia64 ctx{ArchSpec("ia64-unknown-linux-gnu")};

  const RegisterInfo *ip = findByName(ctx, "ip");
  ASSERT_NE(ip, nullptr);
  EXPECT_EQ(ip->kinds[eRegisterKindGeneric],
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_PC));
  EXPECT_STREQ(ip->alt_name, "pc");

  const RegisterInfo *r12 = findByName(ctx, "r12");
  ASSERT_NE(r12, nullptr);
  EXPECT_EQ(r12->kinds[eRegisterKindGeneric],
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_SP));

  const RegisterInfo *b0 = findByName(ctx, "b0");
  ASSERT_NE(b0, nullptr);
  EXPECT_EQ(b0->kinds[eRegisterKindGeneric],
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_RA));

  // The first eight stacked registers of a frame are its arguments.
  const RegisterInfo *r32 = findByName(ctx, "r32");
  ASSERT_NE(r32, nullptr);
  EXPECT_EQ(r32->kinds[eRegisterKindGeneric],
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_ARG1));

  const RegisterInfo *r39 = findByName(ctx, "r39");
  ASSERT_NE(r39, nullptr);
  EXPECT_EQ(r39->kinds[eRegisterKindGeneric],
            static_cast<uint32_t>(LLDB_REGNUM_GENERIC_ARG8));

  const RegisterInfo *r40 = findByName(ctx, "r40");
  ASSERT_NE(r40, nullptr);
  EXPECT_EQ(r40->kinds[eRegisterKindGeneric], LLDB_INVALID_REGNUM);
}

TEST(RegisterContextIA64Test, NoteOffsets) {
  RegisterContextLinux_ia64 ctx{ArchSpec("ia64-unknown-linux-gnu")};

  // Offsets into NT_PRSTATUS follow the kernel's elf_gregset_t: r0-r31, the
  // NaT bits, pr, b0-b7, ip, cfm, psr and then the application registers.
  // The stacked registers are not in the note.
  EXPECT_EQ(ctx.GetGPRSize(), 128u * 8);

  const RegisterInfo *r1 = findByName(ctx, "r1");
  ASSERT_NE(r1, nullptr);
  EXPECT_EQ(r1->byte_offset, 1u * 8);
  EXPECT_EQ(r1->byte_size, 8u);

  const RegisterInfo *r31 = findByName(ctx, "r31");
  ASSERT_NE(r31, nullptr);
  EXPECT_EQ(r31->byte_offset, 31u * 8);

  const RegisterInfo *nat = findByName(ctx, "nat");
  ASSERT_NE(nat, nullptr);
  EXPECT_EQ(nat->byte_offset, 32u * 8);

  const RegisterInfo *pr = findByName(ctx, "pr");
  ASSERT_NE(pr, nullptr);
  EXPECT_EQ(pr->byte_offset, 33u * 8);

  const RegisterInfo *b0 = findByName(ctx, "b0");
  ASSERT_NE(b0, nullptr);
  EXPECT_EQ(b0->byte_offset, 34u * 8);

  const RegisterInfo *ip = findByName(ctx, "ip");
  ASSERT_NE(ip, nullptr);
  EXPECT_EQ(ip->byte_offset, 42u * 8);

  const RegisterInfo *cfm = findByName(ctx, "cfm");
  ASSERT_NE(cfm, nullptr);
  EXPECT_EQ(cfm->byte_offset, 43u * 8);

  const RegisterInfo *bsp = findByName(ctx, "bsp");
  ASSERT_NE(bsp, nullptr);
  EXPECT_EQ(bsp->byte_offset, 46u * 8);

  const RegisterInfo *ssd = findByName(ctx, "ssd");
  ASSERT_NE(ssd, nullptr);
  EXPECT_EQ(ssd->byte_offset, 56u * 8);
}

TEST(RegisterContextIA64Test, SkipBackingStoreRegisters) {
  const uint64_t base = UINT64_C(0x6000000000004000);
  auto skip = RegisterContextLinux_ia64::SkipBackingStoreRegisters;

  EXPECT_EQ(skip(base, 0), base);
  EXPECT_EQ(skip(base, 1), base + 8);
  EXPECT_EQ(skip(base, 62), base + 62 * 8);

  // The doubleword at 0x1f8 of every 0x200 holds NaT bits, not a register.
  EXPECT_EQ(skip(base, 63), base + 0x200);
  EXPECT_EQ(skip(base + 62 * 8, 1), base + 0x200);
  // 96 registers from the middle of a group pass two of them.
  EXPECT_EQ(skip(base + 0x100, 96), base + 0x100 + 98 * 8);
  EXPECT_EQ(skip(base, 126), base + 0x400);

  // And the same going down.
  EXPECT_EQ(skip(base, -1), base - 0x10);
  EXPECT_EQ(skip(base + 8, -1), base);
  EXPECT_EQ(skip(base + 0x200, -63), base);
  EXPECT_EQ(skip(base + 0x208, -96), base + 0x208 - 98 * 8);
}
