"""Command-line entry point for PC voice to Bluetooth control."""

from __future__ import annotations

import argparse
import os
import sys
from typing import Iterable

from .asr_listener import DashScopeMicListener
from .bluetooth_sender import (
    BluetoothConfig,
    BluetoothSender,
    DryRunBluetoothSender,
    MultiBluetoothSender,
    SendTargetResult,
    parse_bluetooth_ports,
)
from .command_parser import CommandDebouncer, CommandParser, ParsedCommand


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Recognize Chinese voice commands and send car action codes over Bluetooth."
    )
    parser.add_argument(
        "--mode",
        choices=("manual-code", "manual-text", "asr"),
        default="manual-text",
        help="manual-code sends typed F/B/L/R/S/U/D; manual-text parses typed Chinese; asr uses mic.",
    )
    default_ports = os.getenv("BT_PORTS") or os.getenv("BT_PORT", "/dev/rfcomm0")
    parser.add_argument(
        "--ports",
        default=default_ports,
        help="comma-separated Bluetooth serial ports, e.g. /dev/rfcomm0,/dev/rfcomm1",
    )
    parser.add_argument("--port", dest="ports", help=argparse.SUPPRESS)
    parser.add_argument("--baud", type=int, default=int(os.getenv("BT_BAUD", "9600")))
    parser.add_argument("--dry-run", action="store_true", help="print commands without Bluetooth")
    parser.add_argument(
        "--repeat-interval",
        type=float,
        default=float(os.getenv("COMMAND_REPEAT_INTERVAL", "1.0")),
        help="seconds to suppress repeated identical commands",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    load_env_file_if_available()
    args = build_arg_parser().parse_args(argv)

    parser = CommandParser()
    debouncer = CommandDebouncer(args.repeat_interval)
    ports = parse_bluetooth_ports(args.ports)
    sender = build_sender(ports, args.baud, args.dry_run)

    try:
        if args.mode == "manual-code":
            run_manual_code(sender)
        elif args.mode == "manual-text":
            run_text_stream(read_stdin_lines(), parser, debouncer, sender)
        else:
            listener = DashScopeMicListener(api_key=os.getenv("DASHSCOPE_API_KEY", ""))
            run_text_stream(listener.listen(), parser, debouncer, sender)
    except RuntimeError as exc:
        print(f"[error] {exc}", file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        print("\n[main] stopped")
        return 0
    finally:
        sender.close()

    return 0


def build_sender(ports: tuple[str, ...], baudrate: int, dry_run: bool):
    if dry_run:
        return DryRunBluetoothSender(ports)
    if len(ports) == 1:
        return BluetoothSender(BluetoothConfig(port=ports[0], baudrate=baudrate))
    return MultiBluetoothSender.from_ports(ports, baudrate=baudrate)


def load_env_file_if_available() -> None:
    try:
        from dotenv import load_dotenv
    except ImportError:
        return

    load_dotenv()


def read_stdin_lines() -> Iterable[str]:
    print("Type Chinese commands, one per line. Ctrl+C to exit.")
    while True:
        try:
            line = input("> ").strip()
        except EOFError:
            return
        if line:
            yield line


def run_manual_code(sender) -> None:
    print("Type command codes F/B/L/R/S/U/D, one per line. Ctrl+C to exit.")
    while True:
        try:
            code = input("> ").strip().upper()
        except EOFError:
            return
        if not code:
            continue
        results = sender.send_command(code)
        print(f"[send] {code} {format_send_results(results)}")


def run_text_stream(
    texts: Iterable[str],
    parser: CommandParser,
    debouncer: CommandDebouncer,
    sender,
) -> None:
    for text in texts:
        command = parser.parse(text)
        if command is None:
            print(f"[ignore] {text}")
            continue

        if not debouncer.should_send(command):
            print(f"[skip-repeat] {format_command(command)}")
            continue

        results = sender.send_command(command.code)
        print(f"[send] {format_command(command)} {format_send_results(results)}")


def format_command(command: ParsedCommand) -> str:
    return (
        f"{command.source_text} -> {command.code} "
        f"({command.label}, matched={command.matched_phrase})"
    )


def format_send_results(results: list[SendTargetResult]) -> str:
    parts = []
    for result in results:
        if result.ok:
            parts.append(f"{result.port}:ok")
        else:
            parts.append(f"{result.port}:error={result.error}")
    return "[" + ", ".join(parts) + "]"


if __name__ == "__main__":
    sys.exit(main())
