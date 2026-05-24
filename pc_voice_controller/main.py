"""Command-line entry point for PC voice to Bluetooth control."""

from __future__ import annotations

import argparse
import os
import sys
from typing import Iterable

from .asr_listener import DashScopeMicListener
from .bluetooth_sender import BluetoothConfig, BluetoothSender, DryRunBluetoothSender
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
    parser.add_argument("--port", default=os.getenv("BT_PORT", "/dev/rfcomm0"))
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
    sender = (
        DryRunBluetoothSender()
        if args.dry_run
        else BluetoothSender(BluetoothConfig(port=args.port, baudrate=args.baud))
    )

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
        code = input("> ").strip().upper()
        if not code:
            continue
        sender.send_command(code)
        print(f"[send] {code}")


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

        sender.send_command(command.code)
        print(f"[send] {format_command(command)}")


def format_command(command: ParsedCommand) -> str:
    return (
        f"{command.source_text} -> {command.code} "
        f"({command.label}, matched={command.matched_phrase})"
    )


if __name__ == "__main__":
    sys.exit(main())
