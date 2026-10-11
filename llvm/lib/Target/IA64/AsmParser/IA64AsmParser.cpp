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
  if (Inst.getOpcode() != IA64::ALLOC)
    return false;

  // Operands are the mnemonic, the predicate, the destination, "=" and
  // ar.pfs; the four sizes follow.
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

ParseStatus IA64AsmParser::parseDirective(AsmToken DirectiveID) {
  return ParseStatus::NoMatch;
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
