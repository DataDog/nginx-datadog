#include "NoNumberedNgxLogDebugCheck.h"

#include <memory>

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/StringRef.h"

namespace clang::tidy::nginx_datadog {
namespace {

bool isNumberedNgxLogDebug(llvm::StringRef Name) {
  constexpr llvm::StringLiteral Prefix = "ngx_log_debug";
  return Name.size() == Prefix.size() + 1 && Name.starts_with(Prefix) &&
         Name.back() >= '0' && Name.back() <= '8';
}

class NoNumberedNgxLogDebugPPCallbacks : public PPCallbacks {
 public:
  explicit NoNumberedNgxLogDebugPPCallbacks(NoNumberedNgxLogDebugCheck& Check)
      : Check(Check) {}

  void MacroExpands(const Token& MacroNameToken, const MacroDefinition&,
                    SourceRange, const MacroArgs*) override {
    const IdentifierInfo* Identifier = MacroNameToken.getIdentifierInfo();
    if (Identifier == nullptr ||
        !isNumberedNgxLogDebug(Identifier->getName())) {
      return;
    }

    Check.diag(MacroNameToken.getLocation(),
               "use variadic macro 'ngx_log_debug' instead of non-variadic "
               "macro '%0'")
        << Identifier->getName()
        << FixItHint::CreateReplacement(
               CharSourceRange::getTokenRange(MacroNameToken.getLocation(),
                                              MacroNameToken.getLocation()),
               "ngx_log_debug");
  }

 private:
  NoNumberedNgxLogDebugCheck& Check;
};

}  // namespace

void NoNumberedNgxLogDebugCheck::registerPPCallbacks(const SourceManager&,
                                                     Preprocessor* PP,
                                                     Preprocessor*) {
  PP->addPPCallbacks(std::make_unique<NoNumberedNgxLogDebugPPCallbacks>(*this));
}

}  // namespace clang::tidy::nginx_datadog
