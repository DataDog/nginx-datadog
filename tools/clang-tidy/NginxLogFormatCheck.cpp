#include "NginxLogFormatCheck.h"

#include <cctype>
#include <string>

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

using namespace clang::ast_matchers;

namespace clang::tidy::nginx_datadog {
namespace {

enum class ExpectedKind {
  SizeT,
  SSizeT,
  OffT,
  TimeT,
  NgxPidT,
  NgxMsecT,
  NgxIntT,
  NgxUIntT,
  NgxAtomicIntT,
  NgxAtomicUIntT,
  Int,
  UInt,
  Long,
  ULong,
  Int32,
  UInt32,
  Int64,
  UInt64,
  Double,
  CharInt,
  CharacterPointer,
  VoidPointer,
  NgxStrPointer,
  NgxVariableValuePointer,
  RLimT,
};

struct ExpectedArg {
  ExpectedKind Kind;
  std::string Specifier;
  unsigned Offset;
};

struct FormatIssue {
  std::string Message;
  unsigned Offset;
};

struct FormatParseResult {
  llvm::SmallVector<ExpectedArg, 8> Args;
  llvm::SmallVector<FormatIssue, 2> Issues;
};

bool isDigit(char C) { return C >= '0' && C <= '9'; }

std::string expectedTypeName(ExpectedKind Kind) {
  switch (Kind) {
    case ExpectedKind::SizeT:
      return "size_t";
    case ExpectedKind::SSizeT:
      return "ssize_t";
    case ExpectedKind::OffT:
      return "off_t";
    case ExpectedKind::TimeT:
      return "time_t";
    case ExpectedKind::NgxPidT:
      return "ngx_pid_t";
    case ExpectedKind::NgxMsecT:
      return "ngx_msec_t";
    case ExpectedKind::NgxIntT:
      return "ngx_int_t";
    case ExpectedKind::NgxUIntT:
      return "ngx_uint_t";
    case ExpectedKind::NgxAtomicIntT:
      return "ngx_atomic_int_t";
    case ExpectedKind::NgxAtomicUIntT:
      return "ngx_atomic_uint_t";
    case ExpectedKind::Int:
      return "int";
    case ExpectedKind::UInt:
      return "u_int";
    case ExpectedKind::Long:
      return "long";
    case ExpectedKind::ULong:
      return "u_long";
    case ExpectedKind::Int32:
      return "int32_t";
    case ExpectedKind::UInt32:
      return "uint32_t";
    case ExpectedKind::Int64:
      return "int64_t";
    case ExpectedKind::UInt64:
      return "uint64_t";
    case ExpectedKind::Double:
      return "double";
    case ExpectedKind::CharInt:
      return "int";
    case ExpectedKind::CharacterPointer:
      return "u_char *";
    case ExpectedKind::VoidPointer:
      return "void *";
    case ExpectedKind::NgxStrPointer:
      return "ngx_str_t *";
    case ExpectedKind::NgxVariableValuePointer:
      return "ngx_variable_value_t *";
    case ExpectedKind::RLimT:
      return "rlim_t";
  }
  return "";
}

FormatParseResult parseFormatString(llvm::StringRef Format) {
  FormatParseResult Result;

  for (unsigned I = 0; I < Format.size(); ++I) {
    if (Format[I] != '%') {
      continue;
    }

    const unsigned Percent = I++;
    if (I >= Format.size()) {
      Result.Issues.push_back(
          {"unterminated nginx log format specifier", Percent});
      break;
    }

    if (Format[I] == '0') {
      ++I;
    }

    while (I < Format.size() && isDigit(Format[I])) {
      ++I;
    }

    bool Signed = true;
    bool DoneWithModifiers = false;

    while (I < Format.size() && !DoneWithModifiers) {
      switch (Format[I]) {
        case 'u':
          Signed = false;
          ++I;
          break;
        case 'm':
          ++I;
          break;
        case 'x':
        case 'X':
          Signed = false;
          ++I;
          break;
        case '.':
          ++I;
          while (I < Format.size() && isDigit(Format[I])) {
            ++I;
          }
          DoneWithModifiers = true;
          break;
        case '*':
          Result.Args.push_back({ExpectedKind::SizeT, "%*", I});
          ++I;
          break;
        default:
          DoneWithModifiers = true;
          break;
      }
    }

    if (I >= Format.size()) {
      Result.Issues.push_back(
          {"unterminated nginx log format specifier", Percent});
      break;
    }

    const char Specifier = Format[I];
    const std::string Spelling = Format.substr(Percent, I - Percent + 1).str();

    switch (Specifier) {
      case 'V':
        Result.Args.push_back({ExpectedKind::NgxStrPointer, Spelling, Percent});
        break;
      case 'v':
        Result.Args.push_back(
            {ExpectedKind::NgxVariableValuePointer, Spelling, Percent});
        break;
      case 's':
        Result.Args.push_back(
            {ExpectedKind::CharacterPointer, Spelling, Percent});
        break;
      case 'O':
        Result.Args.push_back({ExpectedKind::OffT, Spelling, Percent});
        break;
      case 'P':
        Result.Args.push_back({ExpectedKind::NgxPidT, Spelling, Percent});
        break;
      case 'T':
        Result.Args.push_back({ExpectedKind::TimeT, Spelling, Percent});
        break;
      case 'M':
        Result.Args.push_back({ExpectedKind::NgxMsecT, Spelling, Percent});
        break;
      case 'z':
        Result.Args.push_back(
            {Signed ? ExpectedKind::SSizeT : ExpectedKind::SizeT, Spelling,
             Percent});
        break;
      case 'i':
        Result.Args.push_back(
            {Signed ? ExpectedKind::NgxIntT : ExpectedKind::NgxUIntT, Spelling,
             Percent});
        break;
      case 'd':
        Result.Args.push_back({Signed ? ExpectedKind::Int : ExpectedKind::UInt,
                               Spelling, Percent});
        break;
      case 'l':
        Result.Args.push_back(
            {Signed ? ExpectedKind::Long : ExpectedKind::ULong, Spelling,
             Percent});
        break;
      case 'D':
        Result.Args.push_back(
            {Signed ? ExpectedKind::Int32 : ExpectedKind::UInt32, Spelling,
             Percent});
        break;
      case 'L':
        Result.Args.push_back(
            {Signed ? ExpectedKind::Int64 : ExpectedKind::UInt64, Spelling,
             Percent});
        break;
      case 'A':
        Result.Args.push_back({Signed ? ExpectedKind::NgxAtomicIntT
                                      : ExpectedKind::NgxAtomicUIntT,
                               Spelling, Percent});
        break;
      case 'f':
        Result.Args.push_back({ExpectedKind::Double, Spelling, Percent});
        break;
      case 'r':
        Result.Args.push_back({ExpectedKind::RLimT, Spelling, Percent});
        break;
      case 'p':
        Result.Args.push_back({ExpectedKind::VoidPointer, Spelling, Percent});
        break;
      case 'c':
        Result.Args.push_back({ExpectedKind::CharInt, Spelling, Percent});
        break;
      case 'Z':
      case 'N':
      case '%':
        break;
      default:
        Result.Issues.push_back(
            {"unsupported nginx log format specifier '" + Spelling + "'",
             Percent});
        break;
    }
  }

  return Result;
}

QualType unqualifiedNonReference(QualType QT) {
  if (QT->isReferenceType()) {
    QT = QT->getPointeeType();
  }

  return QT.getUnqualifiedType();
}

bool hasTypedefName(QualType QT, llvm::StringRef Name) {
  QT = unqualifiedNonReference(QT);
  llvm::SmallPtrSet<const clang::Type*, 8> Seen;

  while (!QT.isNull()) {
    // strip const, volatile, restrict, _Atomic
    QT = QT.getUnqualifiedType();
    const clang::Type* TypePtr = QT.getTypePtrOrNull();
    if (TypePtr == nullptr || !Seen.insert(TypePtr).second) {
      return false;
    }

    if (const auto* Typedef = TypePtr->getAs<TypedefType>()) {
      if (Typedef->getDecl()->getName() == Name) {
        return true;
      }
      // if looking at B, with typedef A B, resolve to A
      QT = Typedef->desugar();
      continue;
    }

    if (const auto* Elaborated = dyn_cast<ElaboratedType>(TypePtr)) {
      // struct/class/enum B -> B
      QT = Elaborated->getNamedType();
      continue;
    }

    if (const auto* Attributed = dyn_cast<AttributedType>(TypePtr)) {
      // remove attributes
      QT = Attributed->getModifiedType();
      continue;
    }

    if (const auto* Decayed = dyn_cast<DecayedType>(TypePtr)) {
      QT = Decayed->getDecayedType();
      continue;
    }

    break;
  }

  return false;
}

const TypedefNameDecl* findTypedefInDeclContext(const DeclContext* Context,
                                                llvm::StringRef Name) {
  for (const Decl* Declaration : Context->decls()) {
    if (const auto* Typedef = dyn_cast<TypedefNameDecl>(Declaration)) {
      if (Typedef->getName() == Name) {
        return Typedef;
      }
    }

    if (const auto* Nested = dyn_cast<DeclContext>(Declaration)) {
      if (const TypedefNameDecl* Found =
              findTypedefInDeclContext(Nested, Name)) {
        return Found;
      }
    }
  }

  return nullptr;
}

bool hasSameCanonicalTypeAsTypedef(ASTContext& Context, QualType Type,
                                   llvm::StringRef TypedefName) {
  const TypedefNameDecl* Typedef =
      findTypedefInDeclContext(Context.getTranslationUnitDecl(), TypedefName);
  if (Typedef == nullptr) {
    return false;
  }

  return Context.hasSameUnqualifiedType(
      Context.getCanonicalType(unqualifiedNonReference(Type)),
      Context.getCanonicalType(Typedef->getUnderlyingType()));
}

bool hasSameBuiltinType(ASTContext& Context, QualType Type, QualType Expected) {
  return Context.hasSameUnqualifiedType(
      Context.getCanonicalType(unqualifiedNonReference(Type)),
      Context.getCanonicalType(Expected));
}

bool isPointerToTypedef(QualType Type, llvm::StringRef TypedefName) {
  Type = unqualifiedNonReference(Type);
  const auto* Pointer = Type->getAs<PointerType>();
  if (Pointer == nullptr) {
    return false;
  }

  return hasTypedefName(Pointer->getPointeeType(), TypedefName);
}

bool isCharacterPointer(QualType Type) {
  Type = unqualifiedNonReference(Type);
  const auto* Pointer = Type->getAs<PointerType>();
  if (Pointer == nullptr) {
    return false;
  }

  QualType Pointee = Pointer->getPointeeType().getUnqualifiedType();
  if (hasTypedefName(Pointee, "u_char")) {
    return true;
  }

  return Pointee->isCharType();
}

bool isVoidOrObjectPointer(QualType Type) {
  Type = unqualifiedNonReference(Type);
  if (Type->isNullPtrType()) {
    return true;
  }

  const auto* Pointer = Type->getAs<PointerType>();
  if (Pointer == nullptr) {
    return false;
  }

  QualType Pointee = Pointer->getPointeeType().getUnqualifiedType();
  return Pointee->isVoidType() || !Pointee->isFunctionType();
}

bool argumentMatches(ExpectedKind Kind, const Expr* Argument,
                     ASTContext& Context) {
  const QualType Type = Argument->getType();

  switch (Kind) {
    case ExpectedKind::SizeT:
      // sizeof() doesn't yield the typedef size_t,
      // so don't match the typedef directly
      return hasSameCanonicalTypeAsTypedef(Context, Type, "size_t");
    case ExpectedKind::SSizeT:
      return hasTypedefName(Type, "ssize_t");
    case ExpectedKind::OffT:
      return hasTypedefName(Type, "off_t");
    case ExpectedKind::TimeT:
      return hasTypedefName(Type, "time_t");
    case ExpectedKind::NgxPidT:
      return hasTypedefName(Type, "ngx_pid_t");
    case ExpectedKind::NgxMsecT:
      return hasTypedefName(Type, "ngx_msec_t") ||
             hasTypedefName(Type, "ngx_rbtree_key_t");
    case ExpectedKind::NgxIntT:
      return hasTypedefName(Type, "ngx_int_t");
    case ExpectedKind::NgxUIntT:
      return hasTypedefName(Type, "ngx_uint_t");
    case ExpectedKind::NgxAtomicIntT:
      return hasTypedefName(Type, "ngx_atomic_int_t");
    case ExpectedKind::NgxAtomicUIntT:
      return hasTypedefName(Type, "ngx_atomic_uint_t");
    case ExpectedKind::Int:
      return hasSameBuiltinType(Context, Type, Context.IntTy);
    case ExpectedKind::UInt:
      return hasSameBuiltinType(Context, Type, Context.UnsignedIntTy);
    case ExpectedKind::Long:
      return hasSameBuiltinType(Context, Type, Context.LongTy);
    case ExpectedKind::ULong:
      return hasSameBuiltinType(Context, Type, Context.UnsignedLongTy);
    case ExpectedKind::Int32:
      return hasSameCanonicalTypeAsTypedef(Context, Type, "int32_t");
    case ExpectedKind::UInt32:
      return hasSameCanonicalTypeAsTypedef(Context, Type, "uint32_t");
    case ExpectedKind::Int64:
      return hasSameCanonicalTypeAsTypedef(Context, Type, "int64_t");
    case ExpectedKind::UInt64:
      return hasSameCanonicalTypeAsTypedef(Context, Type, "uint64_t");
    case ExpectedKind::Double:
      return hasSameBuiltinType(Context, Type, Context.DoubleTy);
    case ExpectedKind::CharInt:
      return hasSameBuiltinType(Context, Type, Context.IntTy);
    case ExpectedKind::CharacterPointer:
      return isCharacterPointer(Type);
    case ExpectedKind::VoidPointer:
      return isVoidOrObjectPointer(Type);
    case ExpectedKind::NgxStrPointer:
      return isPointerToTypedef(Type, "ngx_str_t");
    case ExpectedKind::NgxVariableValuePointer:
      return isPointerToTypedef(Type, "ngx_variable_value_t");
    case ExpectedKind::RLimT:
      return hasSameCanonicalTypeAsTypedef(Context, Type, "rlim_t");
  }

  return false;
}

SourceLocation formatLocation(const StringLiteral* Literal, unsigned Offset,
                              const MatchFinder::MatchResult& Result) {
  unsigned StartToken = 0;
  unsigned StartTokenByteOffset = 0;
  return Literal->getLocationOfByte(
      Offset, *Result.SourceManager, Result.Context->getLangOpts(),
      Result.Context->getTargetInfo(), &StartToken, &StartTokenByteOffset);
}

std::string typeName(QualType Type, ASTContext& Context) {
  PrintingPolicy Policy(Context.getLangOpts());
  Policy.SuppressTagKeyword = true;
  return Type.getAsString(Policy);
}

}  // namespace

void NginxLogFormatCheck::registerMatchers(MatchFinder* Finder) {
  Finder->addMatcher(callExpr(callee(functionDecl(hasAnyName(
                                  "ngx_log_error_core", "ngx_log_error",
                                  "ngx_log_debug_core"))))
                         .bind("call"),
                     this);
}

void NginxLogFormatCheck::check(const MatchFinder::MatchResult& Result) {
  const auto* Call = Result.Nodes.getNodeAs<CallExpr>("call");
  if (Call == nullptr) {
    return;
  }

  const FunctionDecl* Callee = Call->getDirectCallee();
  if (Callee == nullptr) {
    return;
  }

  const llvm::StringRef CalleeName = Callee->getName();
  const unsigned FormatIndex = CalleeName == "ngx_log_debug_core" ? 2 : 3;
  if (Call->getNumArgs() <= FormatIndex) {
    return;
  }

  const auto* Literal =
      dyn_cast<StringLiteral>(Call->getArg(FormatIndex)->IgnoreParenImpCasts());
  if (Literal == nullptr || Literal->getCharByteWidth() != 1) {
    return;
  }

  const FormatParseResult Parsed = parseFormatString(Literal->getString());

  for (const FormatIssue& Issue : Parsed.Issues) {
    diag(formatLocation(Literal, Issue.Offset, Result), "%0") << Issue.Message;
  }

  const unsigned FirstFormatArg = FormatIndex + 1;
  const unsigned ProvidedArgCount = Call->getNumArgs() - FirstFormatArg;
  const unsigned ExpectedArgCount = Parsed.Args.size();

  if (ProvidedArgCount < ExpectedArgCount) {
    diag(Literal->getBeginLoc(),
         "nginx log format string expects %0 argument(s), but %1 provided")
        << ExpectedArgCount << ProvidedArgCount;
    return;
  }

  if (ProvidedArgCount > ExpectedArgCount) {
    diag(Call->getArg(FirstFormatArg + ExpectedArgCount)->getExprLoc(),
         "nginx log call has %0 extra argument(s) not consumed by the format "
         "string")
        << (ProvidedArgCount - ExpectedArgCount);
  }

  for (unsigned I = 0; I < ExpectedArgCount; ++I) {
    const ExpectedArg& Expected = Parsed.Args[I];
    const Expr* Argument = Call->getArg(FirstFormatArg + I);

    if (argumentMatches(Expected.Kind, Argument, *Result.Context)) {
      continue;
    }

    diag(formatLocation(Literal, Expected.Offset, Result),
         "nginx log format specifier '%0' expects argument of type '%1', but "
         "argument has type '%2'")
        << Expected.Specifier << expectedTypeName(Expected.Kind)
        << typeName(Argument->getType(), *Result.Context);
    diag(Argument->getExprLoc(), "argument passed here", DiagnosticIDs::Note);
  }
}

}  // namespace clang::tidy::nginx_datadog
