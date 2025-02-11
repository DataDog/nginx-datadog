/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */

#pragma once

#include "framework.h"

extern "C" __declspec(dllexport) HRESULT
    __stdcall RegisterModule(DWORD dwServerVersion,
                             IHttpModuleRegistrationInfo *pModuleInfo,
                             IHttpServer *pGlobalInfo);
