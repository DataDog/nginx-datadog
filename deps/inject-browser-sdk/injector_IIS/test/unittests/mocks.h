/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */

#pragma once
#include "framework.h"
#include "gmock/gmock.h"

class MockModule : public IHttpModuleRegistrationInfo {
public:
  MOCK_METHOD(HTTP_MODULE_ID, GetId, (), (const, override));
  MOCK_METHOD(PCWSTR, GetName, (), (const, override));
  MOCK_METHOD(HRESULT, SetGlobalNotifications,
              (IN CGlobalModule * pGlobalModule,
               IN DWORD dwGlobalNotifications),
              (override));
  MOCK_METHOD(HRESULT, SetPriorityForGlobalNotification,
              (IN DWORD dwGlobalNotification, IN PCWSTR pszPriority),
              (override));
  MOCK_METHOD(HRESULT, SetRequestNotifications,
              (IN IHttpModuleFactory * pModuleFactory,
               IN DWORD dwRequestNotifications,
               IN DWORD dwPostRequestNotifications),
              (override));
  MOCK_METHOD(HRESULT, SetPriorityForRequestNotification,
              (IN DWORD dwRequestNotification, IN PCWSTR pszPriority),
              (override));
};

class MockServer : public IHttpServer {
public:
  MOCK_METHOD(BOOL, IsCommandLineLaunch, (), (const, override));
  MOCK_METHOD(PCWSTR, GetAppPoolName, (), (const, override));
  MOCK_METHOD(HRESULT, AssociateWithThreadPool,
              (_In_ HANDLE hHandle,
               _In_ LPOVERLAPPED_COMPLETION_ROUTINE completionRoutine),
              (override));
  MOCK_METHOD(VOID, IncrementThreadCount, (), (override));
  MOCK_METHOD(VOID, DecrementThreadCount, (), (override));
  MOCK_METHOD(VOID, ReportUnhealthy,
              (_In_ PCWSTR pszReasonString, _In_ HRESULT hrReason), (override));
  MOCK_METHOD(VOID, RecycleProcess, (_In_ PCWSTR pszReason), (override));
  MOCK_METHOD(IAppHostAdminManager *, GetAdminManager, (), (const, override));
  MOCK_METHOD(HRESULT, GetFileInfo,
              (_In_ PCWSTR pszPhysicalPath, _In_ HANDLE hUserToken,
               _In_ PSID pSid, _In_ PCWSTR pszChangeNotificationPath,
               _In_ HANDLE hChangeNotificationToken, _In_ BOOL fCache,
               _Outptr_ IHttpFileInfo **ppFileInfo,
               _In_opt_ IHttpTraceContext *pHttpTraceContext),
              (override));
  MOCK_METHOD(HRESULT, FlushKernelCache, (_In_ PCWSTR pszUrl), (override));
  MOCK_METHOD(HRESULT, DoCacheOperation,
              (_In_ CACHE_OPERATION cacheOperation,
               _In_ IHttpCacheKey *pCacheKey,
               _Outptr_ IHttpCacheSpecificData **ppCacheSpecificData,
               _In_opt_ IHttpTraceContext *pHttpTraceContext),
              (override));
  MOCK_METHOD(GLOBAL_NOTIFICATION_STATUS, NotifyCustomNotification,
              (_In_ ICustomNotificationProvider * pCustomOutput), (override));
  MOCK_METHOD(IHttpPerfCounterInfo *, GetPerfCounterInfo, (), (override));
  MOCK_METHOD(VOID, RecycleApplication, (_In_ PCWSTR pszAppConfigPath),
              (override));
  MOCK_METHOD(VOID, NotifyConfigurationChange, (_In_ PCWSTR pszPath),
              (override));
  MOCK_METHOD(VOID, NotifyFileChange, (_In_ PCWSTR pszFileName), (override));
  MOCK_METHOD(IDispensedHttpModuleContextContainer *, DispenseContainer, (),
              (override));
  MOCK_METHOD(HRESULT, AddFragmentToCache,
              (_In_ HTTP_DATA_CHUNK * pDataChunk, _In_ PCWSTR pszFragmentName),
              (override));
  MOCK_METHOD(HRESULT, ReadFragmentFromCache,
              (_In_ PCWSTR pszFragmentName,
               _Out_writes_bytes_all_(cbSize) BYTE *pvBuffer, _In_ DWORD cbSize,
               _Out_ DWORD *pcbCopied),
              (override));
  MOCK_METHOD(HRESULT, RemoveFragmentFromCache, (_In_ PCWSTR pszFragmentName),
              (override));
  MOCK_METHOD(HRESULT, GetWorkerProcessSettings,
              (_Outptr_ IWpfSettings * *ppWorkerProcessSettings), (override));
  MOCK_METHOD(HRESULT, GetProtocolManagerCustomInterface,
              (_In_ PCWSTR pProtocolManagerDll,
               _In_ PCWSTR pProtocolManagerDllInitFunction,
               _In_ DWORD dwCustomInterfaceId,
               _Outptr_ PVOID *ppCustomInterface),
              (override));
  MOCK_METHOD(BOOL, SatisfiesPrecondition,
              (_In_ PCWSTR pszPrecondition, _Out_ BOOL *pfUnknownPrecondition),
              (const, override));
  MOCK_METHOD(IHttpTraceContext *, GetTraceContext, (), (const, override));
  MOCK_METHOD(HRESULT, RegisterFileChangeMonitor,
              (_In_ PCWSTR pszPath, _In_ HANDLE hToken,
               _Outptr_ IHttpFileMonitor **ppFileMonitor),
              (override));
  MOCK_METHOD(HRESULT, GetExtendedInterface,
              (_In_ HTTP_SERVER_INTERFACE_VERSION version,
               _Outptr_ PVOID *ppInterface),
              (override));
};

class MockHttpContext : public IHttpContext {
  VOID *m_allocatedMemory;

public:
  MockHttpContext() : m_allocatedMemory(NULL){};
  ~MockHttpContext() {
    if (m_allocatedMemory != NULL)
      free(m_allocatedMemory);
  };

  MOCK_METHOD(IHttpSite *, GetSite, (), (override));
  MOCK_METHOD(IHttpApplication *, GetApplication, (), (override));
  MOCK_METHOD(IHttpConnection *, GetConnection, (), (override));
  MOCK_METHOD(IHttpRequest *, GetRequest, (), (override));
  MOCK_METHOD(IHttpResponse *, GetResponse, (), (override));
  MOCK_METHOD(BOOL, GetResponseHeadersSent, (), (const, override));
  MOCK_METHOD(IHttpUser *, GetUser, (), (const, override));
  MOCK_METHOD(IHttpModuleContextContainer *, GetModuleContextContainer, (),
              (override));
  MOCK_METHOD(VOID, IndicateCompletion,
              (_In_ REQUEST_NOTIFICATION_STATUS notificationStatus),
              (override));
  MOCK_METHOD(HRESULT, PostCompletion, (_In_ DWORD cbBytes), (override));
  MOCK_METHOD(VOID, DisableNotifications,
              (_In_ DWORD dwNotifications, _In_ DWORD dwPostNotifications),
              (override));
  MOCK_METHOD(BOOL, GetNextNotification,
              (_In_ REQUEST_NOTIFICATION_STATUS status,
               _Out_ DWORD *pdwNotification, _Out_ BOOL *pfIsPostNotification,
               _Outptr_ CHttpModule **ppModuleInfo,
               _Outptr_ IHttpEventProvider **ppRequestOutput),
              (override));
  MOCK_METHOD(BOOL, GetIsLastNotification,
              (_In_ REQUEST_NOTIFICATION_STATUS status), (override));
  MOCK_METHOD(HRESULT, ExecuteRequest,
              (_In_ BOOL fAsync, _In_ IHttpContext *pHttpContext,
               _In_ DWORD dwExecuteFlags, _In_ IHttpUser *pHttpUser,
               _Out_ BOOL *pfCompletionExpected),
              (override));
  MOCK_METHOD(DWORD, GetExecuteFlags, (), (const, override));
  MOCK_METHOD(HRESULT, GetServerVariable,
              (_In_ PCSTR pszVariableName,
               _Outptr_result_bytebuffer_(*pcchValueLength) PCWSTR *ppszValue,
               _Out_ DWORD *pcchValueLength),
              (override));
  MOCK_METHOD(HRESULT, GetServerVariable,
              (_In_ PCSTR pszVariableName,
               _Outptr_result_bytebuffer_(*pcchValueLength) PCSTR *ppszValue,
               _Out_ DWORD *pcchValueLength),
              (override));
  MOCK_METHOD(HRESULT, SetServerVariable,
              (PCSTR pszVariableName, PCWSTR pszVariableValue), (override));
  MOCK_METHOD(IHttpUrlInfo *, GetUrlInfo, (), (override));
  MOCK_METHOD(IMetadataInfo *, GetMetadata, (), (override));
  MOCK_METHOD(_Ret_writes_(*pcchPhysicalPath) PCWSTR, GetPhysicalPath,
              (_Out_ DWORD * pcchPhysicalPath), (override));
  MOCK_METHOD(_Ret_writes_(*pcchScriptName) PCWSTR, GetScriptName,
              (_Out_ DWORD * pcchScriptName), (const, override));
  MOCK_METHOD(_Ret_writes_(*pcchScriptTranslated) PCWSTR, GetScriptTranslated,
              (_Out_ DWORD * pcchScriptTranslated), (override));
  MOCK_METHOD(IScriptMapInfo *, GetScriptMap, (), (const, override));
  MOCK_METHOD(VOID, SetRequestHandled, (), (override));
  MOCK_METHOD(IHttpFileInfo *, GetFileInfo, (), (const, override));
  MOCK_METHOD(HRESULT, MapPath,
              (_In_ PCWSTR pszUrl,
               _Inout_updates_(*pcbPhysicalPath) PWSTR pszPhysicalPath,
               _Inout_ DWORD *pcbPhysicalPath),
              (override));
  MOCK_METHOD(HRESULT, NotifyCustomNotification,
              (_In_ ICustomNotificationProvider * pCustomOutput,
               _Out_ BOOL *pfCompletionExpected),
              (override));
  MOCK_METHOD(IHttpContext *, GetParentContext, (), (const, override));
  MOCK_METHOD(IHttpContext *, GetRootContext, (), (const, override));
  MOCK_METHOD(HRESULT, CloneContext,
              (_In_ DWORD dwCloneFlags, _Outptr_ IHttpContext **ppHttpContext),
              (override));
  MOCK_METHOD(HRESULT, ReleaseClonedContext, (), (override));
  MOCK_METHOD(HRESULT, GetCurrentExecutionStats,
              (_Out_ DWORD * pdwNotification,
               _Out_ DWORD *pdwNotificationStartTickCount,
               _Out_ PCWSTR *ppszModule, _Out_ DWORD *pdwModuleStartTickCount,
               _Out_ DWORD *pdwAsyncNotification,
               _Out_ DWORD *pdwAsyncNotificationStartTickCount),
              (const, override));
  MOCK_METHOD(IHttpTraceContext *, GetTraceContext, (), (const, override));
  MOCK_METHOD(HRESULT, CancelIo, (), (override));
  MOCK_METHOD(HRESULT, MapHandler,
              (_In_ DWORD dwSiteId, _In_ PCWSTR pszSiteName, _In_ PCWSTR pszUrl,
               _In_ PCSTR pszVerb, _Outptr_ IScriptMapInfo **ppScriptMap,
               _In_ BOOL fIgnoreWildcardMappings),
              (override));
  MOCK_METHOD(HRESULT, GetExtendedInterface,
              (_In_ HTTP_CONTEXT_INTERFACE_VERSION version,
               _Outptr_ PVOID *ppInterface),
              (override));
  MOCK_METHOD(HRESULT, GetServerVarChanges,
              (_In_ DWORD dwOldChangeNumber, _Out_ DWORD *pdwNewChangeNumber,
               _Inout_ DWORD *pdwVariableSnapshot,
               _Inout_ _At_(*ppVariableNameSnapshot,
                            _Pre_readable_size_(*pdwVariableSnapshot)
                                _Post_readable_size_(*pdwVariableSnapshot))
                   PCSTR **ppVariableNameSnapshot,
               _Inout_ _At_(*ppVariableValueSnapshot,
                            _Pre_readable_size_(*pdwVariableSnapshot)
                                _Post_readable_size_(*pdwVariableSnapshot))
                   PCWSTR **ppVariableValueSnapshot,
               _Out_ DWORD *pdwDiffedVariables,
               _Out_ DWORD **ppDiffedVariableIndices),
              (override));

  // To make memory management a little easier, we'll implement this function
  // instead of mocking it
  VOID *AllocateRequestMemory(DWORD size) override {
    if (m_allocatedMemory != NULL) {
      free(m_allocatedMemory);
    };
    m_allocatedMemory = (VOID *)malloc(size);
    return m_allocatedMemory;
  };
};

class MockHttpSite : public IHttpSite {
public:
  MOCK_METHOD(DWORD, GetSiteId, (), (const, override));
  MOCK_METHOD(PCWSTR, GetSiteName, (), (const, override));
  MOCK_METHOD(IHttpModuleContextContainer *, GetModuleContextContainer, (),
              (override));
  MOCK_METHOD(IHttpPerfCounterInfo *, GetPerfCounterInfo, (), (override));
};

class MockHttpApplication : public IHttpApplication {
public:
  MOCK_METHOD(PCWSTR, GetApplicationPhysicalPath, (), (const, override));
  MOCK_METHOD(PCWSTR, GetApplicationId, (), (const, override));
  MOCK_METHOD(PCWSTR, GetAppConfigPath, (), (const, override));
  MOCK_METHOD(IHttpModuleContextContainer *, GetModuleContextContainer, (),
              (override));
};
class MockHttpResponse : public IHttpResponse {
public:
  // Because both overloads of GetRawHttpResponse have the same arguments, gmock
  // has a hard time differentiating between the two - so we'll only mock the
  // version which we plan to use
  const HTTP_RESPONSE *GetRawHttpResponse() const override { return nullptr; }
  MOCK_METHOD(HTTP_RESPONSE *, GetRawHttpResponse, (), (override));

  MOCK_METHOD(IHttpCachePolicy *, GetCachePolicy, (), (override));
  MOCK_METHOD(HRESULT, SetStatus,
              (_In_ USHORT statusCode, _In_ PCSTR pszReason,
               _In_ USHORT SubStatus, _In_ HRESULT hrErrorToReport,
               _In_opt_ IAppHostConfigException *pException,
               _In_ BOOL fTrySkipCustomErrors),
              (override));
  MOCK_METHOD(HRESULT, SetHeader,
              (_In_ PCSTR pszHeaderName, _In_ PCSTR pszHeaderValue,
               _In_ USHORT cchHeaderValue, _In_ BOOL fReplace),
              (override));
  MOCK_METHOD(HRESULT, SetHeader,
              (_In_ HTTP_HEADER_ID ulHeaderIndex, _In_ PCSTR pszHeaderValue,
               _In_ USHORT cchHeaderValue, _In_ BOOL fReplace),
              (override));
  MOCK_METHOD(HRESULT, DeleteHeader, (_In_ PCSTR pszHeaderName), (override));
  MOCK_METHOD(HRESULT, DeleteHeader, (_In_ HTTP_HEADER_ID ulHeaderIndex),
              (override));
  MOCK_METHOD(_Ret_writes_bytes_(*pcchHeaderValue) PCSTR, GetHeader,
              (_In_ PCSTR pszHeaderName, _Out_ USHORT *pcchHeaderValue),
              (const, override));
  MOCK_METHOD(_Ret_writes_bytes_(*pcchHeaderValue) PCSTR, GetHeader,
              (_In_ HTTP_HEADER_ID ulHeaderIndex,
               _Out_ USHORT *pcchHeaderValue),
              (const, override));
  MOCK_METHOD(VOID, Clear, (), (override));
  MOCK_METHOD(VOID, ClearHeaders, (), (override));
  MOCK_METHOD(VOID, SetNeedDisconnect, (), (override));
  MOCK_METHOD(VOID, ResetConnection, (), (override));
  MOCK_METHOD(VOID, DisableKernelCache, (ULONG reason), (override));
  MOCK_METHOD(BOOL, GetKernelCacheEnabled, (), (const, override));
  MOCK_METHOD(VOID, SuppressHeaders, (), (override));
  MOCK_METHOD(BOOL, GetHeadersSuppressed, (), (const, override));
  MOCK_METHOD(HRESULT, Flush,
              (_In_ BOOL fAsync, _In_ BOOL fMoreData, _Out_ DWORD *pcbSent,
               _Out_ BOOL *pfCompletionExpected),
              (override));
  MOCK_METHOD(HRESULT, Redirect,
              (_In_ PCSTR pszUrl, _In_ BOOL fResetStatusCode,
               _In_ BOOL fIncludeParameters),
              (override));
  MOCK_METHOD(HRESULT, WriteEntityChunkByReference,
              (_In_ HTTP_DATA_CHUNK * pDataChunk, _In_ LONG lInsertPosition),
              (override));
  MOCK_METHOD(HRESULT, WriteEntityChunks,
              (_In_reads_(nChunks) HTTP_DATA_CHUNK * pDataChunks,
               _In_ DWORD nChunks, _In_ BOOL fAsync, _In_ BOOL fMoreData,
               _Out_ DWORD *pcbSent, _Out_ BOOL *pfCompletionExpected),
              (override));
  MOCK_METHOD(VOID, DisableBuffering, (), (override));
  MOCK_METHOD(VOID, GetStatus,
              (_Out_ USHORT * pStatusCode, _Out_ USHORT *pSubStatus,
               _Outptr_opt_result_bytebuffer_(*pcchReason) PCSTR *ppszReason,
               _Out_ USHORT *pcchReason, _Out_ HRESULT *phrErrorToReport,
               _Outptr_opt_ PCWSTR *ppszModule, _Out_ DWORD *pdwNotification,
               _Outptr_opt_ IAppHostConfigException **,
               _Out_ BOOL *pfTrySkipCustomErrors),
              (override));
  MOCK_METHOD(HRESULT, SetErrorDescription,
              (_In_reads_(cchDescription) PCWSTR pszDescription,
               _In_ DWORD cchDescription, _In_ BOOL fHtmlEncode),
              (override));
  MOCK_METHOD(_Ret_writes_(*pcchDescription) PCWSTR, GetErrorDescription,
              (_Out_ DWORD * pcchDescription), (override));
  MOCK_METHOD(
      HRESULT, GetHeaderChanges,
      (_In_ DWORD dwOldChangeNumber, _Out_ DWORD *pdwNewChangeNumber,
       _Inout_ PCSTR knownHeaderSnapshot[HttpHeaderResponseMaximum],
       _Inout_ DWORD *pdwUnknownHeaderSnapshot,
       _Inout_ PCSTR **ppUnknownHeaderNameSnapshot,
       _Inout_ PCSTR **ppUnknownHeaderValueSnapshot,
       _Out_writes_(HttpHeaderResponseMaximum + 1)
           DWORD diffedKnownHeaderIndices[HttpHeaderResponseMaximum + 1],
       _Out_ DWORD *pdwDiffedUnknownHeaders,
       _Out_ DWORD **ppDiffedUnknownHeaderIndices),
      (override));
  MOCK_METHOD(VOID, CloseConnection, (), (override));
};
