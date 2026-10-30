#include "startup_key.h"

#include <cassert>
#include <sstream>
#include <string>
#include <unistd.h>

class FlushBuffer : public std::stringbuf {
public:
    int syncs = 0;
    int sync() override { ++syncs; return std::stringbuf::sync(); }
};

static void request(const std::string& text, startup_key::InputResult expected,
                    const char* error_text) {
    int fds[2];
    assert(pipe(fds) == 0);
    if (!text.empty())
        assert(write(fds[1], text.data(), text.size()) == static_cast<ssize_t>(text.size()));
    close(fds[1]);
    FlushBuffer buffer;
    std::ostream out(&buffer);
    std::ostringstream err;
    assert(startup_key::Request(fds[0], out, err) == expected);
    assert(buffer.str() == "Enter key:\n");
    assert(buffer.syncs > 0);
    assert(err.str().find(error_text) != std::string::npos);
    if (expected == startup_key::InputResult::Accepted) assert(err.str().empty());
    if (text == "wrong\n") assert(err.str().find("wrong") == std::string::npos);
    close(fds[0]);
}

int main() {
    using startup_key::InputResult;
    request("t.me/benzware\n", InputResult::Accepted, "");
    request("t.me/benzware\r\n", InputResult::Accepted, "");
    request("t.me/benzware", InputResult::Accepted, ""); // final line at EOF
    request("wrong\n", InputResult::Invalid, "Invalid key");
    request("t.me/benzwarex\n", InputResult::Invalid, "Invalid key");
    request(std::string(65, 'x'), InputResult::Invalid, "Invalid key");
    request("", InputResult::Closed, "interactive terminal");

    int fds[2];
    assert(pipe(fds) == 0);
    startup_key::FdReader reader;
    assert(reader.Poll(fds[0]) == InputResult::Waiting);
    assert(write(fds[1], "t.me/", 5) == 5);
    assert(reader.Poll(fds[0]) == InputResult::Waiting); // fragmented input
    assert(write(fds[1], "benzware\r\n", 10) == 10);
    assert(reader.Poll(fds[0]) == InputResult::Accepted);
    assert(reader.Poll(fds[0]) == InputResult::Accepted); // stable final result
    close(fds[0]); close(fds[1]);

    startup_key::FdReader negative_fd;
    assert(negative_fd.Poll(-1, 0) == InputResult::Closed);
    assert(pipe(fds) == 0);
    close(fds[0]); close(fds[1]);
    startup_key::FdReader bad_fd;
    assert(bad_fd.Poll(fds[0], 0) == InputResult::Closed);
}
