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

static void valid(const char* text) {
    std::istringstream in(text);
    FlushBuffer buffer;
    std::ostream out(&buffer);
    std::ostringstream err;
    assert(startup_key::Verify(in, out, err));
    assert(buffer.str() == "Enter key:\n");
    assert(buffer.syncs > 0);
    assert(err.str().empty());
}

int main() {
    valid("t.me/benzware\n");
    valid("t.me/benzware\r\n");
    valid("t.me/benzware"); // a complete final line need not end in LF

    std::istringstream wrong("wrong\n");
    std::ostringstream out, err;
    assert(!startup_key::Verify(wrong, out, err));
    assert(out.str() == "Enter key:\n");
    assert(err.str().find("Invalid key") != std::string::npos);
    assert(err.str().find("wrong") == std::string::npos);

    std::istringstream empty("");
    std::ostringstream out2, err2;
    assert(!startup_key::Verify(empty, out2, err2));
    assert(out2.str() == "Enter key:\n");
    assert(err2.str().find("interactive terminal") != std::string::npos);

    using startup_key::InputResult;
    int fds[2];
    assert(pipe(fds) == 0);
    startup_key::FdReader reader;
    assert(reader.Poll(fds[0]) == InputResult::Waiting); // open but no input
    assert(write(fds[1], "t.me/", 5) == 5);
    assert(reader.Poll(fds[0]) == InputResult::Waiting); // fragmented input
    assert(write(fds[1], "benzware\r\n", 10) == 10);
    assert(reader.Poll(fds[0]) == InputResult::Accepted);
    assert(reader.Poll(fds[0]) == InputResult::Accepted);
    close(fds[0]); close(fds[1]);

    assert(pipe(fds) == 0);
    close(fds[1]);
    startup_key::FdReader closed;
    assert(closed.Poll(fds[0]) == InputResult::Closed); // GUI fallback, not success
    close(fds[0]);

    assert(pipe(fds) == 0);
    assert(write(fds[1], "t.me/benzware", 13) == 13);
    close(fds[1]);
    startup_key::FdReader eof_line;
    assert(eof_line.Poll(fds[0]) == InputResult::Accepted);
    close(fds[0]);

    assert(pipe(fds) == 0);
    assert(write(fds[1], "wrong\n", 6) == 6);
    startup_key::FdReader incorrect;
    assert(incorrect.Poll(fds[0]) == InputResult::Invalid);
    close(fds[0]); close(fds[1]);

    assert(pipe(fds) == 0);
    std::string long_line(65, 'x');
    assert(write(fds[1], long_line.data(), long_line.size()) == (ssize_t)long_line.size());
    startup_key::FdReader oversized;
    assert(oversized.Poll(fds[0]) == InputResult::Invalid);
    close(fds[0]); close(fds[1]);

    startup_key::Entry entry;
    entry.Add('x');
    assert(!entry.Submit() && entry.invalid && entry.text.empty());
    for (const char c : std::string("t.me/benzwarex")) entry.Add(c);
    entry.Backspace();
    assert(entry.Submit() && entry.text.empty() && !entry.invalid);
    for (int i = 0; i < 100; ++i) entry.Add('a');
    assert(entry.text.size() == startup_key::kMaxEntry);
}
