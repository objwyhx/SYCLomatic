//===-- NVPTXAsmParser.cpp - Parse NVPTX assembly to MCInst instructions --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/NVPTXInstPrinter.h"
#include "MCTargetDesc/NVPTXMCTargetDesc.h"
#include "MCTargetDesc/NVPTXMCAsmInfo.h"
#include "MCTargetDesc/NVPTXTargetStreamer.h"
#include "TargetInfo/NVPTXTargetInfo.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCParser/MCAsmLexer.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCParsedAsmOperand.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

extern "C" void migrate(unsigned &Opcode, OperandVector &Operands, bool MatchingInlineAsm);

namespace {
class NVPTXAsmParser : public MCTargetAsmParser {
  ParseInstructionInfo *InstInfo;
  MCAsmParser &Parser;
private:
  SMLoc consumeToken() {
    MCAsmParser &Parser = getParser();
    SMLoc Result = Parser.getTok().getLoc();
    Parser.Lex();
    return Result;
  }

  NVPTXTargetStreamer &getTargetStreamer() {
    assert(getParser().getStreamer().getTargetStreamer() &&
           "do not have a target streamer");
    MCTargetStreamer &TS = *getParser().getStreamer().getTargetStreamer();
    return static_cast<NVPTXTargetStreamer &>(TS);
  }

public:
  NVPTXAsmParser(const MCSubtargetInfo &STI, MCAsmParser &Parser, const MCInstrInfo &MII, const MCTargetOptions &Options)
    : MCTargetAsmParser(Options, STI, MII), Parser(Parser) {
    setAvailableFeatures(getSTI().getFeatureBits());
  }

  virtual ~NVPTXAsmParser();

  bool parseRegister(MCRegister &RegNo, SMLoc &StartLoc,
                     SMLoc &EndLoc) override {return true;}
  OperandMatchResultTy tryParseRegister(MCRegister &RegNo, SMLoc &StartLoc,
                                        SMLoc &EndLoc) override {}

  bool parsePrimaryExpr(const MCExpr *&Res, SMLoc &EndLoc) override {return true;}

  bool ParseInstruction(ParseInstructionInfo &Info, StringRef Name,
                        SMLoc NameLoc, OperandVector &Operands) override;// {}

  bool ParseDirective(AsmToken DirectiveID) override {return true;}

  bool MatchAndEmitInstruction(SMLoc IDLoc, unsigned &Opcode,
                                       OperandVector &Operands, MCStreamer &Out,
                                       uint64_t &ErrorInfo,
                                       bool MatchingInlineAsm) override {
                                        errs() << raw_ostream::RED << "Handle Inst!" << raw_ostream::RESET << "\n";
                                        migrate(Opcode, Operands, MatchingInlineAsm);
                                        return true;
                                       }
  void convertToMapAndConstraints(unsigned Kind,
                                          const OperandVector &Operands) {

  }
};

class NVPTXOperand : public MCParsedAsmOperand {
public:
  enum KindTy {
    Invalid,
    Token,
    Register,
    Immediate,
    Memory,
  };
private:
  KindTy Kind;
  SMLoc Start, End;
  void *OpDecl;
  
  struct TokOp {
    const char *Data;
    unsigned Length;
  };

  struct RegOp {
    unsigned RegNo;
  };

  struct ImmOp {
    const MCExpr *Val;
    bool LocalRef;
  };

  struct MemOp {
    unsigned SegReg;
    const MCExpr *Disp;
    unsigned BaseReg;
    unsigned DefaultBaseReg;
    unsigned IndexReg;
    unsigned Scale;
    unsigned Size;
    unsigned ModeSize;
  };

  union {
    TokOp Tok;
    RegOp Reg;
    ImmOp Imm;
    MemOp Mem;
  };
public:
  NVPTXOperand(KindTy K, SMLoc Start, SMLoc End)
    : Kind(K), Start(Start), End(End), OpDecl(nullptr) {
      Tok.Data = "ABCD";
      Tok.Length = 4;
    }
  
  /// getStartLoc - Get the location of the first token of this operand.
  SMLoc getStartLoc() const override { return Start; }

  /// getEndLoc - Get the location of the last token of this operand.
  SMLoc getEndLoc() const override { return End; }

  /// getLocRange - Get the range between the first and last token of this
  /// operand.
  SMRange getLocRange() const { return SMRange(Start, End); }
  
  bool isToken() const override {return Kind == Token; }

  bool isImm() const override { return Kind == Immediate; }

  bool isMem() const override { return Kind == Memory; }

  bool isReg() const override { return Kind == Register; }

  unsigned getReg() const override {
    return 0;
  }

  void print(raw_ostream &OS) const override {
    OS << "HAHHAHA\n";
  }

  static std::unique_ptr<NVPTXOperand> createReg(unsigned RegNo, SMLoc StartLoc, SMLoc EndLoc,
            bool AddressOf = false, SMLoc OffsetOfLoc = SMLoc(),
            StringRef SymName = StringRef(), void *OpDecl = nullptr);
};

} // anonymous namespace

extern "C" LLVM_EXTERNAL_VISIBILITY void LLVMInitializeNVPTXAsmParser() {
  RegisterMCAsmParser<NVPTXAsmParser> X(getTheNVPTXTarget32());
  RegisterMCAsmParser<NVPTXAsmParser> Y(getTheNVPTXTarget64());
}

NVPTXAsmParser::~NVPTXAsmParser() {}
#include "../NVPTX.h"
bool NVPTXAsmParser::ParseInstruction(ParseInstructionInfo &Info, StringRef Name, SMLoc NameLoc, OperandVector &Operands) {
  SMLoc Start = getLexer().getLoc();
  errs() << raw_ostream::RED << "Hello, I'm NVPTX assembly Parser!" << raw_ostream::RESET << "\n";
  //Operands.push_back(NVPTXOperand::createToken(Name, Start, Start));
  InstInfo = &Info;
  InstInfo->AsmRewrites->emplace_back(AOK_Label, getTok().getLoc(), 17, "LABBBBBBBBBBBBBEL");
  Operands.push_back(std::make_unique<NVPTXOperand>(NVPTXOperand::Token, getTok().getLocRange().Start, getTok().getLocRange().End));
  for (const auto &Op : Operands) {
    Op->print(errs());
    errs() << "\n";
  }
  errs() << "\n";
  NVPTX::CVT_bf16_f32;
  return false;
}
