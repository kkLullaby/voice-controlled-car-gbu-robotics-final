"""Bluetooth serial command sender."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol

from .command_parser import VALID_COMMANDS


class SerialLike(Protocol):
    """Minimal serial interface used by BluetoothSender."""

    def write(self, data: bytes) -> int: ...

    def flush(self) -> None: ...

    def close(self) -> None: ...


@dataclass(frozen=True)
class BluetoothConfig:
    """Runtime configuration for a paired Bluetooth serial device."""

    port: str
    baudrate: int = 9600
    timeout: float = 1.0


class BluetoothSender:
    """Send single-letter car command codes over a serial Bluetooth link."""

    def __init__(self, config: BluetoothConfig, serial_instance: SerialLike | None = None):
        self.config = config
        self._serial = serial_instance
        self._owns_serial = serial_instance is None

    def open(self) -> None:
        if self._serial is not None:
            return

        try:
            import serial
        except ImportError as exc:
            raise RuntimeError(
                "pyserial is not installed. Run: pip install -r requirements.txt"
            ) from exc

        self._serial = serial.Serial(
            port=self.config.port,
            baudrate=self.config.baudrate,
            timeout=self.config.timeout,
        )

    def send_command(self, code: str) -> None:
        command = code.strip().upper()
        if command not in VALID_COMMANDS:
            raise ValueError(f"Unsupported command code: {code!r}")

        if self._serial is None:
            self.open()
        if self._serial is None:
            raise RuntimeError("Bluetooth serial is not open")

        self._serial.write(f"{command}\n".encode("ascii"))
        self._serial.flush()

    def close(self) -> None:
        if self._serial is not None and self._owns_serial:
            self._serial.close()
        self._serial = None

    def __enter__(self) -> "BluetoothSender":
        self.open()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.close()


class DryRunBluetoothSender:
    """Drop-in sender that prints commands without touching Bluetooth hardware."""

    def send_command(self, code: str) -> None:
        command = code.strip().upper()
        if command not in VALID_COMMANDS:
            raise ValueError(f"Unsupported command code: {code!r}")
        print(f"[dry-run] would send: {command}")

    def close(self) -> None:
        return None

