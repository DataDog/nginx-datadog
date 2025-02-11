// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

#include "http_module.h"
#include "module_context.h"
#include "telemetry.h"
#include <cassert>

namespace datadog::rum {
namespace {

constexpr std::string_view k_injection_header = "x-datadog-rum-injected";

std::string_view get_header(IHttpResponse &http_response,
                            std::string_view header) {
  USHORT length;
  PCSTR data = http_response.GetHeader(header.data(), &length);
  if (data == nullptr) {
    return "";
  }

  return std::string_view(data, length);
}

} // namespace

REQUEST_NOTIFICATION_STATUS
HttpModule::OnBeginRequest(IN IHttpContext *http_context,
                           IN IHttpEventProvider *event_provider) {
  assert(http_context != nullptr);
  assert(event_provider != nullptr);
  IHttpRequest *http_request = http_context->GetRequest();
  if (http_request == nullptr) {
    assert(true);
    return RQ_NOTIFICATION_CONTINUE;
  }

  auto opaque_ptr = http_context->GetApplication()
                        ->GetModuleContextContainer()
                        ->GetModuleContext(g_module_id);
  if (opaque_ptr == nullptr) {
    assert(true);
    return RQ_NOTIFICATION_CONTINUE;
  }

  auto &ctx = *static_cast<ModuleContext *>(opaque_ptr);
  if (ctx.js_snippet == nullptr) {
    ctx.logger->debug("Skipping RUM injection: disabled");
    return RQ_NOTIFICATION_CONTINUE;
  }

  HRESULT result =
      http_request->SetHeader("x-datadog-rum-injection-pending", "1", 1, TRUE);
  if (result != S_OK) {
    event_provider->SetErrorStatus(result);
    ctx.logger->error("Error setting request header: " +
                      std::system_category().message(result));
  }

  return RQ_NOTIFICATION_CONTINUE;
}

REQUEST_NOTIFICATION_STATUS
HttpModule::OnSendResponse(IN IHttpContext *http_context,
                           IN ISendResponseProvider *) {
  assert(http_context != nullptr);
  auto http_response = http_context->GetResponse();
  if (http_response == nullptr) {
    assert(true);
    return RQ_NOTIFICATION_CONTINUE;
  }

  auto opaque_ptr = http_context->GetApplication()
                        ->GetModuleContextContainer()
                        ->GetModuleContext(g_module_id);
  if (opaque_ptr == nullptr) {
    assert(true);
    return RQ_NOTIFICATION_CONTINUE;
  }

  auto &ctx = *static_cast<ModuleContext *>(opaque_ptr);
  if (ctx.js_snippet == nullptr) {
    ctx.logger->debug("Skipping RUM injection: disabled");
    return RQ_NOTIFICATION_CONTINUE;
  }

  if (!ShouldAttemptInjection(*http_response, *ctx.logger)) {
    return RQ_NOTIFICATION_CONTINUE;
  }

  return PerformInjection(*http_context, *http_response, ctx);
}

bool HttpModule::ShouldAttemptInjection(IHttpResponse &pHttpResponse,
                                        Logger &logger) const {
  // First, we must validate the the content type is 'text/html'
  // Note that the value of the Content-Type header is case insensitive, and it
  // may or may not be be followed by semicolon-delimited parameters
  //   ie Text/HTML;Charset="utf-8"
  // see https://www.rfc-editor.org/rfc/rfc9110#name-content-type

  if (_strnicmp(get_header(pHttpResponse, "Content-Type").data(), "text/html",
                9) != 0) {
    telemetry::injection_skip::invalid_content_type->inc();
    logger.debug("Skipping RUM injection: content type is not text/html ");
    return false;
  }

  USHORT statusCode = 0;
  pHttpResponse.GetStatus(&statusCode);

  // only inject if response code is 2xx, 4xx, 5xx
  if (statusCode < 200 || (statusCode >= 300 && statusCode < 400)) {
    logger.debug(
        std::format("Skipping RUM injection: return code {}", statusCode));
    return false;
  }

  // We must also validate that another injector in this environment hasn't
  // previously attempted injection into this response
  if (!get_header(pHttpResponse, k_injection_header).empty()) {
    telemetry::injection_skip::already_injected->inc();
    logger.debug("Skipping RUM injection: injection has "
                 "already been attempted on this response");
    return false;
  }

  return true;
}

REQUEST_NOTIFICATION_STATUS
HttpModule::PerformInjection(IHttpContext &pHttpContext,
                             IHttpResponse &pHttpResponse, ModuleContext &ctx) {
  assert(ctx.js_snippet);
  assert(ctx.logger);
  const Snippet *snippet = ctx.js_snippet;
  auto &logger = *ctx.logger;

  HTTP_RESPONSE *pResponseStruct = pHttpResponse.GetRawHttpResponse();
  if (pResponseStruct == nullptr) {
    logger.debug("Raw HTTP response was null");
    return RQ_NOTIFICATION_CONTINUE;
  }

  if (pResponseStruct->EntityChunkCount <= 0) {
    logger.debug("Raw HTTP response does not contain any data");
    return RQ_NOTIFICATION_CONTINUE;
  }

  Injector *injector = injector_create(snippet);
  if (injector == nullptr) {
    logger.error("Error creating injector");
    return RQ_NOTIFICATION_CONTINUE;
  }

  bool injected = false;

  // Write the existing data chunks to the injector, and then write the
  // resulting byte slices into our new response string
  for (int i = 0; i < pResponseStruct->EntityChunkCount; i++) {
    HTTP_DATA_CHUNK &dataChunk = pResponseStruct->pEntityChunks[i];
    if (dataChunk.DataChunkType != HttpDataChunkFromMemory) {
      logger.debug(
          "Skipping response which is not in memory: response type = " +
          std::to_string(dataChunk.DataChunkType));
      injector_cleanup(injector);
      return RQ_NOTIFICATION_CONTINUE;
    }

    auto chunkLength = dataChunk.FromMemory.BufferLength;

    auto injectorResult = injector_write(
        injector, (uint8_t *)dataChunk.FromMemory.pBuffer, chunkLength);
    if (injectorResult.injected) {
      const size_t bufferLength =
          sizeof(uint8_t) * (chunkLength + snippet->length);
      void *buffer = pHttpContext.AllocateRequestMemory(bufferLength);

      auto *offset = (uint8_t *)buffer;
      for (size_t i = 0; i < injectorResult.slices_length; ++i) {
        const auto &slice = injectorResult.slices[i];
        memcpy(offset, slice.start, slice.length);
        offset += slice.length;
      }

      dataChunk.FromMemory.pBuffer = (PVOID)buffer;
      dataChunk.FromMemory.BufferLength = bufferLength;

      injected = true;

      HRESULT result =
          pHttpResponse.SetHeader(k_injection_header.data(), "1", 1, TRUE);
      if (result != S_OK) {
        logger.error("Error setting injection header: " +
                     std::system_category().message(result));
        return RQ_NOTIFICATION_CONTINUE;
      }
      break;
    }
  }

  if (injected) {
    telemetry::injection_succeed->inc();
  } else {
    telemetry::injection_failed->inc();
  }

  injector_cleanup(injector);

  logger.debug("Writing new response");

  DWORD bytesSent;
  HRESULT result = pHttpResponse.Flush(FALSE, FALSE, &bytesSent);
  if (FAILED(result)) {
    logger.debug("Error while flushing response: " +
                 std::system_category().message(result));
  }

  return RQ_NOTIFICATION_FINISH_REQUEST;
}

} // namespace datadog::rum
