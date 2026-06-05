"""Dual-board Bluetooth smoke test.

Runs a fixed safety sequence against one or two Bluetooth serial ports and
prints per-board results. Intended to be invoked manually during integration
with the Arduino side; not discovered by `python -m unittest`.

Sequence (each non-S command sandwiched by S so a wheel never runs free):

    S -> F -> S -> B -> S -> L -> S -> R -> S -> U -> S -> D -> S

Default inter-command delay is 800ms, comfortably below the 1000ms timeout
recommended for the Arduino-side auto-stop in docs/windows_electrical_handoff.md.

Usage:

    # Dry-run (no hardware), good for verifying the sequence wiring:
    python -m tests.smoke_dual_bluetooth --dry-run --ports A,B

    # Single board:
    python -m tests.smoke_dual_bluetooth --ports /dev/rfcomm0

    # Two boards:
    python -m tests.smoke_dual_bluetooth --ports /dev/rfcomm0,/dev/rfcomm1

    # Windows:
    python -m tests.smoke_dual_bluetooth --ports COM5,COM6
"""

from __future__ import annotations

import argparse
import sys
import time
from collections import defaultdict

from pc_voice_controller.bluetooth_sender import (
    DryRunBluetoothSender,
    MultiBluetoothSender,
    SendTargetResult,
    parse_bluetooth_ports,
)


SAFETY_SEQUENCE: tuple[str, ...] = (
    "S",
    "F", "S",
    "B", "S",
    "L", "S",
    "R", "S",
    "U", "S",
    "D", "S",
)


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--ports",
        required=True,
        help="comma-separated Bluetooth ports, e.g. /dev/rfcomm0,/dev/rfcomm1 or COM5,COM6",
    )
    parser.add_argument("--baud", type=int, default=9600)
    parser.add_argument(
        "--delay",
        type=float,
        default=0.8,
        help="seconds between commands (default 0.8, must be < Arduino auto-stop timeout)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="print commands without opening real Bluetooth serial ports",
    )
    parser.add_argument(
        "--sequence",
        default=None,
        help="override sequence, comma-separated single chars (default safety sequence)",
    )
    return parser


def build_sender(ports: tuple[str, ...], baud: int, dry_run: bool):
    if dry_run:
        return DryRunBluetoothSender(ports)
    return MultiBluetoothSender.from_ports(ports, baudrate=baud)


def parse_sequence(value: str | None) -> tuple[str, ...]:
    if not value:
        return SAFETY_SEQUENCE
    parts = tuple(part.strip().upper() for part in value.split(",") if part.strip())
    if not parts:
        raise ValueError("sequence is empty after parsing")
    return parts


def run(
    ports: tuple[str, ...],
    sequence: tuple[str, ...],
    sender,
    delay_seconds: float,
) -> int:
    """Send the sequence; return process exit code (0 if every port ok every step)."""

    per_port_ok: dict[str, int] = defaultdict(int)
    per_port_fail: dict[str, int] = defaultdict(int)
    per_port_last_error: dict[str, str] = {}

    print(f"[smoke] ports={','.join(ports)} steps={len(sequence)} delay={delay_seconds:.2f}s")
    print(f"[smoke] sequence: {' -> '.join(sequence)}")
    print()

    step_width = len(str(len(sequence)))
    for index, code in enumerate(sequence, start=1):
        # Always re-stamp the wall-clock so an observer can sanity-check the cadence.
        timestamp = time.strftime("%H:%M:%S")
        try:
            results = sender.send_command(code)
        except Exception as exc:
            print(f"[{timestamp}] step {index:>{step_width}}/{len(sequence)} {code} FATAL: {exc}")
            return 2

        line = format_step(index, len(sequence), code, results, timestamp, step_width)
        print(line)

        for result in results:
            if result.ok:
                per_port_ok[result.port] += 1
            else:
                per_port_fail[result.port] += 1
                per_port_last_error[result.port] = result.error or "unknown"

        if index < len(sequence):
            time.sleep(delay_seconds)

    print()
    return print_summary(ports, per_port_ok, per_port_fail, per_port_last_error)


def format_step(
    index: int,
    total: int,
    code: str,
    results: list[SendTargetResult],
    timestamp: str,
    width: int,
) -> str:
    parts = []
    for result in results:
        if result.ok:
            tag = "dry" if result.dry_run else "ok"
            parts.append(f"{result.port}:{tag}")
        else:
            parts.append(f"{result.port}:ERR({result.error})")
    return f"[{timestamp}] step {index:>{width}}/{total} {code} -> " + ", ".join(parts)


def print_summary(
    ports: tuple[str, ...],
    ok: dict[str, int],
    fail: dict[str, int],
    last_error: dict[str, str],
) -> int:
    print("[smoke] summary")
    exit_code = 0
    for port in ports:
        ok_count = ok.get(port, 0)
        fail_count = fail.get(port, 0)
        line = f"  {port}: ok={ok_count} fail={fail_count}"
        if fail_count:
            line += f"  last_error={last_error.get(port, '?')}"
            exit_code = 1
        print(line)
    if exit_code == 0:
        print("[smoke] all writes acknowledged. Verify wheel motion on each board.")
    else:
        print("[smoke] FAILURES detected; check pairing, COM port, and HC-04 power.")
    return exit_code


def main(argv: list[str] | None = None) -> int:
    args = build_arg_parser().parse_args(argv)
    ports = parse_bluetooth_ports(args.ports)
    sequence = parse_sequence(args.sequence)
    if args.delay < 0:
        print("[smoke] --delay must be >= 0", file=sys.stderr)
        return 2

    sender = build_sender(ports, args.baud, args.dry_run)
    try:
        return run(ports, sequence, sender, args.delay)
    except KeyboardInterrupt:
        print("\n[smoke] interrupted by user; sending final S as safety stop")
        try:
            sender.send_command("S")
        except Exception as exc:
            print(f"[smoke] safety-stop send failed: {exc}", file=sys.stderr)
        return 130
    finally:
        sender.close()


if __name__ == "__main__":
    sys.exit(main())
