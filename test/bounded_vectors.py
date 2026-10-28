"""Independent canonical byte vectors from the MessagePack specification."""

from __future__ import annotations

import subprocess
import sys


def main() -> None:
    expected = [
        "00",
        "7f",
        "cc80",
        "ccff",
        "cd0100",
        "cdffff",
        "ce00010000",
        "ceffffffff",
        "cf0000000100000000",
        "cfffffffffffffffff",
        "ff",
        "e0",
        "d0df",
        "d080",
        "d1ff7f",
        "d18000",
        "d2ffff7fff",
        "d280000000",
        "d3ffffffff7fffffff",
        "d38000000000000000",
    ]
    for size, header in [
        (0, "a0"),
        (1, "a1"),
        (31, "bf"),
        (32, "d920"),
        (255, "d9ff"),
        (256, "da0100"),
        (16384, "da4000"),
    ]:
        expected.append(header + "78" * size)
    expected.extend(["90", "9f", "dc0010", "dcffff", "dd00010000", "dd0007a120"])
    expected.append("c410" + "00" * 16)
    process = subprocess.run(
        [sys.argv[1], "--vectors"], check=True, capture_output=True, text=True
    )
    actual = [
        line.removeprefix("hex ")
        for line in process.stdout.splitlines()
        if line.startswith("hex ")
    ]
    if actual != expected:
        raise AssertionError(
            "C++ encodings differ from independent MessagePack vectors"
        )
    print(f"{len(expected)} independent MessagePack vectors passed")


if __name__ == "__main__":
    main()
