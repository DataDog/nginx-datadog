#include "propagation_header_querier.h"


#include <algorithm>
#include <cassert>
#include <iterator>
#include <new>

#include "dd.h"
#include "string_util.h"
#include <string_view>

namespace datadog {
namespace nginx {

ngx_str_t PropagationHeaderQuerier::lookup_value(ngx_http_request_t* request, const dd::Span& span,
                                                 std::string_view key) {
  if (&span != values_span_) {
    expand_values(request, span);
  }

  for (auto& key_value : span_context_expansion_) {
    if (key_value.first == key) {
      return to_ngx_str(key_value.second);
    }
  }

  // This will prevent the header from being added to the proxied request.
  return ngx_str_t();
}

namespace {

class SpanContextValueWriter : public dd::DictWriter {
  std::vector<std::pair<std::string, std::string>>* span_context_expansion_;
  std::string* buffer_;

 public:
  explicit SpanContextValueExpander(
      std::unordered_map<std::string, std::string>& span_context_expansion, std::string& buffer)
      : span_context_expansion_(&span_context_expansion), buffer_(&buffer) {}

  void set(std::string_view key, std::string_view value) override {
    // TODO
    buffer->clear()
    std::transform(key.begin(), key.end(), std::back_inserter(*buffer_),
                   header_transform_char);
    span_context_expansion.insert_or_assign(*buffer, value);
  }
};

}  // namespace

void PropagationHeaderQuerier::expand_values(ngx_http_request_t* request, const dd::Span& span) {
  values_span_ = &span;
  span_context_expansion_.clear();
  SpanContextValueWriter writer{span_context_expansion_, buffer_};
  span.inject(writer);
}

}  // namespace nginx
}  // namespace datadog
