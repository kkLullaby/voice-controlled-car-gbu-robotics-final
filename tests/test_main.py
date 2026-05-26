import io
import unittest
from unittest.mock import patch

from pc_voice_controller.main import run_manual_code


class RecordingSender:
    def __init__(self):
        self.commands = []

    def send_command(self, code):
        self.commands.append(code)
        return []


class MainCliTest(unittest.TestCase):
    def test_manual_code_returns_cleanly_on_eof(self):
        sender = RecordingSender()

        with patch("builtins.input", side_effect=["S", "F", "S", EOFError()]):
            with patch("sys.stdout", new=io.StringIO()):
                run_manual_code(sender)

        self.assertEqual(sender.commands, ["S", "F", "S"])


if __name__ == "__main__":
    unittest.main()
