#pragma once

#include <iostream>
#include <string>

namespace startup_key {
// This public string is only a launch prompt, not a security boundary.
inline constexpr char kLaunchKey[] = "t.me/benzware";

// Keep the prompt before daemonization and terminate the line before waiting:
// some Android launchers display stdout with a line reader, so a flushed prompt
// without a newline remains invisible even though it reached the pipe.
inline bool Verify(std::istream& input = std::cin,
                   std::ostream& output = std::cout,
                   std::ostream& error = std::cerr) {
    output << "Enter key:\n" << std::flush;
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
