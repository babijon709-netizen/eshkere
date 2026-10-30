#pragma once

#include <cerrno>
#include <iostream>
#include <poll.h>
#include <string>
#include <unistd.h>

namespace startup_key {
// This public string is only a launch prompt, not a security boundary.
inline constexpr char kLaunchKey[] = "t.me/benzware";
inline constexpr std::size_t kMaxEntry = 64;

enum class InputResult { Waiting, Closed, Accepted, Invalid };

inline void PrintPrompt(std::ostream& output = std::cout) {
    output << "Enter key:\n" << std::flush;
}

inline bool Matches(std::string entered) {
    // Terminal and pipe launchers may send CRLF rather than LF.
    if (!entered.empty() && entered.back() == '\r') entered.pop_back();
    return entered == kLaunchKey;
}

// Read from the launcher's actual stdin. Wait indefinitely rather than
// detaching or switching UIs while MT Manager is still providing a PTY.
// Poll is also usable for nonblocking descriptors and fragmented pipe input.
class FdReader {
public:
    InputResult Poll(int fd, int timeout_ms = 0) {
        if (finished_) return result_;
        if (fd < 0) return Finish(InputResult::Closed);
        for (std::size_t i = 0; i <= kMaxEntry; ++i) {
            pollfd pfd{fd, static_cast<short>(POLLIN | POLLHUP), 0};
            const int ready = ::poll(&pfd, 1, i == 0 ? timeout_ms : 0);
            if (ready == 0 || (ready < 0 && errno == EINTR)) return InputResult::Waiting;
            if (ready < 0 || (pfd.revents & POLLNVAL)) return Finish(InputResult::Closed);
            if (!(pfd.revents & (POLLIN | POLLHUP | POLLERR))) return InputResult::Waiting;
            char c = 0;
            const ssize_t n = ::read(fd, &c, 1);
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) return InputResult::Waiting;
            if (n <= 0) {
                return Finish(entered_.empty() ? InputResult::Closed
                                               : (Matches(entered_) ? InputResult::Accepted
                                                                    : InputResult::Invalid));
            }
            if (c == '\n') return Finish(Matches(entered_) ? InputResult::Accepted
                                                           : InputResult::Invalid);
            if (entered_.size() >= kMaxEntry) return Finish(InputResult::Invalid);
            entered_.push_back(c);
        }
        return InputResult::Waiting;
    }

private:
    InputResult Finish(InputResult result) {
        result_ = result;
        finished_ = true;
        entered_.clear();
        return result;
    }
    std::string entered_;
    InputResult result_ = InputResult::Waiting;
    bool finished_ = false;
};

// The launcher must be able to display stdout and supply stdin. In particular,
// a terminal-less runner that buffers output until process exit cannot be made
// interactive by the child process alone.
inline InputResult Request(int fd = STDIN_FILENO,
                           std::ostream& output = std::cout,
                           std::ostream& error = std::cerr) {
    PrintPrompt(output);
    FdReader reader;
    InputResult result;
    do { result = reader.Poll(fd, -1); }
    while (result == InputResult::Waiting);
    if (result == InputResult::Invalid)
        error << "Invalid key. Launch cancelled.\n";
    else if (result == InputResult::Closed)
        error << "Unable to read key. Run in an interactive terminal.\n";
    return result;
}
} // namespace startup_key
