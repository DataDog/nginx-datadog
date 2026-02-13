/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */


#ifndef MAJ_VER
#define MAJ_VER 0
#endif

#ifndef MIN_VER
#define MIN_VER 1
#endif

#ifndef PATCH_VER
#define PATCH_VER 0
#endif

#define RC_FILE_VERSION MAJ_VER, MIN_VER, PATCH_VER, 0

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#define FILE_VERSION_STRING TO_STRING(MAJ_VER.MIN_VER.PATCH_VER.0)
