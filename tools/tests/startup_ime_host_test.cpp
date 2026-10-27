// Exercise the actual bridge's file/session/protocol and subprocess code on host.
#include "../../jni/src/startup_ime.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    std::string a, b;
    assert(startup_ime::RandomSession(a) && startup_ime::RandomSession(b));
    assert(a.size() == 32 && b.size() == 32 && a != b);
    assert(a.find_first_not_of("0123456789abcdef") == std::string::npos);
    using startup_ime::ParseResponse;
    using startup_ime::Response;
    assert(ParseResponse("Tt.me/benzware") == Response::Accepted);
    assert(ParseResponse("C") == Response::Cancelled);
    for (const std::string& s : {std::string("Twrong"), std::string("Cjunk"),
                                  std::string("T"), std::string(""), std::string(130, 'x')})
        assert(ParseResponse(s) == Response::Invalid);
    std::string base = argv[1];
    const std::string apk = base + "/helper.apk", link = base + "/link";
    assert(startup_ime::WriteApk(apk));
    struct stat apk_stat{};
    assert(stat(apk.c_str(), &apk_stat) == 0 && (apk_stat.st_mode & 0777) == 0644);
    assert(!startup_ime::WriteApk(apk)); // exclusive creation
    assert(symlink(apk.c_str(), link.c_str()) == 0);
    std::string read;
    assert(startup_ime::ReadFile(apk, read) && read == "test-apk");
    assert(!startup_ime::ReadFile(link, read));
    { std::ofstream file(apk); file << std::string(129, 'x'); }
    assert(!startup_ime::ReadFile(apk, read));
    // The production stealth setup ignores SIGCHLD; the bridge temporarily
    // restores waitable children and must restore SIG_IGN when it is done.
    signal(SIGCHLD, SIG_IGN);
    {
        startup_ime::ChildSignals child_signals;
        assert(child_signals.ok);
        auto under_ignored = startup_ime::Run({"/bin/echo", "waited"}, 2);
        assert(under_ignored.ok && under_ignored.output == "waited\n");
    }
    struct sigaction sig{};
    assert(sigaction(SIGCHLD, nullptr, &sig) == 0 && sig.sa_handler == SIG_IGN);
    signal(SIGCHLD, SIG_DFL);
    auto command = startup_ime::Run({"/bin/echo", "hello"}, 2);
    assert(command.ok && command.output == "hello\n");
    const std::string bare_script = base + "/no-shebang";
    { std::ofstream file(bare_script); file << "printf 'bare script: %s\\n' \"$1\"\n"; }
    assert(chmod(bare_script.c_str(), 0755) == 0);
    command = startup_ime::Run({bare_script, "hello"}, 2);
    assert(command.ok && command.output == "bare script: hello\n");
    command = startup_ime::Run({"/bin/sh", "-c", "exit 9"}, 2);
    assert(!command.ok);
    command = startup_ime::Run({"/bin/sleep", "2"}, 1);
    assert(!command.ok && command.output.find("timed out") != std::string::npos);
    std::puts("startup IME bridge host tests: PASS");
}
