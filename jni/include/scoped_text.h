#pragma once

#include "bw_obf.h"
#include <cstddef>
#include <cstdint>

// This is literal obfuscation, not a secret-key security boundary. Use only
// with APIs that consume the text during the call and do not retain a pointer.
namespace bw {
inline void wipe_bytes(void* memory, std::size_t size) noexcept {
    volatile unsigned char* bytes = static_cast<volatile unsigned char*>(memory);
    while (size--) *bytes++ = 0;
}

template<std::size_t N, std::uint64_t Salt>
class EncodedText {
    unsigned char bytes_[N]{};
public:
    static constexpr std::size_t size = N;
    static constexpr unsigned char mask(std::size_t i) noexcept {
        return static_cast<unsigned char>(mix64(kKey ^ Salt ^ i));
    }
    constexpr explicit EncodedText(const char (&text)[N]) noexcept {
        for (std::size_t i = 0; i < N; ++i)
            bytes_[i] = static_cast<unsigned char>(text[i]) ^ mask(i);
    }
    __attribute__((noinline)) void decode(char* destination) const noexcept {
        // A volatile source prevents constant-folding back into a plaintext
        // literal even when the surrounding application uses LTO.
        const volatile unsigned char* source = bytes_;
        for (std::size_t i = 0; i < N; ++i)
            destination[i] = static_cast<char>(source[i] ^ mask(i));
    }
};

template<std::size_t N>
class ScopedText {
    char bytes_[N]{};
public:
    template<std::uint64_t Salt>
    explicit ScopedText(const EncodedText<N, Salt>& encoded) noexcept {
        encoded.decode(bytes_);
    }
    ScopedText(const ScopedText&) = delete;
    ScopedText& operator=(const ScopedText&) = delete;
    ScopedText(ScopedText&&) = delete;
    ScopedText& operator=(ScopedText&&) = delete;
    ~ScopedText() noexcept { wipe_bytes(bytes_, sizeof(bytes_)); }
    const char* c_str() const noexcept { return bytes_; }
};

template<std::uint64_t Salt, std::size_t N>
constexpr auto encode_text(const char (&text)[N]) noexcept {
    return EncodedText<N, Salt>(text);
}
} // namespace bw

// Keep the owner alive for the entire API call. Never assign the c_str() of
// this temporary to a pointer that outlives the full expression.
#define BW_TEXT(text) ([]() noexcept { \
    static constexpr auto encoded = ::bw::encode_text< \
        static_cast<std::uint64_t>(__LINE__) * 0x9E3779B97F4A7C15ULL ^ \
        static_cast<std::uint64_t>(__COUNTER__)>(text); \
    return ::bw::ScopedText<sizeof(text)>(encoded); \
}())
