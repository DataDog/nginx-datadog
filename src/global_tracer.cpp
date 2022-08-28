
#include "global_tracer.h"

#include <datadog/tracer.h>

#include <optional>

namespace datadog {
namespace nginx {
namespace {
std::optional<dd::Tracer> instance;
}  // namespace

dd::Tracer* global_tracer() {
  if (instance) {
    return &*instance;
  }
  return nullptr;
}

void reset_global_tracer() { instance.reset(); }

void reset_global_tracer(dd::Tracer&& tracer) { instance = std::move(tracer); }

}  // namespace nginx
}  // namespace datadog
