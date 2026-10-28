// Exercise the real config trailer codec without touching /data/local.
#include "../../jni/src/config.cpp"
#include <cassert>
#include <sstream>

AppState g_state;

int main() {
    static_assert(sizeof(CfgBlob) == 260, "legacy config layout changed");
    static_assert(sizeof(CfgExt2Trailer) == 256, "v2 extended trailer layout changed");
    const int bitrates[] = {0, 4, 8, 16};
    for (bool visible : {false, true}) for (int mbps : bitrates) {
        g_state.streamer_mode = visible;
        g_state.capture_video = {mbps};
        g_state.esp_pumpkins = true;
        g_state.esp_armor = true;
        g_state.viewmodel.weapon_skin_enabled = true;
        g_state.viewmodel.weapon_skin_index = 1;
        std::ostringstream output(std::ios::binary);
        WriteExt2Trailer(output);
        const std::string bytes = output.str();
        assert(bytes.size() == sizeof(CfgExt2Trailer));
        std::istringstream input(bytes, std::ios::binary);
        CfgExt2Trailer decoded{};
        assert(ReadExt2Trailer(input, decoded));
        assert(overlay_capture::visible_from_config(decoded.flags) == visible);
        assert((decoded.flags & (1u << 15)) && (decoded.flags & (1u << 16)));
        assert(decoded.version == 2 && (decoded.flags & (1u << 22)));
        assert(((decoded.flags & (1u << 17)) == 0) == visible);
        assert(decoded.weapon_skin_index == 1);
        assert((decoded.flags & screen_capture::kLegacyDurationMask) == 0);
        g_state.streamer_mode = !visible;
        g_state.capture_video = {-1};
        ApplyCaptureConfig(decoded.flags);
        assert(g_state.streamer_mode == visible && g_state.capture_video.bitrate_mbps == mbps);

        // Simulate all duration values written by the previous release: the
        // real trailer reader keeps the bitrate and ignores the old limit.
        for (uint32_t old_limit = 0; old_limit < 4; ++old_limit) {
            CfgExt2Trailer old = decoded;
            old.flags |= old_limit << 18;
            std::string encrypted(reinterpret_cast<const char*>(&old), sizeof(old));
            XorBuf(reinterpret_cast<uint8_t*>(encrypted.data()), encrypted.size());
            std::istringstream old_input(encrypted, std::ios::binary);
            CfgExt2Trailer old_decoded{};
            assert(ReadExt2Trailer(old_input, old_decoded));
            g_state.streamer_mode = !visible;
            g_state.capture_video = {-1};
            ApplyCaptureConfig(old_decoded.flags);
            assert(g_state.streamer_mode == visible && g_state.capture_video.bitrate_mbps == mbps);
        }

        // v1 was 252 bytes (with pumpkin color), before the skin index existed.
        CfgExt2Trailer old_v1 = decoded;
        old_v1.version = 1;
        old_v1.flags &= ~(1u << 22);
        std::string old_v1_bytes(reinterpret_cast<const char*>(&old_v1), sizeof(old_v1) - sizeof(int32_t));
        XorBuf(reinterpret_cast<uint8_t*>(old_v1_bytes.data()), old_v1_bytes.size());
        std::istringstream legacy(old_v1_bytes, std::ios::binary);
        CfgExt2Trailer decoded_legacy{};
        assert(ReadExt2Trailer(legacy, decoded_legacy));
        assert(decoded_legacy.version == 1 && decoded_legacy.weapon_skin_index == -1);
        assert(overlay_capture::visible_from_config(decoded_legacy.flags) == visible);
        assert((decoded_legacy.flags & (1u << 16)) != 0);
        ApplyCaptureConfig(decoded_legacy.flags);
        assert(g_state.capture_video.bitrate_mbps == mbps);

        // Also accept the older v1 variant that predates the pumpkin color.
        std::string pre_pumpkin(old_v1_bytes.data(), old_v1_bytes.size() - sizeof(ImVec4));
        std::istringstream legacy_pre_pumpkin(pre_pumpkin, std::ios::binary);
        CfgExt2Trailer decoded_pre_pumpkin{};
        assert(ReadExt2Trailer(legacy_pre_pumpkin, decoded_pre_pumpkin));
        assert(decoded_pre_pumpkin.version == 1 && decoded_pre_pumpkin.weapon_skin_index == -1);
        std::istringstream truncated(bytes.substr(0, 12), std::ios::binary);
        CfgExt2Trailer ignored{};
        assert(!ReadExt2Trailer(truncated, ignored));
    }
    // A profile predating the capture trailer restores old defaults even when
    // loaded over a profile with different settings.
    g_state.streamer_mode = false;
    g_state.capture_video = {16};
    ApplyCaptureConfig(0);
    assert(g_state.streamer_mode && g_state.capture_video.bitrate_mbps == 0);
    std::puts("PASS: capture config round-trips, legacy duration ignored, unchanged layout");
}
