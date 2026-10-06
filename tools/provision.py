#!/usr/bin/env python3
"""Build the provisioning image for one unit, which writes its device serial number.

The provisioning firmware (XBEE_APP_PROVISION) configures the XBee module and,
once that has passed, writes the device serial number YYWW-NNNNN-C to MCU NVM3
(src/app/inc/sn_burner.h). Every image carries one serial number, so it is
built per unit:

    python tools/provision.py --sequence 00123

- NNNNN is the --sequence argument.
- YYWW is the ISO week-numbering year and ISO week of today's date on this
  machine, which is the build date of the image.
- C is the ISO 7064 MOD 11-10 check digit over YYWWNNNNN.

The image is built in its own directory (cmake_gcc/build-provision by default),
so the normal console build in cmake_gcc/build is left alone, and a copy named
after the serial number is put in its units/ folder.

After building, the script asks whether to flash the image and watch the
board's log. Answering yes flashes it with Simplicity Commander (no mass erase,
so the rest of the NVM3 region is kept), reads the VCOM log at 115200 baud and
waits for the firmware's result line. That step needs --port and pyserial
(python -m pip install -r tools/requirements.txt).

Exit codes:
    0  built; and if flashed, the board wrote the expected serial number
    1  bad arguments, or a tool or file this script needs is missing
    2  configuring or building failed
    3  flashing failed
    4  the board reported that the serial number was not written
    5  the board wrote a different serial number than this script expected
    6  timed out waiting for the board's result
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import List, Optional, Sequence

REPO_ROOT = Path(__file__).resolve().parent.parent
CMAKE_DIR = REPO_ROOT / "cmake_gcc"
DEFAULT_BUILD_DIR = CMAKE_DIR / "build-provision"
SLT_INSTALLS = Path.home() / ".silabs" / "slt" / "installs"

CONFIGURE_PRESET = "project"
BUILD_CONFIG = "base"
TARGET = "xbee_provision"
DEVICE = "EFM32PG28B210F1024IM68"
LOG_BAUD = 115200
DEFAULT_TIMEOUT_S = 180.0

# Years a two-digit YY can stand for, as in src/utils/inc/device_sn.h.
YEAR_MIN = 2000
YEAR_MAX = 2099

SEQUENCE_RE = re.compile(r"\d{5}")
# The firmware's terminal lines (src/app/src/sn_burner.c). "not written" is
# matched first; it does not contain "serial number written" anyway.
NOT_WRITTEN_RE = re.compile(r"serial number not written: (.*)")
WRITTEN_RE = re.compile(r"serial number written: (\d{4}-\d{5}-\d)")
# The log colours its lines with ANSI escape sequences (app_log_config.h).
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")

EXIT_OK = 0
EXIT_USAGE = 1
EXIT_BUILD = 2
EXIT_FLASH = 3
EXIT_NOT_WRITTEN = 4
EXIT_MISMATCH = 5
EXIT_TIMEOUT = 6


def check_digit(digits: str) -> int:
    """Return the ISO 7064 MOD 11-10 check digit, as device_sn.c computes it."""
    product = 10
    for ch in digits:
        total = (product + int(ch)) % 10
        if total == 0:
            total = 10
        product = (2 * total) % 11
    return (11 - product) % 10


def build_yyww(day: datetime.date) -> str:
    """Return YYWW for a date: ISO week-numbering year and ISO week."""
    iso_year, iso_week, _ = day.isocalendar()
    if not YEAR_MIN <= iso_year <= YEAR_MAX:
        raise ValueError(f"ISO year {iso_year} is outside {YEAR_MIN} to {YEAR_MAX}")
    return f"{iso_year % 100:02d}{iso_week:02d}"


def format_sn(yyww: str, sequence: str) -> str:
    """Return the serial number "YYWW-NNNNN-C"."""
    return f"{yyww}-{sequence}-{check_digit(yyww + sequence)}"


def fail(message: str, code: int) -> int:
    """Print an error and return the exit code to use."""
    print(f"error: {message}", file=sys.stderr)
    return code


def run(command: Sequence[str], cwd: Path) -> int:
    """Run a command, echoing it first, with its output going to this console."""
    print("$ " + " ".join(f'"{part}"' if " " in part else part for part in command))
    sys.stdout.flush()
    return subprocess.run(list(command), cwd=str(cwd), check=False).returncode


def find_cmake(explicit: Optional[str]) -> Optional[Path]:
    """Find cmake: --cmake, then PATH, then the one Studio's build uses."""
    if explicit:
        path = Path(explicit)
        return path if path.is_file() else None

    on_path = shutil.which("cmake")
    if on_path:
        return Path(on_path)

    # The cmake that the Studio-generated build in cmake_gcc/build was made
    # with, which is known to suit this project.
    cache = CMAKE_DIR / "build" / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("CMAKE_COMMAND:INTERNAL="):
                path = Path(line.split("=", 1)[1])
                if path.is_file():
                    return path

    exe = "cmake.exe" if os.name == "nt" else "cmake"
    found = sorted(SLT_INSTALLS.glob(f"conan/p/cmake*/p/bin/{exe}"),
                   key=lambda p: p.stat().st_mtime, reverse=True)
    return found[0] if found else None


def find_commander(explicit: Optional[str]) -> Optional[Path]:
    """Find Simplicity Commander, in the order cmake_gcc/toolchain.cmake uses.

    --commander, then POST_BUILD_EXE, then the Silicon Labs tool installs, then
    PATH.
    """
    if explicit:
        path = Path(explicit)
        return path if path.is_file() else None

    candidates: List[Path] = []
    env = os.environ.get("POST_BUILD_EXE")
    if env:
        candidates.append(Path(env))
    exe = "commander.exe" if os.name == "nt" else "commander"
    candidates += sorted(SLT_INSTALLS.glob(f"archive/*/{exe}"))
    candidates += sorted(SLT_INSTALLS.glob("archive/*/Contents/MacOS/commander"))
    on_path = shutil.which("commander")
    if on_path:
        candidates.append(Path(on_path))

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def check_defines(build_dir: Path, sequence: str, yyww: str) -> Optional[str]:
    """Confirm the build inputs reached the compiler. Return a problem, or None.

    Reads the compile database the configure step was asked for, so that a
    serial number lost on its way through CMake is caught here rather than on
    the board.
    """
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        return f"{database} was not produced, so the build inputs cannot be checked"

    expected = (
        "-DXBEE_APP=XBEE_APP_PROVISION",
        f"-DSN_BURNER_SEQUENCE={sequence}",
        f"-DSN_BURNER_BUILD_YYWW={yyww}",
    )
    for entry in json.loads(database.read_text(encoding="utf-8")):
        if entry.get("file", "").replace("\\", "/").endswith("/src/app/src/sn_burner.c"):
            command = entry.get("command", "")
            missing = [define for define in expected if define not in command]
            if missing:
                return "sn_burner.c was compiled without " + ", ".join(missing)
            return None
    return "sn_burner.c is not in the build"


def ask_yes_no(question: str) -> bool:
    """Ask a question; only "y" or "yes" is a yes. No input at all is a no."""
    try:
        answer = input(question)
    except EOFError:
        print()
        return False
    return answer.strip().lower() in ("y", "yes")


def watch_log(port, expected_sn: str, timeout_s: float) -> int:
    """Echo the board's log until its serial number result line, or the timeout."""
    deadline = time.monotonic() + timeout_s
    pending = b""

    while time.monotonic() < deadline:
        chunk = port.read(256)
        if not chunk:
            continue
        pending += chunk
        while b"\n" in pending:
            raw, pending = pending.split(b"\n", 1)
            line = ANSI_RE.sub("", raw.decode("ascii", errors="replace")).rstrip("\r")
            print(f"  | {line}")

            match = NOT_WRITTEN_RE.search(line)
            if match:
                print(f"FAIL: the board did not write the serial number: {match.group(1)}")
                return EXIT_NOT_WRITTEN

            match = WRITTEN_RE.search(line)
            if match:
                if match.group(1) == expected_sn:
                    print(f"PASS: serial number {expected_sn} written and verified by the board")
                    return EXIT_OK
                print(f"FAIL: the board wrote {match.group(1)}, expected {expected_sn}")
                return EXIT_MISMATCH

    print(f"FAIL: no serial number result from the board within {timeout_s:.0f} s")
    return EXIT_TIMEOUT


def flash_and_watch(args: argparse.Namespace, image: Path, expected_sn: str) -> int:
    """Flash the image, then watch the log for the serial number result."""
    if not args.port:
        return fail("--port is needed to watch the log, for example --port COM5", EXIT_USAGE)

    commander = find_commander(args.commander)
    if commander is None:
        return fail("Simplicity Commander not found. Pass --commander or set POST_BUILD_EXE.",
                    EXIT_USAGE)

    try:
        import serial  # pylint: disable=import-outside-toplevel
    except ImportError:
        return fail("pyserial is not installed. Install it with: "
                    "python -m pip install -r tools/requirements.txt", EXIT_USAGE)

    try:
        port = serial.Serial(args.port, LOG_BAUD, timeout=0.2)
    except serial.SerialException as exc:
        return fail(f"cannot open {args.port}: {exc}", EXIT_USAGE)

    # The port is opened before flashing, so that nothing the board prints
    # after its reset is missed.
    with port:
        port.reset_input_buffer()

        command = [str(commander), "flash", str(image), "--device", DEVICE]
        if args.serialno:
            command += ["--serialno", args.serialno]
        if run(command, REPO_ROOT) != 0:
            return fail("flashing failed", EXIT_FLASH)

        print(f"Watching {args.port} at {LOG_BAUD} baud for up to {args.timeout:.0f} s...")
        return watch_log(port, expected_sn, args.timeout)


def parse_args(argv: Optional[Sequence[str]]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build the provisioning image for one unit, which writes its "
                    "device serial number YYWW-NNNNN-C to MCU NVM3.")
    parser.add_argument("--sequence", required=True,
                        help="the NNNNN part: exactly five digits, for example 00123")
    parser.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD_DIR,
                        help="build directory (default: %(default)s)")
    parser.add_argument("--port",
                        help="serial port of the board's VCOM log, for example COM5; "
                             "needed only to flash and watch")
    parser.add_argument("--commander",
                        help="path to Simplicity Commander (default: found automatically)")
    parser.add_argument("--serialno",
                        help="J-Link serial number, when more than one probe is connected")
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S,
                        help="seconds to wait for the board's result (default: %(default)s)")
    parser.add_argument("--cmake",
                        help="path to cmake (default: PATH, then the one Studio's build uses)")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)

    if not SEQUENCE_RE.fullmatch(args.sequence):
        return fail(f'--sequence "{args.sequence}" is not exactly five digits, '
                    "for example 00123", EXIT_USAGE)

    for generated in ("CMakePresets.json", "xbee_provision.cmake"):
        if not (CMAKE_DIR / generated).is_file():
            return fail(f"cmake_gcc/{generated} is missing. Open the project in "
                        "Simplicity Studio once so that it generates the build files.",
                        EXIT_USAGE)

    cmake = find_cmake(args.cmake)
    if cmake is None:
        return fail("cmake not found. Put it on PATH or pass --cmake.", EXIT_USAGE)

    today = datetime.date.today()
    try:
        yyww = build_yyww(today)
    except ValueError as exc:
        return fail(str(exc), EXIT_USAGE)
    sequence = args.sequence
    expected_sn = format_sn(yyww, sequence)
    build_dir = args.build_dir.resolve()

    print(f"Serial number  {expected_sn}  (week {yyww} from the build date, {today.isoformat()})")
    print(f"Build dir      {build_dir}")
    print()

    configure = [str(cmake), "--preset", CONFIGURE_PRESET, "-B", str(build_dir),
                 "-DXBEE_APP_SELECT=PROVISION",
                 f"-DSN_BURNER_SEQUENCE={sequence}",
                 f"-DSN_BURNER_BUILD_YYWW={yyww}",
                 # For check_defines().
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"]
    if run(configure, CMAKE_DIR) != 0:
        return fail("configuring failed", EXIT_BUILD)

    build = [str(cmake), "--build", str(build_dir), "--config", BUILD_CONFIG,
             "--target", TARGET]
    if run(build, CMAKE_DIR) != 0:
        return fail("building failed", EXIT_BUILD)

    problem = check_defines(build_dir, sequence, yyww)
    if problem:
        return fail(problem, EXIT_BUILD)

    built = build_dir / BUILD_CONFIG / f"{TARGET}.hex"
    if not built.is_file():
        return fail(f"{built} was not produced", EXIT_BUILD)

    units = build_dir / "units"
    units.mkdir(parents=True, exist_ok=True)
    image = units / f"{TARGET}_{expected_sn}.hex"
    shutil.copyfile(built, image)

    print()
    print(f"Serial number  {expected_sn}")
    print(f"Image          {image}")
    print()

    if not ask_yes_no(f"Flash {image.name} and watch the log? [y/N] "):
        print("Not flashed.")
        return EXIT_OK

    return flash_and_watch(args, image, expected_sn)


if __name__ == "__main__":
    sys.exit(main())
