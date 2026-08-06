#include "NginxLogFormatCheck.h"
#include "NoNumberedNgxLogDebugCheck.h"
#include "clang-tidy/ClangTidyModule.h"
#include "clang-tidy/ClangTidyModuleRegistry.h"

namespace clang::tidy::nginx_datadog {
namespace {

class NginxDatadogModule : public ClangTidyModule {
 public:
  void addCheckFactories(ClangTidyCheckFactories& CheckFactories) override {
    CheckFactories.registerCheck<NginxLogFormatCheck>(
        "nginx-datadog-ngx-log-format");
    CheckFactories.registerCheck<NoNumberedNgxLogDebugCheck>(
        "nginx-datadog-no-numbered-ngx-log-debug");
  }
};

}  // namespace
}  // namespace clang::tidy::nginx_datadog

static clang::tidy::ClangTidyModuleRegistry::Add<
    clang::tidy::nginx_datadog::NginxDatadogModule>
    X("nginx-datadog-module", "Adds nginx-datadog clang-tidy checks.");

volatile int NginxDatadogModuleAnchorSource = 0;
