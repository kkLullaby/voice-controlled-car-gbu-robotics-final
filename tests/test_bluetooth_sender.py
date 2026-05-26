import unittest

from pc_voice_controller.bluetooth_sender import (
    BluetoothConfig,
    BluetoothSender,
    MultiBluetoothSender,
    parse_bluetooth_ports,
)


class FakeSerial:
    def __init__(self):
        self.writes = []
        self.flushed = False
        self.closed = False

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        self.flushed = True

    def close(self) -> None:
        self.closed = True


class FailingSerial(FakeSerial):
    def write(self, data: bytes) -> int:
        raise OSError("simulated write failure")


class BluetoothSenderTest(unittest.TestCase):
    def test_send_command_writes_single_code_with_newline(self):
        serial = FakeSerial()
        sender = BluetoothSender(
            BluetoothConfig(port="/dev/test", close_after_send=False),
            serial_instance=serial,
        )

        result = sender.send_command("f")

        self.assertEqual(serial.writes, [b"F\n"])
        self.assertTrue(serial.flushed)
        self.assertEqual(result[0].port, "/dev/test")
        self.assertTrue(result[0].ok)

    def test_send_command_rejects_unknown_code(self):
        serial = FakeSerial()
        sender = BluetoothSender(BluetoothConfig(port="/dev/test"), serial_instance=serial)

        with self.assertRaises(ValueError):
            sender.send_command("X")

        self.assertEqual(serial.writes, [])

    def test_send_command_returns_error_result_on_write_failure(self):
        serial = FailingSerial()
        sender = BluetoothSender(BluetoothConfig(port="/dev/test"), serial_instance=serial)

        results = sender.send_command("D")

        self.assertEqual(len(results), 1)
        self.assertEqual(results[0].port, "/dev/test")
        self.assertFalse(results[0].ok)
        self.assertIn("simulated write failure", results[0].error)

    def test_send_command_releases_serial_after_success_by_default(self):
        serial = FakeSerial()
        sender = BluetoothSender(BluetoothConfig(port="/dev/test"), serial_instance=serial)

        results = sender.send_command("S")

        self.assertTrue(results[0].ok)
        self.assertIsNone(sender._serial)

    def test_multi_sender_broadcasts_to_all_ports(self):
        serial_a = FakeSerial()
        serial_b = FakeSerial()
        sender = MultiBluetoothSender(
            [
                BluetoothSender(BluetoothConfig(port="/dev/a"), serial_instance=serial_a),
                BluetoothSender(BluetoothConfig(port="/dev/b"), serial_instance=serial_b),
            ]
        )

        results = sender.send_command("S")

        self.assertEqual(serial_a.writes, [b"S\n"])
        self.assertEqual(serial_b.writes, [b"S\n"])
        self.assertEqual([result.port for result in results], ["/dev/a", "/dev/b"])
        self.assertTrue(all(result.ok for result in results))

    def test_parse_bluetooth_ports_accepts_comma_separated_values(self):
        self.assertEqual(
            parse_bluetooth_ports("/dev/rfcomm0, /dev/rfcomm1"),
            ("/dev/rfcomm0", "/dev/rfcomm1"),
        )


if __name__ == "__main__":
    unittest.main()
