// Exercise the real config trailer codec without touching /data/local.
#include "../../jni/src/config.cpp"
#include <cassert>
#include <sstream>

AppState g_state;

int main() {
    static_assert(sizeof(CfgBlob) == 260, "legacy config layout changed");
    static_assert(sizeof(CfgExt2Trailer) == 252, "extended trailer layout changed");
    const int durations[] = {60, 120, 180};
    const int bitrates[] = {0, 4, 8, 16};
    for (bool visible : {false, true}) for (int seconds : durations) for (int mbps : bitrates) {
        g_state.streamer_mode = visible;
        g_state.capture_video = {seconds, mbps};
        g_state.esp_pumpkins = true;
        g_state.esp_armor = true;
        std::ostringstream output(std::ios::binary);
        WriteExt2Trailer(output);
        const std::string bytes = output.str();
        assert(bytes.size() == sizeof(CfgExt2Trailer));
        std::istringstream input(bytes, std::ios::binary);
        CfgExt2Trailer decoded{};
        assert(ReadExt2Trailer(input, decoded));
        assert(overlay_capture::visible_from_config(decoded.flags) == visible);
        assert((decoded.flags & (1u << 15)) && (decoded.flags & (1u << 16)));
        g_state.streamer_mode = !visible;
        g_state.capture_video = {-1, -1};
        ApplyCaptureConfig(decoded.flags);
        assert(g_state.streamer_mode == visible);
        assert(g_state.capture_video.duration_seconds == seconds && g_state.capture_video.bitrate_mbps == mbps);

        // The older shorter trailer is still accepted, and corrupt/truncated
        // trailers are still rejected without changing the fixed file layout.
        std::istringstream legacy(bytes.substr(0, bytes.size() - sizeof(ImVec4)), std::ios::binary);
        CfgExt2Trailer decoded_legacy{};
        assert(ReadExt2Trailer(legacy, decoded_legacy));
        assert(overlay_capture::visible_from_config(decoded_legacy.flags) == visible);
        assert((decoded_legacy.flags & (1u << 16)) != 0);
        g_state.capture_video = {-1, -1};
        ApplyCaptureConfig(decoded_legacy.flags);
        assert(g_state.capture_video.duration_seconds == seconds && g_state.capture_video.bitrate_mbps == mbps);
        std::istringstream truncated(bytes.substr(0, 12), std::ios::binary);
        CfgExt2Trailer ignored{};
        assert(!ReadExt2Trailer(truncated, ignored));
    }
    // Existing files have neither capture-video bits nor, in older versions,
    // even an extended trailer. Loading over changed settings restores defaults.
    g_state.streamer_mode = false;
    g_state.capture_video = {60, 16};
    ApplyCaptureConfig(0);
    assert(g_state.streamer_mode);
    assert(g_state.capture_video.duration_seconds == 180 && g_state.capture_video.bitrate_mbps == 0);
    std::puts("PASS: config trailer preset round-trips, old/truncated layouts, unchanged flag bits");
}
