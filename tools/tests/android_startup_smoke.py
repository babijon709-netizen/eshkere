#!/usr/bin/env python3
"""Android/Bionic pipe and PTY checks, independent of MT Manager."""
import os
import select
import subprocess
import time

remote = "/data/local/tmp/benzware-startup-test"


def run(*args, check=True, input=None, timeout=30):
    return subprocess.run(args, input=input, capture_output=True, check=check, timeout=timeout)


def wait_text(stream, wanted, timeout=10):
    data = b""
    deadline = time.monotonic() + timeout
    while wanted not in data:
        ready, _, _ = select.select([stream], [], [], max(0, deadline - time.monotonic()))
        if not ready:
            raise AssertionError(f"timed out waiting for {wanted!r}; got {data!r}")
        part = os.read(stream.fileno(), 1)
        if not part:
            raise AssertionError(f"EOF waiting for {wanted!r}; got {data!r}")
        data += part
    return data


def exercise(name):
    binary = f"{remote}/{name}"
    for data in (b"t.me/benzware\n", b"t.me/benzware\r\n", b"t.me/benzware"):
        good = run("adb", "shell", "-T", binary, input=data, check=False)
        assert good.returncode == 0 and b"Enter key:\nKEY_ACCEPTED\n" in good.stdout, (good.returncode, good.stdout, good.stderr)
    wrong = run("adb", "shell", "-T", binary, input=b"wrong\n", check=False)
    assert wrong.returncode == 1 and b"Invalid key" in wrong.stderr, (wrong.returncode, wrong.stdout, wrong.stderr)
    closed = run("adb", "shell", "-T", f"{binary} </dev/null", check=False)
    assert closed.returncode == 1 and b"interactive terminal" in closed.stderr, (closed.returncode, closed.stdout, closed.stderr)

    # A line-buffered consumer must see a full prompt while the process is
    # still attached and waiting, even well past the former 100ms probe.
    p = subprocess.Popen(["adb", "shell", "-T", binary], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        assert wait_text(p.stdout, b"Enter key:\n") == b"Enter key:\n"
        time.sleep(0.5)
        assert p.poll() is None
        p.stdin.write(b"t.me/benzware\n"); p.stdin.flush()
        assert p.wait(timeout=15) == 0
        assert b"KEY_ACCEPTED" in p.stdout.read()
    finally:
        if p.poll() is None:
            p.kill(); p.wait()
        for stream in (p.stdin, p.stdout, p.stderr):
            stream.close()

    # A real controlling Android shell PTY, with delayed user input.
    p = subprocess.Popen(["adb", "shell", "-tt", binary], stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    try:
        assert b"Enter key:" in wait_text(p.stdout, b"Enter key:")
        time.sleep(0.5)
        assert p.poll() is None
        p.stdin.write(b"t.me/benzware\n"); p.stdin.flush()
        assert p.wait(timeout=15) == 0
        assert b"KEY_ACCEPTED" in p.stdout.read()
    finally:
        if p.poll() is None:
            p.kill(); p.wait()
        p.stdin.close(); p.stdout.close()
    print(f"android startup pipes and attached PTY: PASS ({name})")


def main():
    run("adb", "root", check=False)
    run("adb", "wait-for-device")
    run("adb", "shell", "mkdir", "-p", remote)
    try:
        for env, name in (("ANDROID_STARTUP_PLAIN", "startup-plain"),
                          ("ANDROID_STARTUP_ENCRYPTED", "startup-encrypted")):
            run("adb", "push", os.environ[env], f"{remote}/{name}")
            run("adb", "shell", "chmod", "0755", f"{remote}/{name}")
            exercise(name)
    finally:
        run("adb", "shell", "rm", "-rf", remote, check=False)


if __name__ == "__main__":
    main()
