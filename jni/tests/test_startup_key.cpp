#include "startup_key.h"

#include <cassert>
#include <sstream>
#include <string>

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
    assert(buffer.str() == "Enter key: ");
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
    assert(out.str() == "Enter key: ");
    assert(err.str().find("Invalid key") != std::string::npos);
    assert(err.str().find("wrong") == std::string::npos);

    std::istringstream empty("");
    std::ostringstream out2, err2;
    assert(!startup_key::Verify(empty, out2, err2));
    assert(out2.str() == "Enter key: ");
    assert(err2.str().find("interactive terminal") != std::string::npos);
}
