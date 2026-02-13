// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

#include "http_module.h"
#include "mocks.h"
#include "gtest/gtest.h"

using namespace datadog::rum;

using ::testing::_;
using ::testing::DoAll;
using ::testing::Invoke;
using ::testing::Matcher;
using ::testing::Return;
using ::testing::StrEq;

struct ContentTypeParams {
  std::string_view input;
  bool expected;
};

class HttpModuleTest : public testing::Test,
                       public testing::WithParamInterface<ContentTypeParams> {};

class SilentLogger : public datadog::rum::Logger {
public:
  SilentLogger() = default;
  void info(std::string) override{};
  void debug(std::string) override{};
  void error(std::string) override{};
};

TEST_P(HttpModuleTest, ShouldAttemptInjection_ContentType) {
  HttpModule module;
  SilentLogger logger;
  MockHttpResponse httpResponse;

  const auto [content_type, expected] = GetParam();

  EXPECT_CALL(httpResponse, GetHeader(Matcher<PCSTR>(StrEq("Content-Type")), _))
      .WillOnce(DoAll(
          Invoke([content_type](_In_ PCSTR, _Out_ USHORT *headerValueLen) {
            *headerValueLen = (USHORT)content_type.size();
          }),
          Return(content_type.data())));

  EXPECT_EQ(expected, module.ShouldAttemptInjection(httpResponse, logger));
}

constexpr ContentTypeParams kContentTypes[] = {
    {.input = "foo/bar", .expected = false},
    {.input = "", .expected = false},
};

// NOTE(@dmehala): tried Value-Parameterized Tests. Conclusion: PITA. I'll stick
// to repeat stuff.
INSTANTIATE_TEST_SUITE_P(CheckContentType, HttpModuleTest,
                         testing::ValuesIn(kContentTypes));

TEST(HttpModuleTest, ShouldAttemptInjection_ReturnsFalseFor2xxAnd3xx) {
  HttpModule module;
  SilentLogger logger;
  MockHttpResponse httpResponse;

  struct TestCase final {
    USHORT min;
    USHORT max;
    bool expected;
  };

  constexpr TestCase test_cases[] = {
      {.min = 100, .max = 199},
      {.min = 300, .max = 399},
  };

  const std::string_view contentTypeHeaderValue = "text/html";

  for (const auto &current_case : test_cases) {
    for (USHORT code = current_case.min; code <= current_case.max; code++) {
      EXPECT_CALL(httpResponse,
                  GetHeader(Matcher<PCSTR>(StrEq("Content-Type")), _))
          .WillOnce(DoAll(Invoke([contentTypeHeaderValue](
                                     _In_ PCSTR, _Out_ USHORT *headerValueLen) {
                            *headerValueLen =
                                (USHORT)contentTypeHeaderValue.size();
                          }),
                          Return(contentTypeHeaderValue.data())));

      EXPECT_CALL(httpResponse, GetStatus(_, _, _, _, _, _, _, _, _))
          .WillOnce(DoAll(
              Invoke([code](_Out_ USHORT *pStatusCode, _Out_ USHORT *,
                            _Outptr_opt_result_bytebuffer_(*pcchReason) PCSTR *,
                            _Out_ USHORT *, _Out_ HRESULT *,
                            _Outptr_opt_ PCWSTR *, _Out_ DWORD *,
                            _Outptr_opt_ IAppHostConfigException **,
                            _Out_ BOOL *) { *pStatusCode = code; }),
              Return()));
      EXPECT_FALSE(module.ShouldAttemptInjection(httpResponse, logger));
    }
  }
}

TEST(HttpModuleTest, ShouldAttemptInjection_ReturnsTrueForValidStatusCode) {
  HttpModule module;
  SilentLogger logger;
  MockHttpResponse httpResponse;

  struct TestCase final {
    USHORT min;
    USHORT max;
    bool expected;
  };

  constexpr TestCase test_cases[] = {
      {.min = 200, .max = 299},
      {.min = 400, .max = 499},
      {.min = 500, .max = 599},
  };

  const std::string_view contentTypeHeaderValue = "text/html";

  for (const auto &current_case : test_cases) {
    for (USHORT code = current_case.min; code <= current_case.max; code++) {
      EXPECT_CALL(httpResponse,
                  GetHeader(Matcher<PCSTR>(StrEq("Content-Type")), _))
          .WillOnce(DoAll(Invoke([contentTypeHeaderValue](
                                     _In_ PCSTR, _Out_ USHORT *headerValueLen) {
                            *headerValueLen =
                                (USHORT)contentTypeHeaderValue.size();
                          }),
                          Return(contentTypeHeaderValue.data())));

      EXPECT_CALL(httpResponse,
                  GetHeader(Matcher<PCSTR>(StrEq("x-datadog-rum-injected")), _))
          .WillRepeatedly(
              DoAll(Invoke([](_In_ PCSTR, _Out_ USHORT *headerValueLen) {
                      *headerValueLen = 0;
                    }),
                    Return("")));

      EXPECT_CALL(httpResponse, GetStatus(_, _, _, _, _, _, _, _, _))
          .WillOnce(DoAll(
              Invoke([code](_Out_ USHORT *pStatusCode, _Out_ USHORT *,
                            _Outptr_opt_result_bytebuffer_(*pcchReason) PCSTR *,
                            _Out_ USHORT *, _Out_ HRESULT *,
                            _Outptr_opt_ PCWSTR *, _Out_ DWORD *,
                            _Outptr_opt_ IAppHostConfigException **,
                            _Out_ BOOL *) { *pStatusCode = code; }),
              Return()));

      EXPECT_TRUE(module.ShouldAttemptInjection(httpResponse, logger));
    }
  }
}

TEST(HttpModuleTest,
     ShouldAttemptInjection_ReturnsFalseWhenCustomHeaderPresent) {
  HttpModule module;
  SilentLogger logger;
  MockHttpResponse httpResponse;

  const char *contentTypeHeader = "Content-Type";
  const char *contentTypeHeaderValue = "text/html";

  const char *customHeader = "x-datadog-rum-injected";
  const char *customHeaderValue = "1";
  USHORT code = 200;

  EXPECT_CALL(httpResponse,
              GetHeader(Matcher<PCSTR>(StrEq(contentTypeHeader)), _))
      .WillOnce(DoAll(Invoke([contentTypeHeaderValue](
                                 _In_ PCSTR, _Out_ USHORT *headerValueLen) {
                        *headerValueLen =
                            (USHORT)strlen(contentTypeHeaderValue);
                      }),
                      Return(contentTypeHeaderValue)));

  EXPECT_CALL(httpResponse, GetStatus(_, _, _, _, _, _, _, _, _))
      .WillOnce(DoAll(
          Invoke([code](_Out_ USHORT *pStatusCode, _Out_ USHORT *,
                        _Outptr_opt_result_bytebuffer_(*pcchReason) PCSTR *,
                        _Out_ USHORT *, _Out_ HRESULT *, _Outptr_opt_ PCWSTR *,
                        _Out_ DWORD *, _Outptr_opt_ IAppHostConfigException **,
                        _Out_ BOOL *) { *pStatusCode = code; }),
          Return()));

  EXPECT_CALL(httpResponse, GetHeader(Matcher<PCSTR>(StrEq(customHeader)), _))
      .WillOnce(DoAll(
          Invoke([customHeaderValue](_In_ PCSTR, _Out_ USHORT *headerValueLen) {
            *headerValueLen = (USHORT)strlen(customHeaderValue);
          }),
          Return(customHeaderValue)));

  EXPECT_FALSE(module.ShouldAttemptInjection(httpResponse, logger));
}

TEST(HttpModuleTest, ShouldAttemptInjection_ReturnsTrueForValidHeaders) {
  HttpModule module;
  SilentLogger logger;
  MockHttpResponse httpResponse;

  const char *contentTypeHeader = "Content-Type";
  const char *contentTypeHeaderValue = "Text/HTML; Charset=\"utf - 8\"";

  const char *customHeader = "x-datadog-rum-injected";
  const char *customHeaderValue = "";
  USHORT code = 200;

  EXPECT_CALL(httpResponse, GetStatus(_, _, _, _, _, _, _, _, _))
      .WillOnce(DoAll(
          Invoke([code](_Out_ USHORT *pStatusCode, _Out_ USHORT *,
                        _Outptr_opt_result_bytebuffer_(*pcchReason) PCSTR *,
                        _Out_ USHORT *, _Out_ HRESULT *, _Outptr_opt_ PCWSTR *,
                        _Out_ DWORD *, _Outptr_opt_ IAppHostConfigException **,
                        _Out_ BOOL *) { *pStatusCode = code; }),
          Return()));

  EXPECT_CALL(httpResponse,
              GetHeader(Matcher<PCSTR>(StrEq(contentTypeHeader)), _))
      .WillOnce(DoAll(Invoke([contentTypeHeaderValue](
                                 _In_ PCSTR, _Out_ USHORT *headerValueLen) {
                        *headerValueLen =
                            (USHORT)strlen(contentTypeHeaderValue);
                      }),
                      Return(contentTypeHeaderValue)));

  EXPECT_CALL(httpResponse, GetHeader(Matcher<PCSTR>(StrEq(customHeader)), _))
      .WillOnce(DoAll(
          Invoke([customHeaderValue](_In_ PCSTR, _Out_ USHORT *headerValueLen) {
            *headerValueLen = (USHORT)strlen(customHeaderValue);
          }),
          Return(customHeaderValue)));

  EXPECT_TRUE(module.ShouldAttemptInjection(httpResponse, logger));
}
