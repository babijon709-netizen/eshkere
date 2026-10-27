#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "NdkMediaError.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AMediaFormat AMediaFormat;
media_status_t AMediaFormat_delete(AMediaFormat*);
bool AMediaFormat_getInt32(AMediaFormat*, const char*, int32_t*);
bool AMediaFormat_getString(AMediaFormat*, const char*, const char**);
bool AMediaFormat_getBuffer(AMediaFormat*, const char*, void**, size_t*);
#ifdef __cplusplus
}
#endif
