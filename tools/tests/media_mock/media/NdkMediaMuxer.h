#pragma once
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "NdkMediaCodec.h"
#include "NdkMediaError.h"
#include "NdkMediaFormat.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct AMediaMuxer AMediaMuxer;
typedef enum { AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4 = 0 } OutputFormat;
AMediaMuxer* AMediaMuxer_new(int, OutputFormat);
media_status_t AMediaMuxer_delete(AMediaMuxer*);
media_status_t AMediaMuxer_setOrientationHint(AMediaMuxer*, int);
ssize_t AMediaMuxer_addTrack(AMediaMuxer*, const AMediaFormat*);
media_status_t AMediaMuxer_start(AMediaMuxer*);
media_status_t AMediaMuxer_stop(AMediaMuxer*);
media_status_t AMediaMuxer_writeSampleData(AMediaMuxer*, size_t, const uint8_t*, const AMediaCodecBufferInfo*);
#ifdef __cplusplus
}
#endif
