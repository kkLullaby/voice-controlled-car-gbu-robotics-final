"""Bluetooth serial command sender."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Iterable, Protocol

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
    close_after_send: bool = True


@dataclass(frozen=True)
class SendTargetResult:
    """Result of sending one command to one Bluetooth target."""

    port: str
    ok: bool
    error: str | None = None
    dry_run: bool = False


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

    def send_command(self, code: str) -> list[SendTargetResult]:
        command = code.strip().upper()
        if command not in VALID_COMMANDS:
            raise ValueError(f"Unsupported command code: {code!r}")

        try:
            if self._serial is None:
                self.open()
            if self._serial is None:
                raise RuntimeError("Bluetooth serial is not open")

            self._serial.write(f"{command}\n".encode("ascii"))
            self._serial.flush()
            return [SendTargetResult(port=self.config.port, ok=True)]
        except Exception as exc:
            self.close()
            return [SendTargetResult(port=self.config.port, ok=False, error=str(exc))]
        finally:
            if self.config.close_after_send:
                self.close()

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

    def __init__(self, ports: Iterable[str] | None = None):
        self.ports = tuple(ports or ("dry-run",))

    def send_command(self, code: str) -> list[SendTargetResult]:
        command = code.strip().upper()
        if command not in VALID_COMMANDS:
            raise ValueError(f"Unsupported command code: {code!r}")
        results = []
        for port in self.ports:
            print(f"[dry-run] {port} <- {command}")
            results.append(SendTargetResult(port=port, ok=True, dry_run=True))
        return results

    def close(self) -> None:
        return None


class MultiBluetoothSender:
    """Broadcast one command to multiple Bluetooth serial ports."""

    def __init__(self, senders: Iterable[BluetoothSender]):
        self.senders = tuple(senders)
        if not self.senders:
            raise ValueError("At least one Bluetooth sender is required")

    @classmethod
    def from_ports(
        cls,
        ports: Iterable[str],
        *,
        baudrate: int = 9600,
        timeout: float = 1.0,
    ) -> "MultiBluetoothSender":
        senders = [
            BluetoothSender(BluetoothConfig(port=port, baudrate=baudrate, timeout=timeout))
            for port in ports
        ]
        return cls(senders)

    def send_command(self, code: str) -> list[SendTargetResult]:
        command = code.strip().upper()
        if command not in VALID_COMMANDS:
            raise ValueError(f"Unsupported command code: {code!r}")

        results: list[SendTargetResult] = []
        for sender in self.senders:
            try:
                results.extend(sender.send_command(command))
            except Exception as exc:
                results.append(
                    SendTargetResult(
                        port=sender.config.port,
                        ok=False,
                        error=str(exc),
                    )
                )
        return results

    def close(self) -> None:
        for sender in self.senders:
            sender.close()


def parse_bluetooth_ports(value: str | Iterable[str] | None) -> tuple[str, ...]:
    """Parse comma-separated Bluetooth ports into a stable tuple."""

    if value is None:
        return ("/dev/rfcomm0",)
    if isinstance(value, str):
        ports = [part.strip() for part in value.split(",")]
    else:
        ports = [str(part).strip() for part in value]
    cleaned = tuple(port for port in ports if port)
    if not cleaned:
        raise ValueError("At least one Bluetooth port is required")
    return cleaned
