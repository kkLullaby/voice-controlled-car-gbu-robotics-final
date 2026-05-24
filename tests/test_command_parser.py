import unittest

from pc_voice_controller.command_parser import CommandDebouncer, CommandParser


class CommandParserTest(unittest.TestCase):
    def test_parse_basic_motion_commands(self):
        parser = CommandParser()

        self.assertEqual(parser.parse("前进").code, "F")
        self.assertEqual(parser.parse("请往后退一点").code, "B")
        self.assertEqual(parser.parse("向左转").code, "L")
        self.assertEqual(parser.parse("往右边走").code, "R")

    def test_parse_speed_and_stop_commands(self):
        parser = CommandParser()

        self.assertEqual(parser.parse("快一点").code, "U")
        self.assertEqual(parser.parse("慢点").code, "D")
        self.assertEqual(parser.parse("马上停下").code, "S")

    def test_stop_has_priority_over_motion(self):
        parser = CommandParser()

        self.assertEqual(parser.parse("停止前进").code, "S")
        self.assertEqual(parser.parse("别动，先不要向前").code, "S")

    def test_unknown_text_returns_none(self):
        parser = CommandParser()

        self.assertIsNone(parser.parse(""))
        self.assertIsNone(parser.parse("今天天气不错"))

    def test_debouncer_suppresses_repeated_command_within_interval(self):
        parser = CommandParser()
        debouncer = CommandDebouncer(repeat_interval_seconds=1.0)
        first = parser.parse("前进")
        second = parser.parse("往前走")
        third = parser.parse("左转")

        self.assertTrue(debouncer.should_send(first, now=10.0))
        self.assertFalse(debouncer.should_send(second, now=10.5))
        self.assertTrue(debouncer.should_send(second, now=11.1))
        self.assertTrue(debouncer.should_send(third, now=11.2))


if __name__ == "__main__":
    unittest.main()
