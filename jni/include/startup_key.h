#pragma once

#include <iostream>
#include <string>

namespace startup_key {
// This public string is only a launch prompt, not a security boundary.
inline constexpr char kLaunchKey[] = "t.me/benzware";

// Keep the prompt before any daemonization and flush it before waiting for
// input. MT Manager/adb may supply either a terminal or a pair of pipes.
inline bool Verify(std::istream& input = std::cin,
                   std::ostream& output = std::cout,
                   std::ostream& error = std::cerr) {
    output << "Enter key: " << std::flush;
    std::string entered;
    if (!std::getline(input, entered)) {
        error << "Unable to read key. Run the binary in an interactive terminal.\n";
        return false;
    }
    // Terminal and pipe launchers may send CRLF rather than LF.
    if (!entered.empty() && entered.back() == '\r') entered.pop_back();
    if (entered != kLaunchKey) {
        error << "Invalid key. Launch cancelled.\n";
        return false;
    }
    return true;
}
} // namespace startup_key
