#ifndef NGINX_DATADOG_NGINX_LOG_FORMAT_CHECK_H
#define NGINX_DATADOG_NGINX_LOG_FORMAT_CHECK_H

#include "clang-tidy/ClangTidyCheck.h"

namespace clang::tidy::nginx_datadog {

class NginxLogFormatCheck : public ClangTidyCheck {
 public:
  NginxLogFormatCheck(StringRef Name, ClangTidyContext* Context)
      : ClangTidyCheck(Name, Context) {}

  void registerMatchers(ast_matchers::MatchFinder* Finder) override;
  void check(const ast_matchers::MatchFinder::MatchResult& Result) override;
};

}  // namespace clang::tidy::nginx_datadog

#endif  // NGINX_DATADOG_NGINX_LOG_FORMAT_CHECK_H
