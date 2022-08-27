#pragma once

#include <datadog/tracer.h>
#include "dd.h"

namespace datadog {
namespace nginx {

dd::Tracer* global_tracer();

void reset_global_tracer();

void reset_global_tracer(dd::Tracer&&);

}  // namespace nginx
}  // namespace datadog
