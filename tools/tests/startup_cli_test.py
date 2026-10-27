#!/usr/bin/env python3
"""Exercise the real startup prompt through pipes and a pseudo-terminal."""
import os
import pty
import queue
import select
import subprocess
import sys
import termios
import threading
import time

PROMPT = b"Enter key:\n"

# The diagnostic CLI deliberately bypasses the gate; the ordinary path must
# still prompt before fork/setsid and before stdio is redirected to /dev/null.
with open("jni/src/main.cpp", encoding="utf-8") as source:
    main = source.read().split("int main(int argc, char* argv[])", 1)[1]
    assert main.index("--check-driver") < main.index("startup_key::Verify()")
    assert main.index("startup_key::Verify()") < main.index("stealth::DaemonizeEarly()")
    assert main.index("startup_key::Verify()") < main.index("stealth::ApplyRuntime()")


def read_prompt(fd, process):
    # Model a launcher that doesn't display stdout until it has a full line.
    # A PTY can translate LF to CRLF; a pipe must carry the exact LF form.
    data = b""
    deadline = time.monotonic() + 4
    while not data.endswith(b"\n"):
        ready, _, _ = select.select([fd], [], [], max(0, deadline - time.monotonic()))
        if not ready:
            process.kill()
            raise AssertionError(f"no complete prompt line before input: {data!r}")
        chunk = os.read(fd, 1)
        if not chunk:
            raise AssertionError(f"process exited before prompt: {data!r}")
        data += chunk
        assert len(data) <= len(PROMPT) + 1, data
    assert data.replace(b"\r\n", b"\n") == PROMPT, data


def piped(binary, supplied, expected_code, expected_error):
    p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    read_prompt(p.stdout.fileno(), p)
    out, err = p.communicate(supplied, timeout=5)
    assert out == b"", out
    assert p.returncode == expected_code, (p.returncode, err)
    assert expected_error in err, err


def line_buffered_launcher(binary):
    # A common launcher calls readline() and doesn't forward partial lines to
    # its UI. The child must still be waiting for input when this line arrives.
    p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    lines = queue.Queue()
    reader = threading.Thread(target=lambda: lines.put(p.stdout.readline()), daemon=True)
    reader.start()
    try:
        try:
            line = lines.get(timeout=4)
        except queue.Empty:
            raise AssertionError("line-buffered launcher could not display prompt") from None
        assert line == PROMPT and p.poll() is None, (line, p.returncode)
        p.stdin.write(b"t.me/benzware\n")
        p.stdin.flush()
        assert p.wait(timeout=5) == 0
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        reader.join(timeout=1)
        p.stdin.close()
        p.stdout.close()
        p.stderr.close()


def terminal(binary):
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)
    p = subprocess.Popen([binary], stdin=slave, stdout=slave, stderr=slave, close_fds=True)
    os.close(slave)
    try:
        read_prompt(master, p)
        os.write(master, b"t.me/benzware\n")
        assert p.wait(timeout=5) == 0
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        os.close(master)


for binary in sys.argv[1:]:
    piped(binary, b"t.me/benzware\n", 0, b"")
    piped(binary, b"t.me/benzware\r\n", 0, b"")
    piped(binary, b"wrong\n", 1, b"Invalid key")
    piped(binary, b"", 1, b"interactive terminal")
    line_buffered_launcher(binary)
    terminal(binary)
    print(f"startup prompt via pipe, line-buffered launcher and PTY: PASS ({os.path.basename(binary)})")
