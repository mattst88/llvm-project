//===-- IA64AsmParser.cpp - Parse IA64 assembly to MCInst instructions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCAsmInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "MCTargetDesc/IA64TargetStreamer.h"
#include "TargetInfo/IA64TargetInfo.h"
#include "llvm/ADT/SmallBitVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/AsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;

#define DEBUG_TYPE "ia64-asm-parser"

namespace {

class IA64Operand;

class IA64AsmParser : public MCTargetAsmParser {
  bool matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                               OperandVector &Operands, MCStreamer &Out,
                               uint64_t &ErrorInfo,
                               bool MatchingInlineAsm) override;

  bool parseRegister(MCRegister &Reg, SMLoc &StartLoc, SMLoc &EndLoc) override;
  ParseStatus tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                               SMLoc &EndLoc) override;

  bool parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;

  ParseStatus parseDirective(AsmToken DirectiveID) override;

  unsigned validateTargetOperandClass(MCParsedAsmOperand &Op,
                                      unsigned Kind) override;

  // A statement may open with a qualifying predicate: "(p6) add ...".
  bool tokenIsStartOfStatement(AsmToken::TokenKind Token) override {
    return Token == AsmToken::LParen;
  }

  void onEmptyStatement(const AsmToken &Terminator) override;

  bool parseOperand(OperandVector &Operands);

  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override;

  /// The directives that put data in the section, most of them written
  /// without a dot. NoMatch if \p Name is not one.
  ParseStatus parseDataDirective(StringRef Name);
  bool parseDataValues(unsigned Size, bool Aligned);
  bool parseDataStrings(bool ZeroTerminated);

  /// Annotation directives, which tell a dependency violation checker what
  /// it cannot infer. NoMatch if \p Name is not one.
  ParseStatus parseAnnotationDirective(StringRef Name);
  /// A list of predicate registers and of ranges of them: "p1-p5, p16".
  /// \p Count is the number of registers.
  bool parsePredicateList(unsigned &Count);

  /// ".proc" and ".endp", which bracket a procedure.
  ParseStatus parseProcDirective(StringRef Name);
  /// The entry points of the open procedure, if any.
  SmallVector<MCSymbol *, 2> ProcEntries;

  ParseStatus parseUnwindDirective(StringRef Name, SMLoc NameLoc);
  bool parseUnwindReg(IA64TargetStreamer::UnwindReg &Reg);
  bool parseUnwindGR(int64_t &GR, bool OrNumber);
  bool parseUnwindOffset(int64_t &Offset, int64_t Min, unsigned Multiple);
  /// The kind of the currently open unwind region.
  enum { NoRegion, PrologueRegion, BodyRegion } UnwindRegion = NoRegion;
  /// The GR save mask and grsave register given by the open .prologue.
  unsigned PrologueMask = 0, PrologueGRSave = 0;
  /// The current prologue nesting depth, and the depth at each .label_state.
  unsigned OpenPrologues = 0;
  DenseMap<uint64_t, unsigned> LabelledStates;
  /// The last directive that describes the following instruction, while
  /// that instruction has not appeared yet.
  SMLoc AwaitsInstruction;
  /// How many instructions it still describes, and whether it is a mask
  /// directive, which no other directive may interrupt.
  unsigned Awaited = 0;
  bool AwaitedByMask = false;

  /// The operand of a memory access written without a locality hint.
  std::unique_ptr<IA64Operand> defaultHintOperand();
  /// The operand of a floating-point instruction that names no status field.
  std::unique_ptr<IA64Operand> defaultStatusFieldOperand();
  /// The operands of a branch written without ".many" and without ".clr".
  std::unique_ptr<IA64Operand> defaultBranchPrefetchOperand();
  std::unique_ptr<IA64Operand> defaultBranchDeallocOperand();

  bool validateInstruction(const MCInst &Inst, const OperandVector &Operands);

  IA64TargetStreamer &getTargetStreamer() {
    return static_cast<IA64TargetStreamer &>(
        *getParser().getStreamer().getTargetStreamer());
  }

  /// Whether the instruction being matched was written with a "(pN)" prefix.
  bool ExplicitQP = false;

#define GET_ASSEMBLER_HEADER
#include "IA64GenAsmMatcher.inc"

public:
  enum IA64MatchResultTy {
    Match_Dummy = FIRST_TARGET_MATCH_RESULT_TY,
#define GET_OPERAND_DIAGNOSTIC_TYPES
#include "IA64GenAsmMatcher.inc"
  };

  IA64AsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser,
                const MCInstrInfo &MII)
      : MCTargetAsmParser(STI, MII) {
    MCAsmParserExtension::Initialize(Parser);
    setAvailableFeatures(ComputeAvailableFeatures(STI.getFeatureBits()));
  }
};

class IA64Operand : public MCParsedAsmOperand {
public:
  // Kinds after Immediate are mnemonic completers that are encoded as
  // instruction operands.
  enum KindTy {
    Token,
    Register,
    Immediate,
    Hint,
    StatusField,
    BranchWhether,
    BranchPrefetch,
    BranchDealloc
  };

private:
  KindTy Kind;
  SMLoc StartLoc, EndLoc;
  StringRef Tok;
  MCRegister Reg;
  const MCExpr *Imm = nullptr;
  unsigned Completer = 0;

public:
  IA64Operand(KindTy K, SMLoc S, SMLoc E) : Kind(K), StartLoc(S), EndLoc(E) {}

  bool isToken() const override { return Kind == Token; }
  bool isReg() const override { return Kind == Register; }
  bool isImm() const override { return Kind == Immediate; }
  bool isMem() const override { return false; }

  StringRef getToken() const {
    assert(Kind == Token && "not a token");
    return Tok;
  }
  MCRegister getReg() const override {
    assert(Kind == Register && "not a register");
    return Reg;
  }
  const MCExpr *getImm() const {
    assert(Kind == Immediate && "not an immediate");
    return Imm;
  }

  /// The operand's value, if it is an immediate that folds to a constant.
  std::optional<int64_t> getConstant() const {
    if (Kind != Immediate)
      return std::nullopt;
    if (const auto *CE = dyn_cast<MCConstantExpr>(Imm))
      return CE->getValue();
    return std::nullopt;
  }

  bool isQP() const {
    return Kind == Register && Reg >= IA64::P0 && Reg <= IA64::P63;
  }
  /// An IP-relative target with N bits in the encoding: a constant must be
  /// a whole number of bundles that fits; anything else is left to a fixup.
  template <unsigned N> bool isPCRel() const {
    if (Kind != Immediate)
      return false;
    std::optional<int64_t> V = getConstant();
    return !V || isShiftedInt<N, 4>(*V);
  }
  template <unsigned N> bool isSImm() const {
    std::optional<int64_t> V = getConstant();
    return V && isInt<N>(*V);
  }
  /// An immediate that may be an expression for a fixup to fill in.
  template <unsigned N> bool isSImmOrExpr() const {
    if (Kind != Immediate)
      return false;
    std::optional<int64_t> V = getConstant();
    return !V || isInt<N>(*V);
  }
  template <unsigned N> bool isUImm() const {
    std::optional<int64_t> V = getConstant();
    return V && isUInt<N>(*V);
  }
  // The immediate of a compare pseudo-op that is assembled as a "less than"
  // compare against the immediate minus one.
  bool isSImm8Minus1() const {
    std::optional<int64_t> V = getConstant();
    return V && *V >= -127 && *V <= 128;
  }
  bool isSImm8Minus1NonZero() const {
    return isSImm8Minus1() && *getConstant();
  }
  // The locality hint completers: loads have no .nt2, and stores only .nta.
  bool isFetchHint() const { return Kind == Hint; }
  bool isLoadHint() const { return Kind == Hint && Completer != 2; }
  bool isStoreHint() const {
    return Kind == Hint && (Completer == 0 || Completer == 3);
  }
  bool isStatusField() const { return Kind == StatusField; }
  bool isBranchWhether() const { return Kind == BranchWhether; }
  bool isBranchPrefetch() const { return Kind == BranchPrefetch; }
  bool isBranchDealloc() const { return Kind == BranchDealloc; }
  bool isInc3() const {
    std::optional<int64_t> V = getConstant();
    if (!V)
      return false;
    uint64_t Magnitude = *V < 0 ? -*V : *V;
    return Magnitude == 1 || Magnitude == 4 || Magnitude == 8 ||
           Magnitude == 16;
  }
  bool isMask17() const {
    std::optional<int64_t> V = getConstant();
    return V && (isInt<17>(*V) || isUInt<17>(*V));
  }
  bool isPmpyshrCount() const {
    std::optional<int64_t> V = getConstant();
    return V && (*V == 0 || *V == 7 || *V == 15 || *V == 16);
  }
  bool isMBType() const {
    std::optional<int64_t> V = getConstant();
    return V && *V >= 0 && IA64II::getMBTypeName(*V);
  }
  template <int64_t Lo, int64_t Hi> bool isImmInRange() const {
    std::optional<int64_t> V = getConstant();
    return V && *V >= Lo && *V <= Hi;
  }

  SMLoc getStartLoc() const override { return StartLoc; }
  SMLoc getEndLoc() const override { return EndLoc; }

  void addRegOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid number of operands");
    Inst.addOperand(MCOperand::createReg(getReg()));
  }
  void addImmOperands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid number of operands");
    if (Kind > Immediate)
      Inst.addOperand(MCOperand::createImm(Completer));
    else if (std::optional<int64_t> V = getConstant())
      Inst.addOperand(MCOperand::createImm(*V));
    else
      Inst.addOperand(MCOperand::createExpr(getImm()));
  }

  void addImmMinus1Operands(MCInst &Inst, unsigned N) const {
    assert(N == 1 && "invalid number of operands");
    Inst.addOperand(MCOperand::createImm(*getConstant() - 1));
  }

  void print(raw_ostream &O, const MCAsmInfo &MAI) const override {
    switch (Kind) {
    case Token:
      O << "Token " << Tok;
      break;
    case Register:
      O << "Register " << Reg.id();
      break;
    case Immediate:
      O << "Immediate ";
      MAI.printExpr(O, *Imm);
      break;
    case Hint:
      O << "Hint " << Completer;
      break;
    case StatusField:
      O << "StatusField " << Completer;
      break;
    case BranchWhether:
      O << "BranchWhether " << Completer;
      break;
    case BranchPrefetch:
      O << "BranchPrefetch " << Completer;
      break;
    case BranchDealloc:
      O << "BranchDealloc " << Completer;
      break;
    }
  }

  static std::unique_ptr<IA64Operand> createToken(StringRef Str, SMLoc S) {
    auto Op = std::make_unique<IA64Operand>(Token, S, S);
    Op->Tok = Str;
    return Op;
  }
  static std::unique_ptr<IA64Operand> createReg(MCRegister Reg, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<IA64Operand>(Register, S, E);
    Op->Reg = Reg;
    return Op;
  }
  static std::unique_ptr<IA64Operand> createCompleter(KindTy Kind,
                                                      unsigned Value, SMLoc S) {
    auto Op = std::make_unique<IA64Operand>(Kind, S, S);
    Op->Completer = Value;
    return Op;
  }
  static std::unique_ptr<IA64Operand> createImm(const MCExpr *Val, SMLoc S,
                                                SMLoc E) {
    auto Op = std::make_unique<IA64Operand>(Immediate, S, E);
    Op->Imm = Val;
    return Op;
  }
};

} // end anonymous namespace

#define GET_REGISTER_MATCHER
#define GET_MATCHER_IMPLEMENTATION
#include "IA64GenAsmMatcher.inc"

/// Register names are matched without regard to case.
/// Strip operand completers off the end of a mnemonic.
static void splitCompleters(
    StringRef &Name,
    SmallVectorImpl<std::pair<IA64Operand::KindTy, unsigned>> &Completers) {
  // A branch mnemonic ends in the whether hint, the prefetch hint and the
  // deallocation hint, in that order. Only the first is required.
  if (Name == "br" || Name == "brl" || Name.starts_with("br.") ||
      Name.starts_with("brl.")) {
    std::optional<unsigned> Whether, Prefetch, Dealloc;
    if (Name.consume_back(".clr"))
      Dealloc = 1;
    if (Name.consume_back(".few"))
      Prefetch = 0;
    else if (Name.consume_back(".many"))
      Prefetch = 1;
    for (unsigned Value = 0; Value != 4 && !Whether; ++Value)
      if (Name.consume_back(IA64II::getBranchWhetherName(Value)))
        Whether = Value;
    // Without a branch type, "br" and "brl" are pseudo-ops for an
    // unconditional branch, as in both "br" and "br.sptk.many".
    if (Name == "br" || Name == "brl") {
      Name = Name == "br" ? "br.cond" : "brl.cond";
      if (!Whether)
        Whether = 0;
    }
    if (Whether)
      Completers.push_back({IA64Operand::BranchWhether, *Whether});
    if (Prefetch)
      Completers.push_back({IA64Operand::BranchPrefetch, *Prefetch});
    if (Dealloc)
      Completers.push_back({IA64Operand::BranchDealloc, *Dealloc});
    return;
  }
  // A memory access may end in a locality hint.
  for (unsigned Value = 1; Value != 4; ++Value) {
    if (Name.consume_back(IA64II::getHintName(Value))) {
      Completers.push_back({IA64Operand::Hint, Value});
      return;
    }
  }
  // A floating-point instruction may end in a status field.
  if (Name.size() > 3 && Name.drop_back().ends_with(".s") &&
      Name.back() >= '0' && Name.back() <= '3') {
    Completers.push_back({IA64Operand::StatusField, Name.back() - '0'});
    Name = Name.drop_back(3);
  }
}

static MCRegister matchRegister(StringRef Name) {
  std::string Lower = Name.lower();
  if (MCRegister Reg = MatchRegisterName(Lower))
    return Reg;
  return MatchRegisterAltName(Lower);
}

bool IA64AsmParser::matchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                            OperandVector &Operands,
                                            MCStreamer &Out,
                                            uint64_t &ErrorInfo,
                                            bool MatchingInlineAsm) {
  // The statement was a directive, which parseInstruction has dealt with.
  if (Operands.empty())
    return false;

  MCInst Inst;
  unsigned Result =
      MatchInstructionImpl(Operands, Inst, ErrorInfo, MatchingInlineAsm);

  // parseInstruction supplies p0 for an instruction written without a
  // qualifying predicate. A few instructions have none at all, so on failure
  // see whether one of those was meant.
  if (Result != Match_Success && !ExplicitQP) {
    SmallVector<std::unique_ptr<MCParsedAsmOperand>, 8> WithoutQP;
    for (unsigned I = 0, E = Operands.size(); I != E; ++I)
      if (I != 1)
        WithoutQP.push_back(std::move(Operands[I]));
    MCInst Retry;
    uint64_t RetryInfo = 0;
    unsigned RetryResult =
        MatchInstructionImpl(WithoutQP, Retry, RetryInfo, MatchingInlineAsm);
    if (RetryResult == Match_Success) {
      Result = RetryResult;
      Inst = Retry;
    } else {
      // Report whichever attempt got further. RetryInfo indexes the list
      // without the predicate.
      if (RetryInfo != ~0ULL && RetryInfo >= 1)
        ++RetryInfo;
      if (int64_t(RetryInfo) > int64_t(ErrorInfo)) {
        Result = RetryResult;
        ErrorInfo = RetryInfo;
      }
    }
    // Put the operands back for the diagnostics below, and so that the caller
    // sees the list it passed in.
    std::unique_ptr<MCParsedAsmOperand> QP = std::move(Operands[1]);
    Operands.clear();
    for (unsigned I = 0, E = WithoutQP.size(); I != E; ++I) {
      if (I == 1)
        Operands.push_back(std::move(QP));
      Operands.push_back(std::move(WithoutQP[I]));
    }
    if (WithoutQP.size() == 1)
      Operands.push_back(std::move(QP));
  }

  switch (Result) {
  case Match_Success:
    if (validateInstruction(Inst, Operands))
      return true;
    // "add" with an immediate and an r0-r3 base prefers addl; the matcher
    // only reaches addl for an immediate that does not fit in adds.
    if (Inst.getOpcode() == IA64::ADDS &&
        static_cast<IA64Operand &>(*Operands[0]).getToken() == "add" &&
        getContext()
            .getRegisterInfo()
            ->getRegClass(IA64::GR0_3RegClassID)
            .contains(Inst.getOperand(2).getReg()))
      Inst.setOpcode(IA64::ADDL);
    Inst.setLoc(IDLoc);
    Opcode = Inst.getOpcode();
    Out.emitInstruction(Inst, getSTI());
    if (Awaited != 0 && --Awaited == 0)
      AwaitsInstruction = SMLoc();
    return false;
  case Match_MnemonicFail:
    return Error(IDLoc, "unrecognized instruction mnemonic");
  case Match_MissingFeature:
    return Error(IDLoc, "instruction requires a CPU feature not enabled");
  case Match_InvalidOperand: {
    SMLoc ErrorLoc = IDLoc;
    if (ErrorInfo != ~0ULL) {
      if (ErrorInfo >= Operands.size())
        return Error(IDLoc, "too few operands for instruction");
      ErrorLoc = Operands[ErrorInfo]->getStartLoc();
      if (ErrorLoc == SMLoc())
        ErrorLoc = IDLoc;
    }
    return Error(ErrorLoc, "invalid operand for instruction");
  }
  case Match_InvalidSImm8:
  case Match_InvalidSImm14:
  case Match_InvalidSImm22:
  case Match_InvalidSImm8Minus1:
  case Match_InvalidSImm8Minus1NonZero:
  case Match_InvalidPCRel9:
  case Match_InvalidPCRel21:
  case Match_InvalidPCRel60:
  case Match_InvalidCount1To3:
  case Match_InvalidCount1To4: {
    // The failing operand may be one that the statement omitted.
    if (ErrorInfo == ~0ULL)
      return Error(IDLoc, "invalid operand for instruction");
    if (ErrorInfo >= Operands.size())
      return Error(IDLoc, "too few operands for instruction");
    SMLoc ErrorLoc = Operands[ErrorInfo]->getStartLoc();
    // The range diagnostics below apply only to an immediate operand.
    if (!Operands[ErrorInfo]->isImm())
      return Error(ErrorLoc == SMLoc() ? IDLoc : ErrorLoc,
                   "invalid operand for instruction");
    switch (Result) {
    case Match_InvalidSImm8:
      return Error(ErrorLoc, "immediate must be an integer in [-128, 127]");
    case Match_InvalidSImm14:
      return Error(ErrorLoc, "immediate must be an integer in [-8192, 8191]");
    case Match_InvalidSImm22:
      return Error(ErrorLoc,
                   "immediate must be an integer in [-2097152, 2097151]");
    case Match_InvalidSImm8Minus1:
      return Error(ErrorLoc, "immediate must be an integer in [-127, 128]");
    case Match_InvalidSImm8Minus1NonZero:
      return Error(ErrorLoc,
                   "immediate must be a nonzero integer in [-127, 128]");
    case Match_InvalidPCRel9:
      return Error(ErrorLoc,
                   "target must be a multiple of 16 in [-4096, 4080]");
    case Match_InvalidPCRel21:
      return Error(ErrorLoc,
                   "target must be a multiple of 16 in [-16777216, 16777200]");
    case Match_InvalidPCRel60:
      return Error(ErrorLoc, "target must be a multiple of 16");
    case Match_InvalidCount1To3:
      return Error(ErrorLoc, "shift count must be an integer in [1, 3]");
    default:
      return Error(ErrorLoc, "shift count must be an integer in [1, 4]");
    }
  }
  }
  llvm_unreachable("unknown match result");
}

std::unique_ptr<IA64Operand> IA64AsmParser::defaultHintOperand() {
  return IA64Operand::createCompleter(IA64Operand::Hint, 0, SMLoc());
}

std::unique_ptr<IA64Operand> IA64AsmParser::defaultStatusFieldOperand() {
  return IA64Operand::createCompleter(IA64Operand::StatusField, 0, SMLoc());
}

std::unique_ptr<IA64Operand> IA64AsmParser::defaultBranchPrefetchOperand() {
  return IA64Operand::createCompleter(IA64Operand::BranchPrefetch, 0, SMLoc());
}

std::unique_ptr<IA64Operand> IA64AsmParser::defaultBranchDeallocOperand() {
  return IA64Operand::createCompleter(IA64Operand::BranchDealloc, 0, SMLoc());
}

bool IA64AsmParser::validateInstruction(const MCInst &Inst,
                                        const OperandVector &Operands) {
  const MCRegisterInfo &MRI = *getContext().getRegisterInfo();
  // Operands are the mnemonic, the predicate, the destination, "=" and the
  // source. A move that names its unit (mov.m, mov.i) must name an AR
  // accessible from that unit.
  auto CheckAR = [&](unsigned OpNo, unsigned ParsedNo, IA64II::Unit Unit) {
    unsigned Number = MRI.getEncodingValue(Inst.getOperand(OpNo).getReg());
    if (IA64::getARUnits(Number) & (1u << Unit))
      return false;
    return Error(Operands[ParsedNo]->getStartLoc(),
                 Twine("application register is not one of the ") +
                     (Unit == IA64II::UnitM ? "M" : "I") + " unit");
  };
  switch (Inst.getOpcode()) {
  case IA64::MOV_M_AR:
  case IA64::MOV_M_ARi:
    return CheckAR(0, 2, IA64II::UnitM);
  case IA64::MOV_M_FROM_AR:
    return CheckAR(1, 4, IA64II::UnitM);
  case IA64::MOV_I_AR:
  case IA64::MOV_I_ARi:
    return CheckAR(0, 2, IA64II::UnitI);
  case IA64::MOV_I_FROM_AR:
    return CheckAR(1, 4, IA64II::UnitI);
  case IA64::ALLOC:
    break;
  default:
    return false;
  }

  // An alloc with a predicate other than p0 is an illegal operation.
  if (Inst.getOperand(5).getReg() != IA64::P0)
    return Error(Operands[1]->getStartLoc(), "alloc takes no predicate");

  // After ar.pfs come the four sizes.
  auto Loc = [&](unsigned N) { return Operands[5 + N]->getStartLoc(); };
  uint64_t Inputs = Inst.getOperand(1).getImm();
  uint64_t Locals = Inst.getOperand(2).getImm();
  uint64_t Outputs = Inst.getOperand(3).getImm();
  uint64_t Rotating = Inst.getOperand(4).getImm();
  uint64_t Size = Inputs + Locals + Outputs;
  if (Size > IA64II::MaxFrameSize)
    return Error(Loc(0), "register stack frame is larger than " +
                             Twine(IA64II::MaxFrameSize) + " registers");
  if (Rotating % 8)
    return Error(Loc(3), "number of rotating registers must be a multiple "
                         "of 8");
  if (Rotating > Size)
    return Error(Loc(3), "more rotating registers than the frame has");
  return false;
}

bool IA64AsmParser::parseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                  SMLoc &EndLoc) {
  if (!tryParseRegister(Reg, StartLoc, EndLoc).isSuccess())
    return Error(getLexer().getLoc(), "invalid register name");
  return false;
}

ParseStatus IA64AsmParser::tryParseRegister(MCRegister &Reg, SMLoc &StartLoc,
                                            SMLoc &EndLoc) {
  const AsmToken &Tok = getParser().getTok();
  if (Tok.isNot(AsmToken::Identifier))
    return ParseStatus::NoMatch;
  Reg = matchRegister(Tok.getIdentifier());
  if (!Reg)
    return ParseStatus::NoMatch;
  StartLoc = Tok.getLoc();
  EndLoc = Tok.getEndLoc();
  getParser().Lex();
  return ParseStatus::Success;
}

bool IA64AsmParser::parseOperand(OperandVector &Operands) {
  MCAsmParser &Parser = getParser();
  const AsmToken &Tok = Parser.getTok();
  SMLoc S = Tok.getLoc();

  switch (Tok.getKind()) {
  case AsmToken::Equal:
    Operands.push_back(IA64Operand::createToken("=", S));
    Parser.Lex();
    return false;
  case AsmToken::LBrac:
    Operands.push_back(IA64Operand::createToken("[", S));
    Parser.Lex();
    return false;
  case AsmToken::RBrac:
    Operands.push_back(IA64Operand::createToken("]", S));
    Parser.Lex();
    return false;
  case AsmToken::At: {
    // "@name" is one of the mux1 permutations, and "@name(expression)" a
    // relocation specifier, which is part of an expression.
    const AsmToken &Next = getLexer().peekTok();
    if (Next.isNot(AsmToken::Identifier))
      return Error(S, "expected a name after '@'");
    StringRef Name = Next.getString();
    for (unsigned Value = 0; Value != 16; ++Value) {
      const char *Known = IA64II::getMBTypeName(Value);
      if (!Known || !Name.equals_insensitive(Known))
        continue;
      Operands.push_back(IA64Operand::createImm(
          MCConstantExpr::create(Value, getContext()), S, Next.getEndLoc()));
      Parser.Lex(); // Eat the '@'.
      Parser.Lex(); // Eat the name.
      return false;
    }
    break;
  }
  case AsmToken::Identifier: {
    // "r4#" is the symbol r4 and not the register.
    const AsmToken &Next = getLexer().peekTok();
    if (Next.is(AsmToken::Hash) && Next.getLoc() == Tok.getEndLoc())
      break;
    MCRegister Reg;
    SMLoc E;
    if (tryParseRegister(Reg, S, E).isSuccess()) {
      Operands.push_back(IA64Operand::createReg(Reg, S, E));
      return false;
    }
    // Names that are part of an instruction's syntax and not registers that
    // can be used freely.
    StringRef Name = Tok.getIdentifier();
    if (StringSwitch<bool>(Name.lower())
            .Cases({"ip", "pr", "pr.rot", "psr", "psr.l", "psr.um"}, true)
            .Cases({"rr", "dbr", "ibr", "pkr", "pmc", "pmd"}, true)
            .Cases({"cpuid", "dtr", "itr"}, true)
            .Default(false)) {
      Operands.push_back(IA64Operand::createToken(Name, S));
      Parser.Lex();
      return false;
    }
    break;
  }
  default:
    break;
  }

  const MCExpr *Expr;
  SMLoc E;
  if (Parser.parseExpression(Expr, E))
    return true;
  Operands.push_back(IA64Operand::createImm(Expr, S, E));
  return false;
}

bool IA64AsmParser::parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) {
  MCAsmParser &Parser = getParser();
  if (Parser.getTok().isNot(AsmToken::At))
    return Parser.parsePrimaryExpr(Res, EndLoc, nullptr);

  // "@name(expression)" is a relocation specifier.
  SMLoc S = Parser.getTok().getLoc();
  Parser.Lex(); // Eat the '@'.
  StringRef Name;
  if (Parser.getTok().isNot(AsmToken::Identifier) ||
      Parser.parseIdentifier(Name))
    return Error(S, "expected a name after '@'");
  IA64::Specifier Spec = IA64::parseSpecifierName(Name);
  if (Spec == IA64::S_None)
    return Error(S, "unknown operand '@" + Name + "'");
  const MCExpr *Sub;
  if (Parser.parseToken(AsmToken::LParen,
                        "expected '(' after '@" + Name + "'") ||
      Parser.parseExpression(Sub))
    return true;
  EndLoc = Parser.getTok().getEndLoc();
  if (Parser.parseToken(AsmToken::RParen, "expected ')'"))
    return true;

  // "@ltoff" may wrap a few other specifiers, forming a combined specifier.
  // No others nest.
  if (const auto *Inner = dyn_cast<MCSpecifierExpr>(Sub)) {
    IA64::Specifier Pair = IA64::S_None;
    if (Spec == IA64::S_LTOFF)
      Pair = IA64::getLtoffSpecifier(
          static_cast<IA64::Specifier>(Inner->getSpecifier()));
    if (Pair == IA64::S_None)
      return Error(Inner->getLoc(),
                   "this relocation specifier cannot be inside '@" + Name +
                       "'");
    Spec = Pair;
    Sub = Inner->getSubExpr();
  }
  Res = MCSpecifierExpr::create(Sub, Spec, getContext(), S);
  return false;
}

bool IA64AsmParser::parseInstruction(ParseInstructionInfo &Info, StringRef Name,
                                     SMLoc NameLoc, OperandVector &Operands) {
  MCAsmParser &Parser = getParser();

  // Data directives have no leading dot, so they arrive as instructions.
  ParseStatus Directive = parseDataDirective(Name);
  if (!Directive.isNoMatch())
    return Directive.isFailure();

  // "(pN)" before the mnemonic is the qualifying predicate. The generic
  // parser has consumed the parenthesis and passed it as the name.
  MCRegister QP = IA64::P0;
  SMLoc QPStart = NameLoc, QPEnd = NameLoc;
  ExplicitQP = Name == "(";
  if (ExplicitQP) {
    QPStart = Parser.getTok().getLoc();
    if (!tryParseRegister(QP, QPStart, QPEnd).isSuccess() ||
        !(QP >= IA64::P0 && QP <= IA64::P63))
      return Error(QPStart, "expected a predicate register");
    if (Parser.parseToken(AsmToken::RParen, "expected ')'"))
      return true;
    NameLoc = Parser.getTok().getLoc();
    if (Parser.parseIdentifier(Name))
      return Error(NameLoc, "expected an instruction mnemonic");
    // The generic parser has not lower-cased this name.
    Name = getContext().allocateString(Name.lower());
  }

  SmallVector<std::pair<IA64Operand::KindTy, unsigned>, 3> Completers;
  splitCompleters(Name, Completers);

  Operands.push_back(IA64Operand::createToken(Name, NameLoc));
  Operands.push_back(IA64Operand::createReg(QP, QPStart, QPEnd));
  // Name may be a lower-cased copy, so compute the location from NameLoc.
  SMLoc CompleterLoc =
      SMLoc::getFromPointer(NameLoc.getPointer() + Name.size());
  for (auto [Kind, Value] : Completers)
    Operands.push_back(IA64Operand::createCompleter(Kind, Value, CompleterLoc));

  while (getLexer().isNot(AsmToken::EndOfStatement)) {
    if (parseOperand(Operands))
      return true;
    // Commas only separate operands; "=" is a token of its own.
    if (getLexer().is(AsmToken::Comma))
      Parser.Lex();
  }
  Parser.Lex(); // Consume the EndOfStatement.

  // A shift by a constant is a pseudo-op for dep.z or extr of the remaining
  // bits.
  StringRef Field = StringSwitch<StringRef>(Name)
                        .Case("shl", "dep.z")
                        .Case("shr", "extr")
                        .Case("shr.u", "extr.u")
                        .Default("");
  if (!Field.empty()) {
    auto &Count = static_cast<IA64Operand &>(*Operands.back());
    if (Count.getConstant() && !Count.isUImm<6>())
      return Error(Count.getStartLoc(),
                   "shift count must be an integer in [0, 63]");
    if (Count.isUImm<6>()) {
      SMLoc Loc = Count.getStartLoc();
      const MCExpr *Length =
          MCConstantExpr::create(64 - *Count.getConstant(), getContext());
      Operands[0] = IA64Operand::createToken(Field, NameLoc);
      Operands.push_back(IA64Operand::createImm(Length, Loc, Loc));
    }
  }
  return false;
}

bool IA64AsmParser::parseDataValues(unsigned Size, bool Aligned) {
  MCStreamer &Out = getParser().getStreamer();
  if (getParser().checkForValidSection())
    return true;
  if (Aligned)
    Out.emitValueToAlignment(Align(Size));
  return getParser().parseMany([&]() -> bool {
    SMLoc Loc = getParser().getTok().getLoc();
    const MCExpr *Value;
    if (getParser().parseExpression(Value))
      return true;
    if (const auto *CE = dyn_cast<MCConstantExpr>(Value)) {
      if (!isUIntN(8 * Size, CE->getValue()) &&
          !isIntN(8 * Size, CE->getValue()))
        return Error(Loc, "value does not fit in " + Twine(Size) +
                              (Size == 1 ? " byte" : " bytes"));
      Out.emitIntValue(CE->getValue(), Size);
    } else {
      Out.emitValue(Value, Size, Loc);
    }
    return false;
  });
}

bool IA64AsmParser::parseDataStrings(bool ZeroTerminated) {
  if (getParser().checkForValidSection())
    return true;
  return getParser().parseMany([&]() -> bool {
    std::string Data;
    if (getParser().parseEscapedString(Data))
      return true;
    getParser().getStreamer().emitBytes(Data);
    if (ZeroTerminated)
      getParser().getStreamer().emitBytes(StringRef("\0", 1));
    return false;
  });
}

ParseStatus IA64AsmParser::parseDataDirective(StringRef Name) {
  // GNU as aligns an item to its size, unless the directive ends in ".ua" or
  // says the size in bytes. That goes for ".long" and the like too, which
  // everywhere else leave the location counter alone.
  bool Aligned = !Name.consume_back(".ua");
  unsigned Size = StringSwitch<unsigned>(Name)
                      .Case("data1", 1)
                      .Cases({"data2", ".short", ".word"}, 2)
                      .Cases({"data4", ".long", ".int"}, 4)
                      .Cases({"data8", ".quad"}, 8)
                      .Default(0);
  if (Size && (Aligned || (Name.starts_with("data") && Size != 1)))
    return parseDataValues(Size, Aligned);
  if (Aligned && (Name == "string" || Name == "stringz"))
    return parseDataStrings(Name == "stringz");
  return ParseStatus::NoMatch;
}

ParseStatus IA64AsmParser::parseDirective(AsmToken DirectiveID) {
  StringRef Name = DirectiveID.getString();

  // A template for the next bundle, named by its units, e.g. ".mmi".
  if (Name.size() == 4)
    for (unsigned I = 0; I != std::size(IA64II::BundleTemplates); ++I) {
      const IA64II::BundleTemplate &T = IA64II::BundleTemplates[I];
      if (!T.Name || T.MidStop != -1 ||
          !Name.substr(1).equals_insensitive(T.Name))
        continue;
      if (parseEOL())
        return ParseStatus::Failure;
      getTargetStreamer().emitTemplate(I);
      return ParseStatus::Success;
    }

  ParseStatus Status = parseProcDirective(Name);
  if (!Status.isNoMatch())
    return Status;
  Status = parseUnwindDirective(Name, DirectiveID.getLoc());
  if (!Status.isNoMatch())
    return Status;
  Status = parseAnnotationDirective(Name);
  if (!Status.isNoMatch())
    return Status;
  return parseDataDirective(Name);
}

ParseStatus IA64AsmParser::parseProcDirective(StringRef Name) {
  SMLoc Loc = getTok().getLoc();
  if (Name.equals_insensitive(".proc")) {
    if (!ProcEntries.empty())
      Warning(Loc, "the previous .proc has no .endp");
    ProcEntries.clear();
    SmallVector<MCSymbol *, 2> Entries;
    do {
      SMLoc SymLoc = getTok().getLoc();
      MCSymbol *Sym;
      if (getParser().parseSymbol(Sym))
        return Error(SymLoc, "expected a symbol");
      // Entries must be defined inside their procedure.
      if (Sym->isDefined())
        return Error(SymLoc, "symbol '" + Sym->getName() +
                                 "' is defined before its procedure");
      Entries.push_back(Sym);
    } while (parseOptionalToken(AsmToken::Comma));
    if (parseEOL())
      return ParseStatus::Failure;
    ProcEntries = std::move(Entries);
    UnwindRegion = NoRegion;
    OpenPrologues = 0;
    LabelledStates.clear();
    AwaitsInstruction = SMLoc();
    Awaited = 0;
    getTargetStreamer().emitProc(ProcEntries);
    return ParseStatus::Success;
  }

  if (Name.equals_insensitive(".endp")) {
    if (ProcEntries.empty())
      return Error(Loc, ".endp without a .proc");
    // The names are redundant and only checked.
    while (!getTok().is(AsmToken::EndOfStatement)) {
      SMLoc SymLoc = getTok().getLoc();
      MCSymbol *Sym;
      if (getParser().parseSymbol(Sym))
        return Error(SymLoc, "expected a symbol");
      if (!is_contained(ProcEntries, Sym))
        Warning(SymLoc, "symbol '" + Sym->getName() +
                            "' is not an entry of the procedure");
      if (!parseOptionalToken(AsmToken::Comma))
        break;
    }
    if (parseEOL())
      return ParseStatus::Failure;
    bool Undefined = false;
    const MCSection *Section = getStreamer().getCurrentSectionOnly();
    for (MCSymbol *Entry : ProcEntries)
      if (!Entry->isInSection() || &Entry->getSection() != Section)
        Undefined |= Error(Loc, "symbol '" + Entry->getName() +
                                    "' is not defined in its procedure");
    if (AwaitsInstruction.isValid())
      Undefined |=
          Error(AwaitsInstruction,
                AwaitedByMask ? "fewer instructions follow than this has "
                                "registers"
                              : "no instruction follows for this to tell of");
    if (!Undefined)
      getTargetStreamer().emitEndp();
    ProcEntries.clear();
    return Undefined ? ParseStatus::Failure : ParseStatus::Success;
  }

  return ParseStatus::NoMatch;
}

bool IA64AsmParser::parsePredicateList(unsigned &Count) {
  Count = 0;
  auto ParsePredicate = [&](unsigned &Number) {
    MCRegister Reg;
    SMLoc S = getTok().getLoc(), E;
    if (!tryParseRegister(Reg, S, E).isSuccess() ||
        !getContext()
             .getRegisterInfo()
             ->getRegClass(IA64::PRRegClassID)
             .contains(Reg))
      return Error(S, "expected a predicate register");
    Number = getContext().getRegisterInfo()->getEncodingValue(Reg);
    return false;
  };
  do {
    SMLoc Loc = getTok().getLoc();
    unsigned First, Last;
    if (ParsePredicate(First))
      return true;
    Last = First;
    if (parseOptionalToken(AsmToken::Minus) && ParsePredicate(Last))
      return true;
    if (Last < First)
      return Error(Loc, "range of registers is the wrong way around");
    Count += Last - First + 1;
  } while (parseOptionalToken(AsmToken::Comma));
  return false;
}

ParseStatus IA64AsmParser::parseAnnotationDirective(StringRef Name) {
  // The assembler does not check for dependency violations, so these are
  // only syntax-checked and then discarded.
  std::string Lower = Name.lower();
  unsigned Count;

  // Predicates preserved across calls.
  if (Lower == ".pred.safe_across_calls")
    return parsePredicateList(Count) || parseEOL();

  // Predicate relations: mutex (at most one of the set is true), imply (the
  // first true implies the second), or clear (forget prior relations).
  StringRef Relation;
  SMLoc Loc = getTok().getLoc();
  if (Lower == ".pred.rel") {
    // The relation is a string or an "@"-prefixed name.
    std::string String;
    if (getTok().is(AsmToken::String)) {
      if (getParser().parseEscapedString(String))
        return ParseStatus::Failure;
    } else if (!parseOptionalToken(AsmToken::At) ||
               getParser().parseIdentifier(Relation)) {
      return Error(Loc, "expected a predicate relation");
    } else {
      String = Relation.str();
    }
    if (String != "mutex" && String != "imply" && String != "clear")
      return Error(Loc, "unknown predicate relation");
    if (getParser().parseComma())
      return ParseStatus::Failure;
    Lower += '.' + String;
  }
  if (Lower == ".pred.rel.mutex" || Lower == ".pred.rel.imply" ||
      Lower == ".pred.rel.clear") {
    Loc = getTok().getLoc();
    if (parsePredicateList(Count))
      return ParseStatus::Failure;
    if (Lower == ".pred.rel.mutex" && Count < 2)
      return Error(Loc, "expected at least two predicate registers");
    if (Lower == ".pred.rel.imply" && Count != 2)
      return Error(Loc, "expected two predicate registers");
    return parseEOL();
  }

  // The address of a memory access as an offset and a base identifier.
  if (Lower == ".mem.offset") {
    int64_t Offset, Base;
    return getParser().parseAbsoluteExpression(Offset) ||
           getParser().parseComma() ||
           getParser().parseAbsoluteExpression(Base) || parseEOL();
  }

  // The known value of a register.
  if (Lower == ".reg.val") {
    MCRegister Reg;
    SMLoc S, E;
    int64_t Value;
    return parseRegister(Reg, S, E) || getParser().parseComma() ||
           getParser().parseAbsoluteExpression(Value) || parseEOL();
  }

  // Labels at which code may be entered from elsewhere.
  if (Lower == ".entry") {
    if (getTok().is(AsmToken::EndOfStatement))
      return parseEOL();
    auto ParseOne = [&] {
      SMLoc SymLoc = getTok().getLoc();
      MCSymbol *Sym;
      if (getParser().parseSymbol(Sym))
        return Error(SymLoc, "expected a symbol");
      return false;
    };
    return parseMany(ParseOne);
  }

  // Stops are only placed where the source has them, which is what
  // ".explicit" requests and ".default" reverts to.
  if (Lower == ".explicit" || Lower == ".default")
    return parseEOL();
  if (Lower == ".auto")
    return Error(Loc, "the assembler does not place stops by itself");

  return ParseStatus::NoMatch;
}

/// Parse a register that a prologue can name as saved.
bool IA64AsmParser::parseUnwindReg(IA64TargetStreamer::UnwindReg &Reg) {
  SMLoc S = getTok().getLoc(), E;
  if (getTok().is(AsmToken::At)) {
    if (!getLexer().peekTok().is(AsmToken::Identifier) ||
        !getLexer().peekTok().getString().equals_insensitive("priunat"))
      return Error(S, "expected a register that a prologue saves");
    Lex();
    Lex();
    Reg = IA64TargetStreamer::UR_PRIUNAT;
    return false;
  }
  if (getTok().is(AsmToken::Identifier) &&
      getTok().getIdentifier().equals_insensitive("pr")) {
    Lex();
    Reg = IA64TargetStreamer::UR_PR;
    return false;
  }
  MCRegister MCReg;
  if (tryParseRegister(MCReg, S, E).isSuccess()) {
    switch (MCReg.id()) {
    case IA64::B0:
      Reg = IA64TargetStreamer::UR_RP;
      return false;
    case IA64::AR64:
      Reg = IA64TargetStreamer::UR_PFS;
      return false;
    case IA64::AR36:
      Reg = IA64TargetStreamer::UR_UNAT;
      return false;
    case IA64::AR65:
      Reg = IA64TargetStreamer::UR_LC;
      return false;
    case IA64::AR40:
      Reg = IA64TargetStreamer::UR_FPSR;
      return false;
    case IA64::AR19:
      Reg = IA64TargetStreamer::UR_RNAT;
      return false;
    case IA64::AR17:
      Reg = IA64TargetStreamer::UR_BSP;
      return false;
    case IA64::AR18:
      Reg = IA64TargetStreamer::UR_BSPSTORE;
      return false;
    default:
      break;
    }
  }
  return Error(S, "expected a register that a prologue saves");
}

/// Parse a general register, or with \p OrNumber a bare register number.
bool IA64AsmParser::parseUnwindGR(int64_t &GR, bool OrNumber) {
  SMLoc S = getTok().getLoc(), E;
  MCRegister Reg;
  const MCRegisterInfo &MRI = *getContext().getRegisterInfo();
  if (tryParseRegister(Reg, S, E).isSuccess()) {
    if (!MRI.getRegClass(IA64::GRRegClassID).contains(Reg))
      return Error(S, "expected a general register");
    GR = MRI.getEncodingValue(Reg);
    return false;
  }
  if (!OrNumber || getTok().is(AsmToken::EndOfStatement))
    return Error(S, "expected a general register");
  if (getParser().parseAbsoluteExpression(GR))
    return true;
  if (GR < 0 || GR > 127)
    return Error(S, "expected a general register");
  return false;
}

/// Parse a byte count that the unwind information encodes in units of
/// \p Multiple.
bool IA64AsmParser::parseUnwindOffset(int64_t &Offset, int64_t Min,
                                      unsigned Multiple) {
  SMLoc S = getTok().getLoc();
  if (getParser().parseAbsoluteExpression(Offset))
    return true;
  if (Offset < Min || Offset % Multiple != 0)
    return Error(S, "expected a multiple of " + Twine(Multiple) +
                        " that is at least " + Twine(Min));
  return false;
}

ParseStatus IA64AsmParser::parseUnwindDirective(StringRef Name, SMLoc NameLoc) {
  enum DirectiveTy {
    None,
    Prologue,
    Body,
    FFrame,
    VFrame,
    VFrameSP,
    Spill,
    Save,
    SaveSP,
    SavePSP,
    AltRP,
    UnwABI,
    SaveG,
    SaveF,
    SaveB,
    SaveGF,
    Restore,
    LabelState,
    CopyState,
  };
  std::string Lower = Name.lower();
  DirectiveTy Directive = StringSwitch<DirectiveTy>(Lower)
                              .Case(".prologue", Prologue)
                              .Case(".body", Body)
                              .Case(".fframe", FFrame)
                              .Case(".vframe", VFrame)
                              .Case(".vframesp", VFrameSP)
                              .Case(".spill", Spill)
                              .Case(".save", Save)
                              .Case(".savesp", SaveSP)
                              .Case(".savepsp", SavePSP)
                              .Case(".altrp", AltRP)
                              .Case(".unwabi", UnwABI)
                              .Case(".save.g", SaveG)
                              .Case(".save.f", SaveF)
                              .Case(".save.b", SaveB)
                              .Case(".save.gf", SaveGF)
                              .Case(".restore", Restore)
                              .Case(".label_state", LabelState)
                              .Case(".copy_state", CopyState)
                              .Default(None);
  if (Directive == None)
    return ParseStatus::NoMatch;

  SMLoc Loc = getTok().getLoc();
  if (ProcEntries.empty())
    return Error(NameLoc, Lower + " outside of a procedure");
  IA64TargetStreamer &TS = getTargetStreamer();
  // A mask directive covers as many instructions as it has registers.
  if (Awaited != 0 && AwaitedByMask) {
    Awaited = 0;
    return Error(std::exchange(AwaitsInstruction, SMLoc()),
                 "fewer instructions follow than this has registers");
  }

  if (Directive == Body) {
    if (parseEOL())
      return ParseStatus::Failure;
    UnwindRegion = BodyRegion;
    TS.emitBody();
    return ParseStatus::Success;
  }

  if (Directive == Prologue) {
    // ".prologue" alone, or with a save mask of rp (8), ar.pfs (4), psp (2)
    // and pr (1), and the first GR they are saved in.
    if (UnwindRegion == PrologueRegion)
      return Error(NameLoc, "a prologue is already open");
    int64_t Mask = 0, GRSave = 0;
    if (!getTok().is(AsmToken::EndOfStatement)) {
      if (getParser().parseAbsoluteExpression(Mask))
        return ParseStatus::Failure;
      if (Mask < 0 || Mask > 15)
        return Error(Loc, "expected a mask from 0 to 15");
      if (Mask == 0)
        Warning(Loc, "a mask of 0 is the same as none");
      if (getParser().parseComma() || parseUnwindGR(GRSave, /*OrNumber=*/true))
        return ParseStatus::Failure;
    }
    if (parseEOL())
      return ParseStatus::Failure;
    UnwindRegion = PrologueRegion;
    PrologueMask = Mask;
    PrologueGRSave = GRSave;
    ++OpenPrologues;
    TS.emitPrologue(Mask, GRSave);
    return ParseStatus::Success;
  }

  if (Directive == Restore || Directive == LabelState ||
      Directive == CopyState) {
    if (UnwindRegion != BodyRegion)
      return Error(NameLoc, Lower + " outside of a body");
    int64_t Value = 0;
    if (Directive == Restore) {
      // Only sp is restored this way, and with it the frames of that many
      // more prologues than the innermost.
      MCRegister Reg;
      SMLoc E;
      if (!tryParseRegister(Reg, Loc, E).isSuccess() || Reg != IA64::R12)
        return Error(Loc, "expected sp");
      SMLoc CountLoc = getTok().getLoc();
      if (parseOptionalToken(AsmToken::Comma)) {
        CountLoc = getTok().getLoc();
        if (getParser().parseAbsoluteExpression(Value))
          return ParseStatus::Failure;
      }
      if (Value < 0 || Value >= OpenPrologues)
        return Error(CountLoc, "there are " + Twine(OpenPrologues) +
                                   " prologues to undo");
      if (parseEOL())
        return ParseStatus::Failure;
      OpenPrologues -= Value + 1;
      AwaitsInstruction = NameLoc;
      Awaited = 1;
      AwaitedByMask = false;
      TS.emitRestore(Value);
      return ParseStatus::Success;
    }
    if (getParser().parseAbsoluteExpression(Value))
      return ParseStatus::Failure;
    if (Value < 0)
      return Error(Loc, "expected a label that is not negative");
    if (parseEOL())
      return ParseStatus::Failure;
    if (Directive == LabelState) {
      LabelledStates[Value] = OpenPrologues;
      TS.emitLabelState(Value);
      return ParseStatus::Success;
    }
    auto It = LabelledStates.find(Value);
    if (It == LabelledStates.end())
      return Error(Loc, "no .label_state has given a state this label");
    OpenPrologues = It->second;
    TS.emitCopyState(Value);
    return ParseStatus::Success;
  }

  // The rest tell what the instructions of a prologue do.
  if (UnwindRegion != PrologueRegion)
    return Error(NameLoc, Lower + " outside of a prologue");

  // The GR that the prologue mask assigns to \p Bit, or -1.
  auto MaskGR = [&](unsigned Bit) -> int64_t {
    if (!(PrologueMask & Bit))
      return -1;
    return PrologueGRSave + popcount(PrologueMask & ~(Bit | (Bit - 1)));
  };
  auto CheckMaskGR = [&](unsigned Bit, int64_t &GR, SMLoc GRLoc) {
    int64_t Expected = MaskGR(Bit);
    if (Expected == -1 || Expected == GR)
      return false;
    // GNU as also uses the mask's register, with only a warning.
    GR = Expected;
    return Warning(GRLoc, "the prologue said r" + Twine(Expected));
  };

  int64_t Value = 0, GR = 0;
  IA64TargetStreamer::UnwindReg Reg;
  switch (Directive) {
  case FFrame:
    if (parseUnwindOffset(Value, 0, 16) || parseEOL())
      return ParseStatus::Failure;
    TS.emitFFrame(Value);
    break;
  case VFrame:
    if (parseUnwindGR(GR, /*OrNumber=*/false) || CheckMaskGR(2, GR, Loc) ||
        parseEOL())
      return ParseStatus::Failure;
    TS.emitVFrame(GR);
    break;
  case VFrameSP:
    if (parseUnwindOffset(Value, 0, 4) || parseEOL())
      return ParseStatus::Failure;
    TS.emitVFrameSP(Value);
    break;
  case Spill:
    if (parseUnwindOffset(Value, -16, 4) || parseEOL())
      return ParseStatus::Failure;
    TS.emitSpillBase(Value);
    break;
  case Save: {
    if (parseUnwindReg(Reg) || getParser().parseComma())
      return ParseStatus::Failure;
    SMLoc GRLoc = getTok().getLoc();
    if (parseUnwindGR(GR, /*OrNumber=*/false))
      return ParseStatus::Failure;
    unsigned Bit = Reg == IA64TargetStreamer::UR_RP    ? 8
                   : Reg == IA64TargetStreamer::UR_PFS ? 4
                   : Reg == IA64TargetStreamer::UR_PR  ? 1
                                                       : 0;
    if ((Bit && CheckMaskGR(Bit, GR, GRLoc)) || parseEOL())
      return ParseStatus::Failure;
    TS.emitSave(Reg, GR);
    break;
  }
  case SaveSP:
  case SavePSP:
    if (parseUnwindReg(Reg) || getParser().parseComma() ||
        parseUnwindOffset(Value, Directive == SaveSP ? 0 : -16, 4) ||
        parseEOL())
      return ParseStatus::Failure;
    if (Directive == SaveSP)
      TS.emitSaveSP(Reg, Value);
    else
      TS.emitSavePSP(Reg, Value);
    break;
  case AltRP: {
    MCRegister BR;
    SMLoc E;
    if (!tryParseRegister(BR, Loc, E).isSuccess() ||
        !getContext()
             .getRegisterInfo()
             ->getRegClass(IA64::BRRegClassID)
             .contains(BR))
      return Error(Loc, "expected a branch register");
    if (parseEOL())
      return ParseStatus::Failure;
    TS.emitAltRP(getContext().getRegisterInfo()->getEncodingValue(BR));
    break;
  }
  case UnwABI: {
    // The ABI is a number or one of the named values @svr4, @hpux, @nt.
    if (getTok().is(AsmToken::At)) {
      Value = StringSwitch<int64_t>(getLexer().peekTok().getString().lower())
                  .Case("svr4", 0)
                  .Case("hpux", 1)
                  .Case("nt", 2)
                  .Default(-1);
      if (Value == -1)
        return Error(Loc, "expected @svr4, @hpux, @nt or a number");
      Lex();
      Lex();
    } else if (getParser().parseAbsoluteExpression(Value)) {
      return ParseStatus::Failure;
    } else if (Value < 0 || Value > 255) {
      return Error(Loc, "expected a number from 0 to 255");
    }
    int64_t Context;
    if (getParser().parseComma())
      return ParseStatus::Failure;
    SMLoc ContextLoc = getTok().getLoc();
    if (getParser().parseAbsoluteExpression(Context))
      return ParseStatus::Failure;
    if (Context < 0 || Context > 255)
      return Error(ContextLoc, "expected a number from 0 to 255");
    if (parseEOL())
      return ParseStatus::Failure;
    TS.emitUnwABI(Value, Context);
    break;
  }
  case SaveG:
  case SaveF:
  case SaveB:
  case SaveGF: {
    // Callee-saved register masks: r4-r7, f2-f5 and f16-f31, and b1-b5.
    int64_t Mask[2] = {0, 0};
    int64_t Limit[2] = {Directive == SaveG || Directive == SaveGF ? 0xf
                        : Directive == SaveF                      ? 0xfffff
                                                                  : 0x1f,
                        0xfffff};
    unsigned Count = Directive == SaveGF ? 2 : 1;
    for (unsigned I = 0; I != Count; ++I) {
      if (I != 0 && getParser().parseComma())
        return ParseStatus::Failure;
      SMLoc MaskLoc = getTok().getLoc();
      if (getParser().parseAbsoluteExpression(Mask[I]))
        return ParseStatus::Failure;
      if (Mask[I] < (Count == 1) || Mask[I] > Limit[I])
        return Error(MaskLoc, "expected a mask from " + Twine(Count == 1) +
                                  " to 0x" + Twine::utohexstr(Limit[I]));
    }
    if (Count == 2 && Mask[0] == 0 && Mask[1] == 0)
      return Error(Loc, "expected a register in one mask at least");
    // GRs and BRs may be saved in GRs.
    std::optional<unsigned> SavedIn;
    if (Directive == SaveG || Directive == SaveB) {
      if (parseOptionalToken(AsmToken::Comma)) {
        if (parseUnwindGR(GR, /*OrNumber=*/false))
          return ParseStatus::Failure;
        SavedIn = GR;
      }
    }
    if (parseEOL())
      return ParseStatus::Failure;
    if (Directive == SaveG)
      TS.emitSaveG(Mask[0], SavedIn);
    else if (Directive == SaveF)
      TS.emitSaveF(Mask[0]);
    else if (Directive == SaveB)
      TS.emitSaveB(Mask[0], SavedIn);
    else
      TS.emitSaveGF(Mask[0], Mask[1]);
    Awaited = popcount(uint64_t(Mask[0])) + popcount(uint64_t(Mask[1]));
    AwaitedByMask = true;
    AwaitsInstruction = NameLoc;
    return ParseStatus::Success;
  }

  default:
    llvm_unreachable("not a directive of a prologue");
  }
  if (Directive != Spill && Directive != AltRP && Directive != UnwABI) {
    AwaitsInstruction = NameLoc;
    Awaited = 1;
    AwaitedByMask = false;
  }
  return ParseStatus::Success;
}

void IA64AsmParser::onEmptyStatement(const AsmToken &Terminator) {
  // ";" separates statements, so ";;" arrives as two statements, the second
  // empty and starting right after the first's terminator. That is a stop,
  // ending the instruction group.
  // At the first byte of the buffer, there is no previous character.
  StringRef Str = Terminator.getString();
  const SourceMgr &SM = getSourceManager();
  unsigned Buffer = SM.FindBufferContainingLoc(Terminator.getLoc());
  if (Str != ";" || !Buffer ||
      Str.data() == SM.getMemoryBuffer(Buffer)->getBufferStart() ||
      Str.data()[-1] != ';')
    return;
  getTargetStreamer().emitStop();
}

unsigned IA64AsmParser::validateTargetOperandClass(MCParsedAsmOperand &AsmOp,
                                                   unsigned Kind) {
  IA64Operand &Op = static_cast<IA64Operand &>(AsmOp);

  // Literal integers in instruction syntax, such as the "1" of
  // "add r1 = r2, r3, 1", are parsed as ordinary immediates.
  switch (Kind) {
  case MCK_1:
    return Op.getConstant() == 1 ? Match_Success : Match_InvalidOperand;
  case MCK_8:
    return Op.getConstant() == 8 ? Match_Success : Match_InvalidOperand;
  case MCK_16:
    return Op.getConstant() == 16 ? Match_Success : Match_InvalidOperand;
  default:
    break;
  }
  return Match_InvalidOperand;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64AsmParser() {
  RegisterMCAsmParser<IA64AsmParser> X(getTheIA64Target());
}
