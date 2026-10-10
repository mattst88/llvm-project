//===-- IA64AsmParser.cpp - Parse IA64 assembly to MCInst instructions ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/IA64BaseInfo.h"
#include "MCTargetDesc/IA64MCTargetDesc.h"
#include "MCTargetDesc/IA64TargetStreamer.h"
#include "TargetInfo/IA64TargetInfo.h"
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
  enum KindTy { Token, Register, Immediate } Kind;
  SMLoc StartLoc, EndLoc;
  StringRef Tok;
  MCRegister Reg;
  const MCExpr *Imm = nullptr;

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
  template <unsigned N> bool isSImm() const {
    std::optional<int64_t> V = getConstant();
    return V && isInt<N>(*V);
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
    if (std::optional<int64_t> V = getConstant())
      Inst.addOperand(MCOperand::createImm(*V));
    else
      Inst.addOperand(MCOperand::createExpr(getImm()));
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
    case Match_InvalidCount1To3:
      return Error(ErrorLoc, "shift count must be an integer in [1, 3]");
    default:
      return Error(ErrorLoc, "shift count must be an integer in [1, 4]");
    }
  }
  }
  llvm_unreachable("unknown match result");
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
  case AsmToken::Identifier: {
    MCRegister Reg;
    SMLoc E;
    if (tryParseRegister(Reg, S, E).isSuccess()) {
      Operands.push_back(IA64Operand::createReg(Reg, S, E));
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

  Operands.push_back(IA64Operand::createToken(Name, NameLoc));
  Operands.push_back(IA64Operand::createReg(QP, QPStart, QPEnd));

  while (getLexer().isNot(AsmToken::EndOfStatement)) {
    if (parseOperand(Operands))
      return true;
    // Commas only separate operands; "=" is a token of its own.
    if (getLexer().is(AsmToken::Comma))
      Parser.Lex();
  }
  Parser.Lex(); // Consume the EndOfStatement.
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
  if (Kind == MCK_1 && Op.getConstant() == 1)
    return Match_Success;
  return Match_InvalidOperand;
}

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeIA64AsmParser() {
  RegisterMCAsmParser<IA64AsmParser> X(getTheIA64Target());
}
