#include "scoped_text.h"
#include <cassert>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

int main() {
    constexpr auto encoded = bw::encode_text<123>("ephemeral");
    using Text = bw::ScopedText<sizeof("ephemeral")>;
    alignas(Text) unsigned char storage[sizeof(Text)]{};
    Text* text = new (storage) Text(encoded);
    assert(std::strcmp(text->c_str(), "ephemeral") == 0);
    text->~Text();
    for (unsigned char byte : storage) assert(byte == 0);
    auto one = BW_TEXT("one");
    auto two = BW_TEXT("two");
    assert(std::strcmp(one.c_str(), "one") == 0);
    assert(std::strcmp(two.c_str(), "two") == 0);
    auto empty = BW_TEXT("");
    assert(empty.c_str()[0] == '\0');
    auto utf8 = BW_TEXT("Проверка");
    assert(std::strcmp(utf8.c_str(), "Проверка") == 0);
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([] {
        for (int j = 0; j < 1000; ++j) {
            auto value = BW_TEXT("thread-local storage");
            assert(std::strcmp(value.c_str(), "thread-local storage") == 0);
        }
    });
    for (auto& thread : threads) thread.join();
}
