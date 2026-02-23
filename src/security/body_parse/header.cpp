#include "header.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>

#include "../decode.h"
#include "chain_is.h"
#include "generator.h"

using namespace std::literals;

namespace dnsec = datadog::nginx::security;

namespace {

inline bool equals_ci(std::string_view a, std::string_view lc_b) {
  if (a.size() != lc_b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); i++) {
    if (std::tolower(a[i]) != lc_b[i]) {
      return false;
    }
  }
  return true;
}

inline std::string to_lc(std::string_view sv) {
  std::string result;
  result.reserve(sv.size());
  std::transform(sv.begin(), sv.end(), std::back_inserter(result),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

void consume_ows(std::string_view &sv) {
  while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t')) {
    sv.remove_prefix(1);
  }
}

/*
 * Consumes a token according to RFC 9110, section 5.6.2.
 *
 * https://httpwg.org/specs/rfc9110.html#rfc.section.5.6.2
 *   token          = 1*tchar
 *   tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
 *                  / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
 *                  / DIGIT / ALPHA
 *                  ; any VCHAR, except delimiters
 *
 * For multipart/form-data, RFC 9110 refers to RFC 7578 ("Returning Values from
 * Forms: multipart/form-data"), which in refers to RFC 2183 ("The
 * Content-Disposition Header Field"), which in turn defines their tokens like
 * this:
 *
 * https://datatracker.ietf.org/doc/html/rfc2045 (by reference to RFC 822)
 *   token      := 1*<any (US-ASCII) CHAR except SPACE, CTLs,
 *                    or tspecials>
 *
 *   tspecials :=  "(" / ")" / "<" / ">" / "@" /
 *                 "," / ";" / ":" / "\" / <">
 *                 "/" / "[" / "]" / "?" / "="
 *                 ; Must be in quoted-string,
 *                 ; to use within parameter values
 *
 * This is both more permissive (allows {}) and more restrictive (forbids
 * characters outside ASCII).
 */
std::optional<std::string_view> consume_wg_token(std::string_view &sv) {
  static constexpr std::string_view tchar =
      "abcdefghijklmnopqrstuvwxyz"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "0123456789!#$%&'*+-.^_`|~";
  auto end = sv.find_first_not_of(tchar);
  if (end == 0) {
    return std::nullopt;
  }
  if (end == std::string_view::npos) {
    end = sv.size();
  }
  auto ret = std::optional{sv.substr(0, end)};
  sv.remove_prefix(end);
  return ret;
}

/*
 * https://httpwg.org/specs/rfc9110.html#quoted.strings
 * quoted-string  = DQUOTE *( qdtext / quoted-pair ) DQUOTE
 * qdtext         = HTAB / SP / %x21 / %x23-5B / %x5D-7E / obs-text
 * obs-text       = %x80-FF
 * quoted-pair    = "\" ( HTAB / SP / VCHAR / obs-text )
 */
std::optional<std::string> consume_9110_quoted_string(std::string_view &sv) {
  if (!sv.starts_with('"')) {
    return std::nullopt;
  }
  sv.remove_prefix(1);

  std::string result;
  while (!sv.empty()) {
    unsigned char ch = sv.front();
    sv.remove_prefix(1);
    if (ch == '"' /* 0x22 */) {
      return result;
    }

    // qdtext
    if (ch == '\t' || ch == ' ' || (ch >= 0x21 && ch != '\\' && ch != 0x7F)) {
      result.push_back(ch);
      continue;
    }

    // quoted-pair
    if (ch == '\\') {
      if (sv.empty()) {
        return std::nullopt;
      }
      ch = sv.front();
      sv.remove_prefix(1);
      if (ch == '\t' || ch == ' ' || (ch >= 0x21 && ch != 0x7F)) {
        result.push_back(ch);
        continue;
      }
    }

    return std::nullopt; /* invalid character */
  }
  return std::nullopt;
}

// an extended understanding of whitespace. Spec would allow only ' ' and '\t'
inline bool is_ext_ws(unsigned char ch) {  // not include \r or \n
  return ch == ' ' || ch == '\t' || ch == '\v' || ch == '\f';
}
/*
 * Line folding, this is described in RFC 5322:
 * FWS             =   ([*WSP CRLF] 1*WSP) / obs-FWS
 * obs-FWS         =   1*WSP *(CRLF 1*WSP)
 * WSP             =   SP / HTAB
 *
 * We deviate in the following ways, allowing for certain invalid input
 * accepted by PHP:
 * - allow line terminations with only \n (no \r)
 * - consider \v and \f as whitespace
 * - ignore invalid first lines starting with white spaces
 *
 * This coroutine returns characters for a single header "line", unfolded.
 */
dnsec::Generator<std::uint8_t> unfold_next_header(
    dnsec::NgxChainInputStream &is) {
initial_line:
  if (is.eof()) {
    co_return;
  }

  auto ch = is.read();
  if (is_ext_ws(ch)) {
    // starts with space, but can't be a continuation. Ignore the whole line,
    // like PHP does. Note that we're not discarding possibly valid payload,
    // because the Content-disposition header is mandatory. In fact, even if
    // there were no headers, the sequence should be --<boundary>\r\n\r\n<data>
    while (!is.eof() && (ch = is.read()) != '\n') {
    }
    goto initial_line;
  } else if (ch == '\r') {
    if (is.eof()) {
      // unexpected end of input: \r nor followed by \n
      co_return;
    }

    ch = is.read();
    if (ch == '\n') {
      // end of the headers
      co_return;
    }
  } else if (ch == '\n') {
    // allow \n without \r
    co_return;
  }

  while (true) {
    if (ch == '\r') {
      if (is.eof()) {
        // unexpected end of input: \r nor followed by \n
        co_return;
      }

      if (ch == '\n') {
      crlf:
        // found \r\n
        if (is.eof()) {
          co_return;
        }

        ch = *is;  // peek; do not consume has it may be part of the next header
                   // or the end of the headers
        if (is_ext_ws(ch)) {
          // We're folding
          // skip the current ws and then the rest of the ws
          do {
            is.read();
          } while (!is.eof() && is_ext_ws(*is) /* peek */);

          // at this point either eof, or we do ch = is.read() (returns non-ws)
          // and restart the loop
        } else {
          // if CRLF is not followed by whitespace, then it's a new line and
          // we're done. Do not consume the current char.
          // This is the only normal finish, although we need to tolerate at the
          // very least early eof due to limited buffering of the request body
          co_return;
        }
      }
    } else if (ch == '\n') {
      // violation: allow \n without \r
      goto crlf;
    } else {
      co_yield ch;
    }

    if (is.eof()) {
      break;
    }
    ch = is.read();
  }

  // abnormal finish
  co_return;
}

}  // namespace

namespace datadog::nginx::security {

/*
 * https://httpwg.org/specs/rfc9110.html#field.content-type
 * Content-Type    = media-type
 * media-type      = type "/" subtype parameters
 * type            = token
 * subtype         = token
 * parameters      = *( OWS ";" OWS [ parameter ] )
 * parameter       = parameter-name "=" parameter-value
 * parameter-name  = token
 * parameter-value = ( token / quoted-string )
 *
 * This definition is taken from the HTTP spec, but we use it for multipart
 * MIME parts too.
 *
 * Implementation details (glimpsed from code, not verified):
 * - PHP: boundary[^=]*=("[^"]+"|[^,;]+)
 *   case insensitive. boundary max size is 5116
 */
std::optional<HttpContentType> HttpContentType::for_string(
    std::string_view sv) {
  HttpContentType ct{};

  consume_ows(sv);

  auto maybe_type = consume_wg_token(sv);
  if (!maybe_type) {
    return std::nullopt;
  }
  ct.type = to_lc(*maybe_type);

  if (sv.empty() || sv.front() != '/') {
    return std::nullopt;
  }
  sv.remove_prefix(1);

  auto maybe_subtype = consume_wg_token(sv);
  if (!maybe_subtype) {
    return std::nullopt;
  }
  ct.subtype = to_lc(*maybe_subtype);

  while (true) {
    consume_ows(sv);
    if (sv.empty()) {
      return ct;
    }
    if (sv.front() != ';') {
      return std::nullopt;
    }
    sv.remove_prefix(1);
    consume_ows(sv);

    if (sv.empty()) {
      return ct;
    }

    std::optional<std::string_view> maybe_param_name = consume_wg_token(sv);
    if (!maybe_param_name) {
      continue;
    }

    if (sv.size() < 2) {
      return std::nullopt;
    }
    if (sv.front() != '=') {
      return std::nullopt;
    }
    sv.remove_prefix(1);

    std::string value;
    if (sv.front() == '"') {
      std::optional<std::string> maybe_value = consume_9110_quoted_string(sv);
      if (!maybe_value) {
        return std::nullopt;
      }
      value = *maybe_value;
    } else {
      auto maybe_value = consume_wg_token(sv);
      if (!maybe_value) {
        return std::nullopt;
      }
      value = *maybe_value;
    }

    if (equals_ci(*maybe_param_name, "charset"sv)) {
      ct.encoding = value;
    } else if (equals_ci(*maybe_param_name, "boundary"sv)) {
      ct.boundary = value;
    }
  }

  return ct;
}

/*
 * https://www.ietf.org/rfc/rfc2183.txt
 *   disposition        := "Content-Disposition" ":"
 *                         disposition-type
 *                         *(";" disposition-parm)
 *
 *   disposition-type    := "inline"
 *                         / "attachment"
 *                         / extension-token
 *                         ; values are not case-sensitive
 *
 *   disposition-parm    := filename-parm
 *                         / creation-date-parm
 *                         / modification-date-parm
 *                         / read-date-parm
 *                         / size-parm
 *                         / parameter
 *
 *   filename-parm       := "filename" "=" value
 *
 *   creation-date-parm  := "creation-date" "=" quoted-date-time
 *
 *   modification-date-parm := "modification-date" "=" quoted-date-time
 *
 *   read-date-parm      := "read-date" "=" quoted-date-time
 *
 *   size-parm           := "size" "=" 1*DIGIT
 *
 *   quoted-date-time    := quoted-string
 *                         ; contents MUST be an RFC 822 `date-time'
 *                         ; numeric timezones (+HHMM or -HHMM) MUST be used
 *
 *   value := token / quoted-string (RFC 2045, continue to see)
 *
 * Parameter values longer than 78  characters, or which contain non-ASCII
 * characters, MUST be encoded as specified in [RFC 2184].
 *
 * We ignore this last part; stuff looks like this:
 * Content-Type: application/x-stuff
 *  title*1*=us-ascii'en'This%20is%20even%20more%20
 *  title*2*=%2A%2A%2Afun%2A%2A%2A%20
 *  title*3="isn't it!"
 *
 * No one does this. Also, the similar scheme described in RFC 5987 is
 * explicitly proscribed by RFC 7578.
 */
/*
 * This method consumes all the headers of a MIME part, looking for
 * Content-Disposition's name. It stops only on EOF or two consecutive CRLF
 * (relaxed to allow plain LF).
 */
std::optional<MimeContentDisposition> MimeContentDisposition::for_stream(
    NgxChainInputStream &is) {
  MimeContentDisposition cd{};

  // consumes data, up until the last matching character; case insensitive
  auto try_match_token = [](auto &gen, std::string_view token) {
    assert(token == to_lc(token));
    std::size_t i;
    for (i = 0; gen.has_next() && i < token.size(); i++) {
      if (std::tolower(gen.peek()) != token[i]) {
        break;
      }
      gen.next();
    }
    return i == token.size();
  };

  while (!is.eof()) {
    Generator<std::uint8_t> gen = unfold_next_header(is);
    if (!gen.has_next()) {
      // end of headers
      break;
    }

    // no space allowed before :
    static constexpr auto header_name_lc = "content-disposition:"sv;
    if (!try_match_token(gen, header_name_lc)) {
      // not the header we're looking for. Consume the rest of it and retry
      while (gen.has_next()) {
        gen.next();
      }
      continue;
    }

    // found the header
    // skip ws after : (matches PHP behavior)
    std::uint8_t ch;
    while (gen.has_next() && is_ext_ws(ch = gen.next())) {
    }

    if (!gen.has_next()) {
      // no value after content-disposition:[ \t\v\f]*
      continue;
    }

  next_parameter:
    // skip until we find a ;, which is what we're interested in
    while (gen.has_next() && gen.next() != ';') {
    }
  next_parameter_after_semicolon:
    // skip ws
    while (gen.has_next() && is_ext_ws(ch = gen.peek())) {
      gen.next();
    }
    if (!gen.has_next()) {
      // no more parameters
      continue;
    }

    std::string header_name;
    bool is_name = try_match_token(gen, "name=");
    if (!is_name) {
      // try to find = or ;
      while (gen.has_next()) {
        auto ch = gen.next();
        if (ch == '=') {
          // break so we can process the value. We can't just advance
          // to the next ; because the next ; may be quoted
          break;
        } else if (ch == ';') {
          goto next_parameter_after_semicolon;  // we already consumed ;
        }
      }
    }

    if (!gen.has_next()) {
      // no value after <parameter>=
      continue;
    }

    /*
     * https://datatracker.ietf.org/doc/html/rfc822#section-3.3
     * quoted-string = <"> *(qtext/quoted-pair) <">
     * qtext         =  <any CHAR excepting <">,      may be folded
     *                  "\" & CR, and including linear-white-space>
     * quoted-pair   =  "\" CHAR
     * CHAR          =  <any ASCII character>
     *
     * Browsers, however, deviate from this. Backlashes (\) are not used for
     * escaping, and so should be interpreted literally. Also, browsers send
     * bytes with the high bit set, in the same encoding as the html document.
     * This encoding is not transmitted by the browsers in any header.
     */
    std::string value;
    if (gen.peek() == '"') {
      gen.next();  // skip "
      while (gen.has_next() && (ch = gen.next()) != '"') {
        value.push_back(ch);
      }
      if (ch != '"') {
        // end of line before closing quote. Ignore value
        continue;  // next header
      }

      if (is_name) {
        // we got what we wanted
        if (value.find('%') != std::string::npos) {
          // decode the value
          cd.name = decode_urlencoded(value);
        } else {
          // save a copy
          cd.name = std::move(value);
        }
      }
      // maybe we got name= a second time though
      goto next_parameter;
    } else {
      std::string value;
      // continue until we get a space, tab, ;, or end of input
      while (gen.has_next() && (ch = gen.peek()) != ' ' && ch != '\t' &&
             ch != ';') {
        value.push_back(ch);
        gen.next();
      }

      if (!value.empty() && is_name) {
        cd.name = std::move(value);
      }
      goto next_parameter;
    }
  }  // while (!is.eof())

  if (cd.name.empty()) {
    return std::nullopt;
  }

  return std::move(cd);
}
}  // namespace datadog::nginx::security
