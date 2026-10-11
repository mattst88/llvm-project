//===-- IA64.td - Describe the IA64 Target Machine ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Target/Target.td"

include "IA64Features.td"
include "IA64RegisterInfo.td"
include "IA64CallingConv.td"
include "IA64InstrInfo.td"
include "IA64Patterns.td"

defm : RemapAllTargetPseudoPointerOperands<GR>;

def IA64InstrInfo : InstrInfo;

//===----------------------------------------------------------------------===//
// IA-64 processors.
//===----------------------------------------------------------------------===//

class Proc<string Name, list<SubtargetFeature> Features>
    : Processor<Name, NoItineraries, Features>;

def : Proc<"generic",  []>;
def : Proc<"itanium",  []>;
def : Proc<"itanium2", []>;

//===----------------------------------------------------------------------===//
// Target declaration.
//===----------------------------------------------------------------------===//

def IA64InstPrinter : AsmWriter {
  string AsmWriterClassName = "InstPrinter";
}

def IA64AsmParser : AsmParser {
  let ShouldEmitMatchRegisterAltName = 1;
}

// Completers are part of the mnemonic, as in "cmp.eq.unc", so a period does
// not break a token; "=" and the brackets of a memory operand are tokens of
// their own.
def IA64AsmParserVariant : AsmParserVariant {
  let TokenizingCharacters = "[]=";
}

def IA64 : Target {
  let InstructionSet = IA64InstrInfo;
  let AssemblyParsers = [IA64AsmParser];
  let AssemblyParserVariants = [IA64AsmParserVariant];
  let AssemblyWriters = [IA64InstPrinter];
}
