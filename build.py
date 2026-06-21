#!/usr/bin/env python3
"""Build, test, bench, and clean SILO with zero fuss.

Usage:
  python3 build.py          # build release binary
  python3 build.py debug    # build debug binary with sanitizers
  python3 build.py test     # run tests
  python3 build.py bench    # run benchmarks
  python3 build.py clean    # remove build artifacts
  python3 build.py all      # clean + test + release
"""

import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))


def run(cmd, **kwargs):
    print(f"+ {' '.join(cmd)}")
    subprocess.check_call(cmd, cwd=ROOT, **kwargs)


def get_libasan():
    try:
        return subprocess.run(
            ["g++", "-print-file-name=libasan.so"],
            capture_output=True, text=True, check=True
        ).stdout.strip()
    except subprocess.CalledProcessError:
        return ""


def build_release():
    run(["make", "-j", "release"])


def build_debug():
    run(["make", "-j", "debug"])


def test():
    run(["make", "-j", "test/runner"])
    libasan = get_libasan()
    env = {**os.environ}
    if libasan:
        env["LD_PRELOAD"] = libasan
    run(["./test/runner"], env=env)


def bench():
    run(["make", "-j", "bench/runner"])
    run(["./bench/runner"])


def clean():
    run(["make", "clean"])


def all():
    clean()
    test()
    build_release()
    print("\n✓ Release binary ready: ./silo")


def main():
    if len(sys.argv) < 2:
        build_release()
        return

    task = sys.argv[1]
    table = {
        "release": build_release,
        "debug": build_debug,
        "test": test,
        "bench": bench,
        "clean": clean,
        "all": all,
    }
    fn = table.get(task)
    if not fn:
        print(f"Unknown task: {task}", file=sys.stderr)
        print("Usage: python3 build.py [release|debug|test|bench|clean|all]", file=sys.stderr)
        sys.exit(1)
    fn()


if __name__ == "__main__":
    main()
