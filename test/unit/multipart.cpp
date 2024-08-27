#include <security/body_parse/body_multipart.h>
#include <security/body_parse/header.h>
#include <security/ddwaf_obj.h>

#include <catch2/catch_test_macros.hpp>
#include <stdexcept>
#include <string_view>

#include "managed_chain.h"

extern "C" {
#include <ngx_buf.h>
#include <ngx_core.h>
}

namespace dnsec = datadog::nginx::security;
using dnsec::ddwaf_obj;

namespace {
std::optional<ddwaf_obj> parse(std::string_view content_type,
                               std::vector<std::string_view> parts,
                               dnsec::DdwafMemres &memres) {
  static ngx_log_t log{};
  static ngx_connection_t empty_conn{.log = &log};
  ddwaf_obj slot;
  ngx_http_request_t req{.connection = &empty_conn};
  auto ct = dnsec::HttpContentType::for_string(content_type);
  if (!ct) {
    throw std::runtime_error{std::string{"invalid content-type: "} +
                             std::string{content_type}};
  }
  test::ManagedChain chain{parts};
  bool success = parse_multipart(slot, req, *ct, chain, memres);

  if (!success) {
    return std::nullopt;
  }

  return {slot};
}
}  // namespace

TEST_CASE("content-type header parsing: valid cases", "[multipart]") {
  SECTION("canonical example") {
    std::string_view header = "multipart/form-data; boundary=myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    REQUIRE(ct);
    CHECK(ct->type == "multipart");
    CHECK(ct->subtype == "form-data");
    CHECK(ct->boundary == "myboundary");
  }

  SECTION("mixed casing") {
    std::string_view header = "MuLtIpArT/FoRm-DaTa; BoUnDaRy=myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    REQUIRE(ct);
    CHECK(ct->type == "multipart");
    CHECK(ct->subtype == "form-data");
    CHECK(ct->boundary == "myboundary");
  }

  SECTION("boundary is quoted") {
    std::string_view header =
        "multipart/form-data; boundary=\";mybound\\ary\\ \\\t\"";
    auto ct = dnsec::HttpContentType::for_string(header);
    REQUIRE(ct);
    CHECK(ct->boundary == ";myboundary \t");
  }

  SECTION("spacing variant around semicolon") {
    std::string_view header =
        "multipart/form-data;boundary=myboundary  ; charset=iso-8859-1; ";
    auto ct = dnsec::HttpContentType::for_string(header);
    REQUIRE(ct);
    CHECK(ct->boundary == "myboundary");
    CHECK(ct->encoding == "iso-8859-1");
  }

  SECTION("dupped semicolon") {
    std::string_view header = "multipart/form-data; ;; boundary=myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    REQUIRE(ct);
    CHECK(ct->boundary == "myboundary");
  }
}

TEST_CASE("Content-type: rejected values", "[multipart]") {
  SECTION("empty value") {
    std::string_view header = "";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("no type") {
    std::string_view header = "/form-data; boundary=myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("no subtype") {
    std::string_view header = "multipart/; boundary=myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("missing semicolon") {
    std::string_view header = "multipart/form-data boundary=myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("param name followed by EOF") {
    std::string_view header = "multipart/form-data; boundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("param name not followed by =") {
    std::string_view header = "multipart/form-data; boundary myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("EOF in place of param value") {
    std::string_view header = "multipart/form-data; boundary=";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("non-token in place of param value") {
    std::string_view header = "multipart/form-data; boundary=;";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("invalid quoted character") {
    std::string_view header =
        "multipart/form-data; boundary=\"my\\\x7F boundary\"";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("unterminated quoted string") {
    std::string_view header = "multipart/form-data; boundary=\"myboundary";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }

  SECTION("unterminated escape sequence") {
    std::string_view header = "multipart/form-data; boundary=\"my\\";
    auto ct = dnsec::HttpContentType::for_string(header);
    CHECK_FALSE(ct);
  }
}

TEST_CASE("valid multipart examples", "[multipart]") {
  SECTION("canonical example") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field2\"\r\n"
               "\r\n"
               "LONG VALUE LONG\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    CHECK(map.size() == 2);
    std::optional<ddwaf_obj> v1 = map.get_opt("field1");
    REQUIRE(v1);
    CHECK((v1->is_string() && v1->string_val_unchecked() == "value1"));

    std::optional<ddwaf_obj> v2 = map.get_opt("field2");
    REQUIRE(v2);
    CHECK((v2->is_string() && v2->string_val_unchecked() == "LONG VALUE LONG"));
  }

  SECTION("using just LF instead of CRLF") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\n"
               "Content-Disposition: form-data; name=\"field1\"\n"
               "\n"
               "value1\n"
               "--myboundary--\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    std::optional<ddwaf_obj> v1 = map.get_opt("field1");
    REQUIRE(v1);
    CHECK(v1->is_string());
    CHECK(v1->string_val_unchecked() == "value1");
  }

  SECTION("field name is repeated") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value2\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    std::optional<ddwaf_obj> v1 = map.get_opt("field1");
    REQUIRE(v1);
    REQUIRE(v1->is_array());

    auto arr_v1 = dnsec::ddwaf_arr_obj{*v1};
    CHECK(arr_v1.size() == 2);
    CHECK((arr_v1.at_unchecked(0).is_string() &&
           arr_v1.at_unchecked(0).string_val_unchecked() == "value1"));
    CHECK((arr_v1.at_unchecked(1).is_string() &&
           arr_v1.at_unchecked(1).string_val_unchecked() == "value2"));
  }

  SECTION("unquoted field name") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=field1\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE((*map.begin()).key() == "field1");
  }

  SECTION("field name has percent encoded data and octets >= 0x80") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"fiéld%201\"\r\n"
               "\r\n"
               "value\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE(map.get_opt("fiéld 1"));
  }

  SECTION("folding of content-disposition") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data;\r\n"
               " name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("folding of content-disposition stradles quoted field name") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data;name=\"field\r\n"
               "   1\";\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE((*map.begin()).key() == "field1");
  }

  SECTION("folding of content-disposition strades unquoted field name") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data;name=field\r\n"
               "   1;\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE((*map.begin()).key() == "field1");
  }

  SECTION("mixed case for content-disposition and name") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "cOnTeNt-dIsPoSiTiOn: fOrM-dAtA; nAmE=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("content-disposition also contains a filename after name") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj = parse(
        "multipart/form-data; boundary=myboundary",
        {"--myboundary\r\n"
         "Content-Disposition: form-data; name=\"field1\"; filename=\"f1\"\r\n"
         "\r\n"
         "value1\r\n"
         "--myboundary--\r\n"},
        memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("content-disposition also contains a filename before name") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; filename=\"f1;\";\r\n"
               " name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("content-disposition with a parameter with no value") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("Additional MIME header before") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Type: text/plain\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("Additional MIME header after") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "Content-Type: text/plain\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("Whitespace indented garbage before first header") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "    GARBAGE\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE(obj->is_map());
    auto map = dnsec::ddwaf_map_obj{*obj};
    REQUIRE(map.size() == 1);
    REQUIRE(map.get_opt("field1"));
  }

  SECTION("Garbage before the first part and after the last part") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"GARBAGE\r\n", "LONG GARBAGE LONG GARBAGE\r\n",
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n",
               "GARBAGE"},
              memres);

    REQUIRE(obj);
    CHECK((obj->is_map() && obj->size_unchecked() == 1));
  }

  SECTION("Garbage after the boundaries is allowed") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundaryGARBAGE\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--GARBAGE\r\n"},
              memres);

    REQUIRE(obj);
    CHECK((obj->is_map() && obj->size_unchecked() == 1));
  }
}

TEST_CASE("truncated multipart examples", "[multipart]") {
  auto single_value = [](auto obj) {
    REQUIRE(obj);
    REQUIRE((obj && obj->is_map() && obj->size_unchecked() == 1));
    auto value = *dnsec::ddwaf_map_obj{*obj}.begin();
    REQUIRE(value.is_string());
    return value.string_val_unchecked();
  };

  SECTION("Ends before boundary without CRLF") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("Ends before boundary with CRLF") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("Ends before boundary with CR") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("Ends before boundary with LF") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\n"
               "Content-Disposition: form-data; name=\"field1\"\n"
               "\n"
               "value1\n"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("Ends with partial boundary (1)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "-"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("Ends with partial boundary (2)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundar"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("EOF after a boundary") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("EOF after a partial header (1)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposi"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("EOF after a partial header (2)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition:"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("EOF after a partial header (4)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name="},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("EOF after a partial header (5)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"fie"},
              memres);

    CHECK(single_value(obj) == "value1");
  }

  SECTION("EOF after a partial header (6)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field\"\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE((obj->is_map() && obj->size_unchecked() == 2));
    auto second = dnsec::ddwaf_map_obj{*obj}.get_opt("field");
    REQUIRE(second);
    CHECK(second->is_string());
    CHECK(second->string_val_unchecked() == "");
  }

  SECTION("EOF after a partial header (7)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field\"\r"},
              memres);

    REQUIRE(obj);
    REQUIRE((obj->is_map() && obj->size_unchecked() == 2));
    auto second = dnsec::ddwaf_map_obj{*obj}.get_opt("field");
    REQUIRE(second);
    CHECK(second->is_string());
    CHECK(second->string_val_unchecked() == "");
  }

  SECTION("EOF after a partial header (8)") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field\"\r\n"
               " continuation"},
              memres);

    REQUIRE(obj);
    REQUIRE((obj->is_map() && obj->size_unchecked() == 2));
    auto second = dnsec::ddwaf_map_obj{*obj}.get_opt("field");
    REQUIRE(second);
    CHECK(second->is_string());
    CHECK(second->string_val_unchecked() == "");
  }

  SECTION("EOF after a full header") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n",
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=field2\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE((obj->is_map() && obj->size_unchecked() == 2));
    auto second = dnsec::ddwaf_map_obj{*obj}.get_opt("field2");
    REQUIRE(second);
    CHECK(second->is_string());
    CHECK(second->string_val_unchecked() == "");
  }
}

TEST_CASE("invalid multipart examples", "[multipart]") {
  SECTION("header has CR not followed by LF") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-type: text/plain\r"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary--\r\n"},
              memres);

    CHECK_FALSE(obj);
  }

  SECTION("part has no headers") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value2\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE((obj->is_map() && obj->size_unchecked() == 1));
    auto value = *dnsec::ddwaf_map_obj{*obj}.begin();
    CHECK(value.key() == "field1");
    REQUIRE(value.is_string());
    CHECK((value.string_val_unchecked() == "value2"));
  }

  SECTION("part has no Content-Disposition header") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "Content-type: text/plain\r\n"
               "\r\n"
               "value1\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value2\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE((obj->is_map() && obj->size_unchecked() == 1));
    auto value = *dnsec::ddwaf_map_obj{*obj}.begin();
    CHECK(value.key() == "field1");
    REQUIRE(value.is_string());
    CHECK((value.string_val_unchecked() == "value2"));
  }

  SECTION("two boundaries in immediate succession") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {"--myboundary\r\n"
               "--myboundary\r\n"
               "Content-Disposition: form-data; name=\"field1\"\r\n"
               "\r\n"
               "value2\r\n"
               "--myboundary--\r\n"},
              memres);

    REQUIRE(obj);
    REQUIRE((obj && obj->is_map() && obj->size_unchecked() == 1));
    auto value = *dnsec::ddwaf_map_obj{*obj}.begin();
    CHECK(value.key() == "field1");
    REQUIRE(value.is_string());
    CHECK((value.string_val_unchecked() == "value2"));
  }

  SECTION("no CRLF after headers") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj = parse(
        "multipart/form-data; boundary=myboundary",
        {"--myboundary\r\n"
         "Content-Disposition: form-data; name=\"field1\"\r\n"
         "value1\r\n"        // recognized as header
         "--myboundary\r\n"  // recognized as header
         "Content-Disposition: form-data; name=\"field2\"\r\n"  // overrides
                                                                // earlier value
         "\r\n"
         "value2\r\n"
         "--myboundary--\r\n"},
        memres);

    REQUIRE(obj);
    REQUIRE((obj && obj->is_map() && obj->size_unchecked() == 1));
    auto value = *dnsec::ddwaf_map_obj{*obj}.begin();
    CHECK(value.key() == "field2");
    REQUIRE(value.is_string());
    CHECK((value.string_val_unchecked() == "value2"));
  }

  SECTION("eof after first boundary") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary", {"--myboundary\r\n"},
              memres);

    CHECK_FALSE(obj);
  }

  SECTION("end boundary in place of first boundary") {
    dnsec::DdwafMemres memres;
    std::optional<ddwaf_obj> obj =
        parse("multipart/form-data; boundary=myboundary",
              {
                  "--myboundary--\r\n"
                  "Content-Disposition: form-data; name=\"field1\"\r\n"
                  "\r\n"
                  "value1\r\n"
                  "--myboundary--\r\n",
              },
              memres);
  }
}
