#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "NdkMediaError.h"
#include "NdkMediaFormat.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AMediaExtractor AMediaExtractor;
enum { AMEDIAEXTRACTOR_SAMPLE_FLAG_SYNC = 1, AMEDIAEXTRACTOR_SAMPLE_FLAG_ENCRYPTED = 2 };
AMediaExtractor* AMediaExtractor_new();
media_status_t AMediaExtractor_delete(AMediaExtractor*);
media_status_t AMediaExtractor_setDataSourceFd(AMediaExtractor*, int, off64_t, off64_t);
size_t AMediaExtractor_getTrackCount(AMediaExtractor*);
AMediaFormat* AMediaExtractor_getTrackFormat(AMediaExtractor*, size_t);
media_status_t AMediaExtractor_selectTrack(AMediaExtractor*, size_t);
ssize_t AMediaExtractor_readSampleData(AMediaExtractor*, uint8_t*, size_t);
uint32_t AMediaExtractor_getSampleFlags(AMediaExtractor*);
int AMediaExtractor_getSampleTrackIndex(AMediaExtractor*);
int64_t AMediaExtractor_getSampleTime(AMediaExtractor*);
bool AMediaExtractor_advance(AMediaExtractor*);
#ifdef __cplusplus
}
#endif
