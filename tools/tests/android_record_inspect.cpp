#include <fcntl.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// Exercise the platform extractor, not only the host's FFmpeg parser. Android
// 8 needs sidx for nonzero seeks in fragmented MP4.
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    struct stat st{};
    if (fd < 0 || fstat(fd, &st) != 0 || st.st_size <= 0) return 3;
    AMediaExtractor* extractor = AMediaExtractor_new();
    if (!extractor || AMediaExtractor_setDataSourceFd(extractor, fd, 0, st.st_size) != AMEDIA_OK) return 4;
    size_t track = AMediaExtractor_getTrackCount(extractor);
    int64_t duration = 0;
    for (size_t i = 0; i < AMediaExtractor_getTrackCount(extractor); ++i) {
        AMediaFormat* format = AMediaExtractor_getTrackFormat(extractor, i);
        const char* mime = nullptr;
        if (format && AMediaFormat_getString(format, "mime", &mime) && mime && !std::strcmp(mime, "video/avc")) {
            track = i;
            (void)AMediaFormat_getInt64(format, "durationUs", &duration);
            AMediaFormat_delete(format);
            break;
        }
        if (format) AMediaFormat_delete(format);
    }
    if (track == AMediaExtractor_getTrackCount(extractor) || AMediaExtractor_selectTrack(extractor, track) != AMEDIA_OK)
        return 5;
    std::vector<uint8_t> buffer(16 * 1024 * 1024);
    size_t frames = 0;
    do {
        const ssize_t size = AMediaExtractor_readSampleData(extractor, buffer.data(), buffer.size());
        if (size < 0) break;
        ++frames;
    } while (AMediaExtractor_advance(extractor));
    if (frames < 2) { std::fprintf(stderr, "platform extractor read %zu samples\n", frames); return 6; }
    if (duration > 0) {
        const int64_t seek = duration * 3 / 4;
        if (AMediaExtractor_seekTo(extractor, seek, AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC) != AMEDIA_OK ||
            AMediaExtractor_readSampleData(extractor, buffer.data(), buffer.size()) < 0) return 7;
        const int64_t sample_time = AMediaExtractor_getSampleTime(extractor);
        if (duration >= 20000000 && sample_time <= 0) {
            std::fprintf(stderr, "seek returned %lld us for target %lld us (duration %lld us)\n",
                         static_cast<long long>(sample_time), static_cast<long long>(seek),
                         static_cast<long long>(duration));
            return 8;
        }
    }
    std::printf("platform extractor: %zu H.264 samples, duration=%lld us\n", frames, static_cast<long long>(duration));
    AMediaExtractor_delete(extractor);
    close(fd);
    return 0;
}
