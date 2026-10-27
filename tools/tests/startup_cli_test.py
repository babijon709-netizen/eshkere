#!/usr/bin/env python3
"""Exercise the production startup gate through pipes and a controlling PTY."""
import fcntl
import os
import pty
import queue
import select
import signal
import subprocess
import sys
import termios
import threading
import time

PROMPT = b"Enter key:\n"
ACCEPTED = b"KEY_ACCEPTED\n"

# Diagnostics bypass the gate. No daemonization, touch, graphics, descriptor
# redirection, or attachment may occur until Request() has returned success.
with open("jni/src/main.cpp", encoding="utf-8") as source:
    main = source.read().split("int main(int argc, char* argv[])", 1)[1]
    gate = main.index("startup_key::Request()")
    assert main.index("--check-driver") < gate
    for action in ("stealth::DaemonizeEarly()", "stealth::ApplyEarly(",
                   "initGUI_draw(", "Touch_Init(", "stealth::ApplyRuntime()",
                   "start_attach_thread()"):
        assert gate < main.index(action), action
    assert "startup_ime::" not in main and "startup_key_ui::" not in main


def read_prompt(fd, process):
    # The complete line must reach the launcher before any key is supplied.
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


def piped(binary, supplied, expected_code, expected_out, expected_error):
    p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        read_prompt(p.stdout.fileno(), p)
        out, err = p.communicate(supplied, timeout=5)
        assert out == expected_out, out
        assert p.returncode == expected_code, (p.returncode, err)
        assert expected_error in err, err
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()


def line_buffered_launcher(binary):
    # A launcher consuming stdout via readline must see the prompt while the
    # input stream is still open. Waiting longer than the old 100ms probe must
    # not fork/detach or force a separate input window.
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
        time.sleep(0.5)
        assert p.poll() is None
        os.kill(p.pid, signal.SIGUSR1)  # interrupt a pending poll/read
        p.stdin.write(b"t.me/")
        p.stdin.flush()
        time.sleep(0.1)
        assert p.poll() is None
        p.stdin.write(b"benzware\n")
        p.stdin.flush()
        out, err = p.communicate(timeout=5)
        assert p.returncode == 0 and out == ACCEPTED and not err, (p.returncode, out, err)
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        reader.join(timeout=1)
        for stream in (p.stdin, p.stdout, p.stderr):
            if not stream.closed:
                stream.close()


def terminal(binary):
    master, slave = pty.openpty()
    attrs = termios.tcgetattr(slave)
    attrs[3] &= ~termios.ECHO
    termios.tcsetattr(slave, termios.TCSANOW, attrs)

    def controlling_terminal():
        os.setsid()
        fcntl.ioctl(0, termios.TIOCSCTTY, 0)

    p = subprocess.Popen([binary], stdin=slave, stdout=slave, stderr=slave,
                         close_fds=True, preexec_fn=controlling_terminal)
    os.close(slave)
    try:
        read_prompt(master, p)
        time.sleep(0.5)
        assert p.poll() is None
        os.write(master, b"t.me/benzware\n")
        assert p.wait(timeout=5) == 0
        assert ACCEPTED.rstrip() in os.read(master, 128).replace(b"\r\n", b"\n")
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        os.close(master)


def cancel_while_waiting(binary):
    p = subprocess.Popen([binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        read_prompt(p.stdout.fileno(), p)
        p.terminate()
        assert p.wait(timeout=5) == -signal.SIGTERM
    finally:
        if p.poll() is None:
            p.kill()
            p.wait()
        for stream in (p.stdin, p.stdout, p.stderr):
            stream.close()


for binary in sys.argv[1:]:
    piped(binary, b"t.me/benzware\n", 0, ACCEPTED, b"")
    piped(binary, b"t.me/benzware\r\n", 0, ACCEPTED, b"")
    piped(binary, b"t.me/benzware", 0, ACCEPTED, b"")
    piped(binary, b"wrong\n", 1, b"", b"Invalid key")
    piped(binary, b"t.me/benzwarex\n", 1, b"", b"Invalid key")
    piped(binary, b"x" * 65, 1, b"", b"Invalid key")
    piped(binary, b"", 1, b"", b"interactive terminal")
    line_buffered_launcher(binary)
    terminal(binary)
    cancel_while_waiting(binary)
    print(f"startup gate via pipe, buffered launcher and PTY: PASS ({os.path.basename(binary)})")
