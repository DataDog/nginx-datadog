#include "blocking.h"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string_view>

#include "util.h"

extern "C" {
#include <ngx_http.h>
}

// NOLINTBEGIN(cppcoreguidelines-pro-type-cstyle-cast)

using namespace std::literals;

namespace {

namespace dnsec = datadog::nginx::security;

struct BlockResponse {
  enum class ContentType {
    HTML,
    JSON,
    NONE,
  };

  int status;
  ContentType ct;
  std::string_view location;

  BlockResponse(int status, enum ContentType ct,
                std::string_view location) noexcept
      : status{status}, ct{ct}, location{location} {}

  static BlockResponse calculate_for(const dnsec::BlockSpecification &spec,
                                     const ngx_http_request_t &req) noexcept {
    int status;
    enum ContentType ct;

    status = spec.status;

    switch (spec.ct) {
      case dnsec::BlockSpecification::ContentType::AUTO:
        ct = determine_ct(req);
        break;
      case dnsec::BlockSpecification::ContentType::HTML:
        ct = ContentType::HTML;
        break;
      case dnsec::BlockSpecification::ContentType::JSON:
        ct = ContentType::JSON;
        break;
      case dnsec::BlockSpecification::ContentType::NONE:
        ct = ContentType::NONE;
        break;
    }

    return {status, ct, spec.location};
  }

  static ngx_str_t content_type_header(enum BlockResponse::ContentType ct) {
    switch (ct) {
      case ContentType::HTML:
        return ngx_string("text/html;charset=utf-8");
      case ContentType::JSON:
        return ngx_string("application/json");
      default:
        return ngx_string("");
    }
  }

  struct AcceptEntry {
    std::string_view type;
    std::string_view subtype;
    double qvalue{};

    enum class Specificity {
      NONE,
      ASTERISK,  // */*
      PARTIAL,   // type/*
      FULL       // type/subtype
    };

    static bool first_is_more_specific(Specificity a, Specificity b) {
      // NOLINTNEXTLINE(readability-identifier-naming)
      using underlying_t = std::underlying_type_t<Specificity>;
      return static_cast<underlying_t>(a) > static_cast<underlying_t>(b);
    }
  };

  struct AcceptEntryIter {
    ngx_str_t header;
    std::size_t pos;
    std::size_t pos_end{};

    explicit AcceptEntryIter(const ngx_str_t &header, std::size_t pos = 0)
        : header{header}, pos{pos} {
      find_end();
    }

    static AcceptEntryIter end() {
      return AcceptEntryIter{ngx_str_t{0, nullptr}, 0};
    }

    bool operator!=(const AcceptEntryIter &other) const {
      return pos != other.pos || header.data != other.header.data ||
             header.len != other.header.len;
    }

    AcceptEntryIter &operator++() noexcept {
      if (pos_end == header.len) {
        *this = end();
        return *this;
      }

      pos = pos_end + 1;
      find_end();
      return *this;
    }

    AcceptEntry operator*() noexcept {
      AcceptEntry entry;
      entry.qvalue = 1.0;

      auto sv{part_sv()};
      auto slash_pos = sv.find('/');
      if (slash_pos == std::string_view::npos) {
        return entry;
      }
      entry.type = trim(sv.substr(0, slash_pos));

      sv = sv.substr(slash_pos + 1);
      auto semicolon_pos = sv.find(';');
      if (semicolon_pos == std::string_view::npos) {
        entry.subtype = trim(sv);
        return entry;
      }

      entry.subtype = trim(sv.substr(0, semicolon_pos));
      sv = sv.substr(semicolon_pos + 1);
      auto q_pos = sv.find("q=");
      if (q_pos != std::string_view::npos &&
          (q_pos == 0 || sv.at(q_pos - 1) == ' ')) {
        sv = sv.substr(q_pos + 2);
        char *end;
        entry.qvalue = std::strtod(sv.data(), &end);
        if (end == sv.data() || entry.qvalue == HUGE_VAL || entry.qvalue <= 0 ||
            entry.qvalue > 1) {
          entry.qvalue = 1.0;
        }
      }

      return entry;
    }

   private:
    void find_end() {
      std::string_view const sv{rest_sv()};
      auto colon_pos = sv.find(',');
      if (colon_pos == std::string_view::npos) {
        pos_end = header.len;
      } else {
        pos_end = pos + colon_pos;
      }
    }

    std::string_view rest_sv() const {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      return std::string_view{reinterpret_cast<char *>(header.data + pos),
                              header.len - pos};
    }

    std::string_view part_sv() const {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
      return std::string_view{reinterpret_cast<char *>(header.data + pos),
                              pos_end - pos};
    }

    static std::string_view trim(std::string_view sv) {
      while (std::isspace(sv.front())) {
        sv.remove_prefix(1);
      }
      while (std::isspace(sv.back())) {
        sv.remove_suffix(1);
      }
      return sv;
    }
  };

  static enum ContentType determine_ct(const ngx_http_request_t &req) {
    if (req.headers_in.accept == nullptr) {
      return ContentType::JSON;
    }

    AcceptEntryIter it{req.headers_in.accept->value};

    using Specif = AcceptEntry::Specificity;
    Specif json_spec{};
    Specif html_spec{};
    double json_qvalue = 0.0;
    size_t json_pos = 0;
    double html_qvalue = 0.0;
    size_t html_pos = 0;

    for (size_t pos = 0; it != AcceptEntryIter::end(); ++it, ++pos) {
      AcceptEntry const ae = *it;

      if (ae.type == "*" && ae.subtype == "*") {
        if (AcceptEntry::first_is_more_specific(Specif::ASTERISK, json_spec)) {
          json_spec = Specif::ASTERISK;
          json_qvalue = ae.qvalue;
          json_pos = pos;
        }
        if (AcceptEntry::first_is_more_specific(Specif::ASTERISK, html_spec)) {
          html_spec = Specif::ASTERISK;
          html_qvalue = ae.qvalue;
          html_pos = pos;
        }
      } else if (ae.type == "text" && ae.subtype == "*") {
        if (AcceptEntry::first_is_more_specific(Specif::PARTIAL, html_spec)) {
          html_spec = Specif::PARTIAL;
          html_qvalue = ae.qvalue;
          html_pos = pos;
        }
      } else if (ae.type == "text" && ae.subtype == "html") {
        if (AcceptEntry::first_is_more_specific(Specif::FULL, html_spec)) {
          html_spec = Specif::FULL;
          html_qvalue = ae.qvalue;
          html_pos = pos;
        }
      } else if (ae.type == "application" && ae.subtype == "*") {
        if (AcceptEntry::first_is_more_specific(Specif::PARTIAL, json_spec)) {
          json_spec = Specif::PARTIAL;
          json_qvalue = ae.qvalue;
          json_pos = pos;
        }
      } else if (ae.type == "application" && ae.subtype == "json") {
        if (AcceptEntry::first_is_more_specific(Specif::FULL, json_spec)) {
          json_spec = Specif::FULL;
          json_qvalue = ae.qvalue;
          json_pos = pos;
        }
      }
    }

    if (html_qvalue > json_qvalue) {
      return ContentType::HTML;
    }
    if (json_qvalue > html_qvalue) {
      return ContentType::JSON;
    }  // equal: what comes first has priority
    if (html_pos < json_pos) {
      return ContentType::HTML;
    }
    return ContentType::JSON;
  }
};

const std::string_view kDefaultTemplateHtml{
    "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"UTF-8\"><meta "
    "name=\"viewport\" "
    "content=\"width=device-width,initial-scale=1\"><title>You've been "
    "blocked</"
    "title><style>a,body,div,html,span{margin:0;padding:0;border:0;font-size:"
    "100%;font:inherit;vertical-align:baseline}body{background:-webkit-radial-"
    "gradient(26% 19%,circle,#fff,#f4f7f9);background:radial-gradient(circle "
    "at 26% "
    "19%,#fff,#f4f7f9);display:-webkit-box;display:-ms-flexbox;display:flex;-"
    "webkit-box-pack:center;-ms-flex-pack:center;justify-content:center;-"
    "webkit-box-align:center;-ms-flex-align:center;align-items:center;-ms-flex-"
    "line-pack:center;align-content:center;width:100%;min-height:100vh;line-"
    "height:1;flex-direction:column}p{display:block}main{text-align:center;"
    "flex:1;display:-webkit-box;display:-ms-flexbox;display:flex;-webkit-box-"
    "pack:center;-ms-flex-pack:center;justify-content:center;-webkit-box-align:"
    "center;-ms-flex-align:center;align-items:center;-ms-flex-line-pack:center;"
    "align-content:center;flex-direction:column}p{font-size:18px;line-height:"
    "normal;color:#646464;font-family:sans-serif;font-weight:400}a{color:#"
    "4842b7}footer{width:100%;text-align:center}footer "
    "p{font-size:16px}</style></head><body><main><p>Sorry, you cannot access "
    "this page. Please contact the customer service "
    "team.</p></main><footer><p>Security provided by <a "
    "href=\"https://www.datadoghq.com/product/security-platform/"
    "application-security-monitoring/\" "
    "target=\"_blank\">Datadog</a></p></footer></body></html>"sv};

const std::string_view kDefaultTemplateJson{
    "{\"errors\": [{\"title\": \"You've been blocked\", \"detail\": \"Sorry, "
    "you cannot access this page. Please contact the customer service team. "
    "Security provided by Datadog.\"}]}"sv};
}  // namespace

namespace datadog::nginx::security {

// NOLINTNEXTLINE
std::unique_ptr<BlockingService> BlockingService::instance;

void BlockingService::initialize(std::optional<std::string_view> templ_html,
                                 std::optional<std::string_view> templ_json) {
  if (instance) {
    throw std::runtime_error("Blocking service already initialized");
  }
  instance = std::unique_ptr<BlockingService>(
      new BlockingService(templ_html, templ_json));
}

void BlockingService::block(BlockSpecification spec, ngx_http_request_t &req) {
  BlockResponse const resp = BlockResponse::calculate_for(spec, req);
  ngx_str_t *templ{};
  if (resp.ct == BlockResponse::ContentType::HTML) {
    templ = &templ_html_;
  } else if (resp.ct == BlockResponse::ContentType::JSON) {
    templ = &templ_json_;
  } else {
    req.header_only = 1;
  }

  ngx_http_discard_request_body(&req);

  // TODO: clear all current headers?

  req.headers_out.status = resp.status;
  req.headers_out.content_type = BlockResponse::content_type_header(resp.ct);
  req.headers_out.content_type_len = req.headers_out.content_type.len;

  if (!resp.location.empty()) {
    push_header(req, "Location"sv, resp.location);
  }
  if (templ) {
    req.headers_out.content_length_n = static_cast<off_t>(templ->len);
  } else {
    req.headers_out.content_length_n = 0;
  }

  // TODO: bypass header filters?
  auto res = ngx_http_send_header(&req);
  if (res == NGX_ERROR || res > NGX_OK || req.header_only) {
    ngx_http_finalize_request(&req, res);
    return;
  }

  ngx_buf_t *b = static_cast<decltype(b)>(ngx_calloc_buf(req.pool));
  if (b == nullptr) {
    ngx_http_finalize_request(&req, NGX_ERROR);
    return;
  }

  b->pos = templ->data;
  b->last = templ->data + templ->len;
  b->last_buf = 1;
  b->memory = 1;

  ngx_chain_t out{};
  out.buf = b;

  // TODO: bypass and call ngx_http_write_filter?
  ngx_http_output_filter(&req, &out);
  ngx_http_finalize_request(&req, NGX_DONE);
}

BlockingService::BlockingService(
    std::optional<std::string_view> templ_html_path,
    std::optional<std::string_view> templ_json_path) {
  if (!templ_html_path) {
    templ_html_ = ngx_stringv(kDefaultTemplateHtml);
  } else {
    custom_templ_html_ = load_template(*templ_html_path);
    templ_html_ = ngx_stringv(custom_templ_html_);
  }

  if (!templ_json_path) {
    templ_json_ = ngx_stringv(kDefaultTemplateJson);
  } else {
    custom_templ_json_ = load_template(*templ_json_path);
    templ_json_ = ngx_stringv(custom_templ_json_);
  }
}

std::string BlockingService::load_template(std::string_view path) {
  std::ifstream const file_stream(std::string{path}, std::ios::binary);
  if (!file_stream) {
    std::string err{"Failed to open file: "};
    err += path;
    throw std::runtime_error(err);
  }

  std::ostringstream s;
  s << file_stream.rdbuf();
  return s.str();
}

void BlockingService::push_header(ngx_http_request_t &req,
                                  std::string_view name,  // NOLINT
                                  std::string_view value) {
  ngx_table_elt_t *header =
      static_cast<ngx_table_elt_t *>(ngx_list_push(&req.headers_out.headers));
  if (!header) {
    return;
  }
  header->hash = 1;
  header->key = ngx_stringv(name);
  header->value = ngx_stringv(value);
}

}  // namespace datadog::nginx::security

// NOLINTEND(cppcoreguidelines-pro-type-cstyle-cast)
