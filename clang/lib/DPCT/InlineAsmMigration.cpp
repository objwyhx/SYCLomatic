//===--------------------- InlineAsmMigration.cpp--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "InlineAsmMigration.h"
#include "AnalysisInfo.h"
#include "CallExprRewriter.h"
// #include "InlineAsmParser.h"
#include "MigrationRuleManager.h"
// #include "llvm/MC/MCContext.h"
// #include "llvm/MC/MCParser/MCAsmParser.h"
// #include "llvm/MC/TargetRegistry.h"
// #include "llvm/MC/MCRegisterInfo.h"
// #include "llvm/MC/MCAsmInfo.h"
// #include "llvm/MC/MCSubtargetInfo.h"
// #include "llvm/MC/MCSubtargetInfo.h"
// #include "llvm/Support/TargetSelect.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/Options.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Triple.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/MC/MCAsmBackend.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSectionMachO.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace clang;
using namespace clang::dpct;

static const AsmStmt *CurrentStmt;
static AsmRule *CurrentRule;
using ReplaceFuncTy = void (AsmRule::*)(TextModification *);
static ReplaceFuncTy replace_func; 

extern "C" void migrate(unsigned &Opcode, OperandVector &Operands, bool MatchingInlineAsm) {
  errs() << raw_ostream::MAGENTA << "Callback Success" << raw_ostream::RESET << "\n";
  for (const auto &Op : Operands) {
    Op->print(errs());
    errs() << "\n";
  }

  // struct TempAsmRule : public AsmRule {
  //   void operator()() {
  //     emplaceTransformation(new ReplaceStmt(CurrentStmt, "// This is a NVPTX assembly replacement."));
  //   }
  // };
  
  // TempAsmRule Temp;
  // Temp();

  (CurrentRule->*replace_func)(new ReplaceStmt(CurrentStmt, "// This is a NVPTX assembly replacement."));
}

void actionOnGCCAsmStmt(const GCCAsmStmt *Asm) {
  const auto &C = DpctGlobalInfo::getContext();
  std::string S  = Asm->generateAsmString(C);
  unsigned DiagOffsets = 0;
  SmallVector<GCCAsmStmt::AsmStringPiece> Pieces;
  if (Asm->AnalyzeAsmString(Pieces, C, DiagOffsets)) {
    llvm::errs() << llvm::raw_ostream::RED << "Invalid inline asm" << llvm::raw_ostream::RESET << "\n";
    return;
  }

  for (const auto &P : Pieces) {
    if (P.isString())
      llvm::errs() << llvm::raw_ostream::BLUE << " S " << P.getString() << llvm::raw_ostream::RESET << "\n";
    else
      llvm::errs() << llvm::raw_ostream::BLUE << " O " << P.getOperandNo() << " " << (P.getModifier() != 0 ? P.getModifier() : '0') << llvm::raw_ostream::RESET << "\n";;
  }

  // AsmToken Tok;
  // AsmLexer Lexer(S);
  // do {
  //   Tok = Lexer.Lex();
  //   Tok.dump(llvm::errs());
  //   llvm::errs() << "\n";
  // } while (Tok.isNot(AsmToken::Eof));

  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmParsers();

  std::string Error;
  SourceMgr SrcMgr;
  MCTargetOptions MCOptions;
  SrcMgr.AddNewSourceBuffer(MemoryBuffer::getMemBuffer(S), SMLoc());
  const static std::string Triple = "nvptx64-nvidia-cuda";
  const Target *TheTarget = TargetRegistry::lookupTarget(Triple, Error);

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(Triple));
  std::unique_ptr<MCAsmInfo> MAI(TheTarget->createMCAsmInfo(*MRI, Triple, MCOptions));

  std::string Out;
  raw_pwrite_stream *OS = &llvm::errs();
  std::unique_ptr<formatted_raw_ostream> FOut = std::make_unique<formatted_raw_ostream>(*OS);
  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(Triple, "", ""));
  MCContext Ctx(llvm::Triple(Triple), MAI.get(), MRI.get(), STI.get(), &SrcMgr, &MCOptions);
  std::unique_ptr<MCInstrInfo> MCII(TheTarget->createMCInstrInfo());
  std::unique_ptr<MCStreamer> Str;
  MCInstPrinter *IP = TheTarget->createMCInstPrinter(
        llvm::Triple(Triple), false, *MAI, *MCII, *MRI);
  std::unique_ptr<MCObjectFileInfo> MOFI(
      TheTarget->createMCObjectFileInfo(Ctx, true));
    std::unique_ptr<MCCodeEmitter> CE(TheTarget->createMCCodeEmitter(*MCII, Ctx));

    std::unique_ptr<MCAsmBackend> MAB(
        TheTarget->createMCAsmBackend(*STI, *MRI, MCOptions));

    Str.reset(TheTarget->createAsmStreamer(
        Ctx, std::move(FOut), /*asmverbose*/ true,
        /*useDwarfDirectory*/ true, IP, std::move(CE), std::move(MAB), false));
  Ctx.setObjectFileInfo(MOFI.get());
  Str.get()->initSections(true, *STI);
  std::unique_ptr<MCAsmParser> Parser(
      createMCAsmParser(SrcMgr, Ctx, *Str.get(), *MAI));
  std::unique_ptr<MCTargetAsmParser> TAP(
      TheTarget->createMCAsmParser(*STI, *Parser, *MCII, MCOptions));
  std::cout << TAP.get() << std::endl;
  Parser->setTargetParser(*TAP.get());
  //Parser->setShowParsedOperands(true);
  Parser->Run(false);
}

void AsmRule::registerMatcher(ast_matchers::MatchFinder &MF) {
  using namespace clang::ast_matchers;
  MF.addMatcher(
      asmStmt(hasAncestor(functionDecl(
                  anyOf(hasAttr(attr::CUDADevice), hasAttr(attr::CUDAGlobal)))))
          .bind("asm"),
      this);
}

bool canAsmLop3ExprFast(std::ostringstream &OS, const std::string &a,
                        const std::string &b, const std::string &c,
                        const std::uint8_t imm) {
#define EMPTY4 "", "", "", ""
#define EMPTY16 EMPTY4, EMPTY4, EMPTY4, EMPTY4
  static const std::string FastMap[256] = {
      /*0x00*/ "0",
      // clang-format off
      EMPTY16, EMPTY4, EMPTY4, "",
      /*0x1a*/ "(@a & @b | @c) ^ @a",
      "", "", "",
      /*0x1e*/ "@a ^ (@b | @c)",
      EMPTY4, EMPTY4, EMPTY4, "", "",
      /*0x2d*/ "~@a ^ (~@b & @c)",
      EMPTY16, "", "",
      /*0x40*/ "@a & @b & ~@c",
      EMPTY16, EMPTY16, EMPTY16, EMPTY4, "", "", "",
      /*0x78*/ "@a ^ (@b & @c)",
      EMPTY4, "", "", "",
      /*0x80*/ "@a & @b & @c",
      EMPTY16, EMPTY4, "",
      /*0x96*/ "@a ^ @b ^ @c",
      EMPTY16, EMPTY4, EMPTY4, EMPTY4, "",
      /*0xb4*/ "@a ^ (@b & ~@c)",
      "", "", "",
      /*0xb8*/ "(@a ^ (@b & (@c ^ @a)))",
      EMPTY16, EMPTY4, EMPTY4, "",
      /*0xd2*/ "@a ^ (~@b & @c)",
      EMPTY16, EMPTY4, "",
      /*0xe8*/ "((@a & (@b | @c)) | (@b & @c))",
      "",
      /*0xea*/ "(@a & @b) | @c",
      EMPTY16, "", "", "",
      // clang-format on
      /*0xfe*/ "@a | @b | @c",
      /*0xff*/ "1"};
#undef EMPTY16
#undef EMPTY4
  const StringRef Expr = FastMap[imm];
  if (Expr.empty()) {
    return false;
  }
  OS << " ";
  const StringRef ReplaceMap[3] = {a, b, c};
  std::string::size_type Pre = 0;
  auto Pos = Expr.find('@');
  while (Pos != std::string::npos) {
    OS << Expr.substr(Pre, Pos - Pre).str();
    OS << ReplaceMap[Expr[Pos + 1] - 'a'].str();
    Pre = Pos + 2;
    Pos = Expr.find('@', Pre);
  }
  OS << Expr.substr(Pre).str();
  return true;
}

std::string getAsmLop3Expr(const llvm::SmallVector<std::string, 5> &Operands) {
  if (Operands.size() != 5) {
    return "";
  }
  const auto &d = Operands[0];
  const auto &a = Operands[1];
  const auto &b = Operands[2];
  const auto &c = Operands[3];
  auto imm = std::stoi(Operands[4], 0, 16);
  assert(imm >= 0 && imm <= UINT8_MAX);
  std::ostringstream OS;
  OS << d << " =";
  if (canAsmLop3ExprFast(OS, a, b, c, imm)) {
    return OS.str();
  }
  if (imm & 0x01)
    OS << " (~" << a << " & ~" << b << " & ~" << c << ") |";
  if (imm & 0x02)
    OS << " (~" << a << " & ~" << b << " & " << c << ") |";
  if (imm & 0x04)
    OS << " (~" << a << " & " << b << " & ~" << c << ") |";
  if (imm & 0x08)
    OS << " (~" << a << " & " << b << " & " << c << ") |";
  if (imm & 0x10)
    OS << " (" << a << " & ~" << b << " & ~" << c << ") |";
  if (imm & 0x20)
    OS << " (" << a << " & ~" << b << " & " << c << ") |";
  if (imm & 0x40)
    OS << " (" << a << " & " << b << " & ~" << c << ") |";
  if (imm & 0x80)
    OS << " (" << a << " & " << b << " & " << c << ") |";
  auto ret = OS.str();
  return ret.replace(ret.length() - 2, 2, "");
}

void AsmRule::runRule(const ast_matchers::MatchFinder::MatchResult &Result) {
  if (auto *AS = getNodeAsType<AsmStmt>(Result, "asm")) {
    CurrentStmt = AS;
    replace_func = &AsmRule::emplaceTransformation;
    CurrentRule = this;
    if (const auto *GCC = dyn_cast<GCCAsmStmt>(AS))
      actionOnGCCAsmStmt(GCC);
    auto AsmString = AS->generateAsmString(*Result.Context);
    auto TemplateString = StringRef(AsmString).substr(0, AsmString.find(';'));
    auto CurrIndex = TemplateString.find(' ');
    auto OpCode = TemplateString.substr(0, CurrIndex);
    if (OpCode == "lop3.b32") {
      // ASM instruction pattern: lop3.b32 d, a, b, c, immLut;
      llvm::SmallVector<std::string, 4> Args;
      for (const auto *const it : AS->children()) {
        ExprAnalysis EA;
        EA.analyze(cast<Expr>(it));
        if (isa<IntegerLiteral>(it) || isa<DeclRefExpr>(it) ||
            isa<ImplicitCastExpr>(it)) {
          Args.push_back(EA.getReplacedString());
        } else {
          Args.push_back("(" + EA.getReplacedString() + ")");
        }
      }
      llvm::SmallVector<std::string, 5> Operands;
      auto PreIndex = CurrIndex;
      CurrIndex = TemplateString.find(",", PreIndex);
      // Clang will generate the ASM instruction into a string like this:
      // Cuda code: asm("lop3.b32 %0, %1*%1, %1, 3, 0x1A;" : "=r"(b) : "r"(a));
      // TemplateString: "lop3.b32 $0, $1*$1,$ 1, 3, 0x1A"
      while (PreIndex != StringRef::npos) {
        auto TempStr =
            TemplateString.substr(PreIndex + 1, CurrIndex - PreIndex - 1).str();
        // Replace all args, example: the "$1*$1" will be replace by "(a*a)".
        if (TempStr.find('$') != TempStr.length() - 2 && Operands.size() != 4) {
          // When the operands only contain a register, or is the last imm, not
          // need add the paren.
          TempStr = "(" + TempStr + ")";
        }
        auto ArgIndex = TempStr.find('$');
        while (ArgIndex != std::string::npos) {
          // The PTX Instructions has mostly 4 parameters, so just use the char
          // after '$'.
          auto ArgNo = TempStr[ArgIndex + 1] - '0';
          TempStr.replace(ArgIndex, 2, Args[ArgNo]);
          ArgIndex = TempStr.find('$');
        }
        Operands.push_back(std::move(TempStr));
        PreIndex = CurrIndex;
        CurrIndex = TemplateString.find(",", PreIndex + 1);
      }
      auto Replacement = getAsmLop3Expr(Operands);
      if (!Replacement.empty()) {
        return emplaceTransformation(new ReplaceStmt(AS, Replacement));
      }
    }
    report(AS->getAsmLoc(), Diagnostics::DEVICE_ASM, true);
  }
  return;
}
