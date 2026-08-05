#ifndef NGINX_DATADOG_NO_NUMBERED_NGX_LOG_DEBUG_CHECK_H
#define NGINX_DATADOG_NO_NUMBERED_NGX_LOG_DEBUG_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::nginx_datadog {

class NoNumberedNgxLogDebugCheck : public ClangTidyCheck {
 public:
  NoNumberedNgxLogDebugCheck(StringRef Name, ClangTidyContext* Context)
      : ClangTidyCheck(Name, Context) {}

  void registerPPCallbacks(const SourceManager& SourceManager, Preprocessor* PP,
                           Preprocessor* ModuleExpanderPP) override;
};

}  // namespace clang::tidy::nginx_datadog

#endif  // NGINX_DATADOG_NO_NUMBERED_NGX_LOG_DEBUG_CHECK_H
