import unittest

from pc_voice_controller.bluetooth_sender import BluetoothConfig, BluetoothSender


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


class BluetoothSenderTest(unittest.TestCase):
    def test_send_command_writes_single_code_with_newline(self):
        serial = FakeSerial()
        sender = BluetoothSender(BluetoothConfig(port="/dev/test"), serial_instance=serial)

        sender.send_command("f")

        self.assertEqual(serial.writes, [b"F\n"])
        self.assertTrue(serial.flushed)

    def test_send_command_rejects_unknown_code(self):
        serial = FakeSerial()
        sender = BluetoothSender(BluetoothConfig(port="/dev/test"), serial_instance=serial)

        with self.assertRaises(ValueError):
            sender.send_command("X")

        self.assertEqual(serial.writes, [])


if __name__ == "__main__":
    unittest.main()
