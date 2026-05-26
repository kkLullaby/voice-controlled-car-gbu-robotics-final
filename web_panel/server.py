"""Local HTTP server for the voice Bluetooth control panel."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import argparse
import json
import os
from pathlib import Path
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import threading
import time
from typing import Any
from urllib.parse import urlparse

from pc_voice_controller.asr_listener import DashScopeMicListener
from pc_voice_controller.bluetooth_sender import (
    DryRunBluetoothSender,
    MultiBluetoothSender,
    SendTargetResult,
    parse_bluetooth_ports,
)
from pc_voice_controller.command_parser import CommandDebouncer, CommandParser, ParsedCommand, VALID_COMMANDS
from pc_voice_controller.main import load_env_file_if_available


PROJECT_ROOT = Path(__file__).resolve().parents[1]
STATIC_ROOT = Path(__file__).resolve().parent / "static"


@dataclass
class PanelConfig:
    dry_run: bool = True
    ports: tuple[str, ...] = ("/dev/rfcomm0", "/dev/rfcomm1")
    baudrate: int = 9600
    repeat_interval_seconds: float = 1.0


class PanelState:
    """Shared state for the local web panel."""

    def __init__(self, config: PanelConfig):
        self.config = config
        self.parser = CommandParser()
        self.debouncer = CommandDebouncer(config.repeat_interval_seconds)
        self.events: deque[dict[str, Any]] = deque(maxlen=120)
        self.lock = threading.RLock()
        self.sender = self._new_sender()
        self.last_text = ""
        self.last_command: str | None = None
        self.last_send_results: list[dict[str, Any]] = []
        self.asr_running = False
        self.asr_stop_event: threading.Event | None = None
        self.asr_thread: threading.Thread | None = None
        self.add_event("system", "Web panel ready")

    def _new_sender(self):
        if self.config.dry_run:
            return DryRunBluetoothSender(self.config.ports)
        return MultiBluetoothSender.from_ports(
            self.config.ports,
            baudrate=self.config.baudrate,
        )

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return {
                "dry_run": self.config.dry_run,
                "ports": list(self.config.ports),
                "port": ",".join(self.config.ports),
                "baudrate": self.config.baudrate,
                "repeat_interval_seconds": self.config.repeat_interval_seconds,
                "asr_running": self.asr_running,
                "last_text": self.last_text,
                "last_command": self.last_command,
                "last_send_results": self.last_send_results,
                "events": list(self.events),
                "dashscope_key_present": bool(os.getenv("DASHSCOPE_API_KEY", "").strip()),
            }

    def update_config(self, data: dict[str, Any]) -> dict[str, Any]:
        with self.lock:
            self.sender.close()
            port_value = data.get("ports", data.get("port", ",".join(self.config.ports)))
            self.config = PanelConfig(
                dry_run=bool(data.get("dry_run", self.config.dry_run)),
                ports=parse_bluetooth_ports(port_value),
                baudrate=int(data.get("baudrate", self.config.baudrate)),
                repeat_interval_seconds=float(
                    data.get("repeat_interval_seconds", self.config.repeat_interval_seconds)
                ),
            )
            self.debouncer = CommandDebouncer(self.config.repeat_interval_seconds)
            self.sender = self._new_sender()
            self.add_event(
                "config",
                "dry_run={} ports={} baud={}".format(
                    self.config.dry_run,
                    ",".join(self.config.ports),
                    self.config.baudrate,
                ),
            )
            return self.snapshot()

    def handle_text(self, text: str, source: str = "panel") -> dict[str, Any]:
        command = self.parser.parse(text)
        with self.lock:
            self.last_text = text

        if command is None:
            self.add_event("ignored", text)
            return {"sent": False, "reason": "no_command", "text": text}

        if not self.debouncer.should_send(command):
            self.add_event("repeat", format_command(command))
            return {
                "sent": False,
                "reason": "repeat",
                "text": text,
                "command": command.code,
            }

        return self.send_parsed_command(command, source=source)

    def send_code(self, code: str, source: str = "panel") -> dict[str, Any]:
        command = str(code or "").strip().upper()
        if command not in VALID_COMMANDS:
            raise ValueError(f"Unsupported command code: {code!r}")

        results = self.sender.send_command(command)
        serialized = serialize_send_results(results)
        with self.lock:
            self.last_command = command
            self.last_send_results = serialized
        self.add_event("send", f"{source}: {command} {format_send_results(results)}")
        return {"sent": any(result.ok for result in results), "command": command, "results": serialized}

    def send_parsed_command(self, command: ParsedCommand, source: str = "panel") -> dict[str, Any]:
        results = self.sender.send_command(command.code)
        serialized = serialize_send_results(results)
        with self.lock:
            self.last_command = command.code
            self.last_send_results = serialized
        self.add_event("send", f"{source}: {format_command(command)} {format_send_results(results)}")
        return {
            "sent": any(result.ok for result in results),
            "text": command.source_text,
            "command": command.code,
            "label": command.label,
            "matched_phrase": command.matched_phrase,
            "results": serialized,
        }

    def start_asr(self) -> dict[str, Any]:
        with self.lock:
            if self.asr_running:
                return {"asr_running": True, "message": "already running"}
            self.asr_running = True
            self.asr_stop_event = threading.Event()
            stop_event = self.asr_stop_event
            self.asr_thread = threading.Thread(
                target=self._asr_worker,
                args=(stop_event,),
                name="dashscope-asr-worker",
                daemon=True,
            )
            self.asr_thread.start()
        self.add_event("asr", "DashScope ASR starting")
        return {"asr_running": True}

    def stop_asr(self) -> dict[str, Any]:
        with self.lock:
            if self.asr_stop_event is not None:
                self.asr_stop_event.set()
            self.asr_running = False
        self.add_event("asr", "DashScope ASR stop requested")
        return {"asr_running": False}

    def _asr_worker(self, stop_event: threading.Event) -> None:
        try:
            listener = DashScopeMicListener(api_key=os.getenv("DASHSCOPE_API_KEY", ""))
            for text in listener.listen(stop_event):
                self.handle_text(text, source="dashscope")
        except Exception as exc:
            self.add_event("error", str(exc))
        finally:
            with self.lock:
                self.asr_running = False
                self.asr_stop_event = None
            self.add_event("asr", "DashScope ASR stopped")

    def add_event(self, kind: str, message: str) -> None:
        event = {
            "time": time.strftime("%H:%M:%S"),
            "kind": kind,
            "message": message,
        }
        self.events.appendleft(event)


class PanelRequestHandler(BaseHTTPRequestHandler):
    state: PanelState

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/":
            self._send_file(STATIC_ROOT / "index.html", "text/html; charset=utf-8")
            return
        if path == "/api/status":
            self._send_json(self.state.snapshot())
            return
        if path.startswith("/static/"):
            relative = path.removeprefix("/static/")
            file_path = (STATIC_ROOT / relative).resolve()
            if STATIC_ROOT.resolve() not in file_path.parents:
                self.send_error(404)
                return
            self._send_file(file_path, content_type_for(file_path))
            return
        self.send_error(404)

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        try:
            data = self._read_json()
            if path == "/api/config":
                self._send_json(self.state.update_config(data))
            elif path == "/api/command/text":
                self._send_json(self.state.handle_text(str(data.get("text", ""))))
            elif path == "/api/command/code":
                self._send_json(self.state.send_code(str(data.get("code", ""))))
            elif path == "/api/asr/start":
                self._send_json(self.state.start_asr())
            elif path == "/api/asr/stop":
                self._send_json(self.state.stop_asr())
            else:
                self.send_error(404)
        except ValueError as exc:
            self._send_json({"error": str(exc)}, status=400)
        except RuntimeError as exc:
            self._send_json({"error": str(exc)}, status=500)
        except Exception as exc:
            self._send_json({"error": str(exc)}, status=500)

    def log_message(self, format: str, *args) -> None:
        return

    def _read_json(self) -> dict[str, Any]:
        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length <= 0:
            return {}
        raw = self.rfile.read(content_length)
        return json.loads(raw.decode("utf-8"))

    def _send_json(self, data: dict[str, Any], status: int = 200) -> None:
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path, content_type: str) -> None:
        if not path.exists() or not path.is_file():
            self.send_error(404)
            return
        body = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


def content_type_for(path: Path) -> str:
    suffix = path.suffix.lower()
    if suffix == ".css":
        return "text/css; charset=utf-8"
    if suffix == ".js":
        return "application/javascript; charset=utf-8"
    if suffix == ".html":
        return "text/html; charset=utf-8"
    return "application/octet-stream"


def format_command(command: ParsedCommand) -> str:
    return (
        f"{command.source_text} -> {command.code} "
        f"({command.label}, matched={command.matched_phrase})"
    )


def serialize_send_results(results: list[SendTargetResult]) -> list[dict[str, Any]]:
    return [
        {
            "port": result.port,
            "ok": result.ok,
            "error": result.error,
            "dry_run": result.dry_run,
        }
        for result in results
    ]


def format_send_results(results: list[SendTargetResult]) -> str:
    parts = []
    for result in results:
        if result.ok:
            parts.append(f"{result.port}:ok")
        else:
            parts.append(f"{result.port}:error={result.error}")
    return "[" + ", ".join(parts) + "]"


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Run the local voice control web panel.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    default_ports = os.getenv("BT_PORTS") or os.getenv("BT_PORT", "/dev/rfcomm0,/dev/rfcomm1")
    parser.add_argument("--bt-ports", default=default_ports)
    parser.add_argument("--bt-port", dest="bt_ports", help=argparse.SUPPRESS)
    parser.add_argument("--bt-baud", type=int, default=int(os.getenv("BT_BAUD", "9600")))
    parser.add_argument("--dry-run", action="store_true", default=True)
    parser.add_argument("--real-bluetooth", action="store_false", dest="dry_run")
    return parser


def main() -> int:
    load_env_file_if_available()
    args = build_arg_parser().parse_args()
    config = PanelConfig(
        dry_run=args.dry_run,
        ports=parse_bluetooth_ports(args.bt_ports),
        baudrate=args.bt_baud,
    )
    state = PanelState(config)

    handler_class = type(
        "ConfiguredPanelRequestHandler",
        (PanelRequestHandler,),
        {"state": state},
    )
    server = ThreadingHTTPServer((args.host, args.port), handler_class)
    print(f"Web panel running at http://{args.host}:{args.port}")
    print(
        "dry_run={} bt_ports={} bt_baud={}".format(
            config.dry_run,
            ",".join(config.ports),
            config.baudrate,
        )
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nWeb panel stopped")
    finally:
        state.stop_asr()
        state.sender.close()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
