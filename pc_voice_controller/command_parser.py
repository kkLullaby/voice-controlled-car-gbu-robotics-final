"""Parse Chinese voice transcripts into compact car command codes."""

from __future__ import annotations

from dataclasses import dataclass
import re
import time
from typing import Iterable


VALID_COMMANDS = frozenset({"F", "B", "L", "R", "S", "U", "D"})


@dataclass(frozen=True)
class ParsedCommand:
    """A command parsed from one ASR transcript."""

    code: str
    label: str
    matched_phrase: str
    source_text: str


@dataclass(frozen=True)
class CommandRule:
    """Keyword rule for one car command."""

    code: str
    label: str
    phrases: tuple[str, ...]


COMMAND_RULES: tuple[CommandRule, ...] = (
    CommandRule("S", "停止", ("停止", "停下", "停车", "刹车", "别动", "不要动", "停住")),
    CommandRule("U", "加速", ("加速", "快一点", "快点", "加快", "速度快", "跑快")),
    CommandRule("D", "减速", ("减速", "慢一点", "慢点", "放慢", "速度慢", "跑慢")),
    CommandRule("F", "前进", ("前进", "向前", "往前", "朝前", "直走", "出发")),
    CommandRule("B", "后退", ("后退", "倒车", "向后", "往后", "退后", "退")),
    CommandRule("L", "左转", ("左转", "向左", "往左", "左拐", "左边")),
    CommandRule("R", "右转", ("右转", "向右", "往右", "右拐", "右边")),
)


_PUNCTUATION_PATTERN = re.compile(r"[\s,，。.!！?？:：;；、\"'“”‘’（）()\[\]{}<>《》]+")


class CommandParser:
    """Keyword-based parser for the first version of the project."""

    def __init__(self, rules: Iterable[CommandRule] = COMMAND_RULES):
        self._rules = tuple(rules)

    def parse(self, text: str) -> ParsedCommand | None:
        """Return the highest-priority command found in text."""

        normalized = normalize_text(text)
        if not normalized:
            return None

        for rule in self._rules:
            for phrase in rule.phrases:
                if normalize_text(phrase) in normalized:
                    return ParsedCommand(
                        code=rule.code,
                        label=rule.label,
                        matched_phrase=phrase,
                        source_text=text,
                    )
        return None


class CommandDebouncer:
    """Suppress repeated command sends caused by streaming ASR partial results."""

    def __init__(self, repeat_interval_seconds: float = 1.0):
        if repeat_interval_seconds < 0:
            raise ValueError("repeat_interval_seconds must be >= 0")
        self.repeat_interval_seconds = repeat_interval_seconds
        self._last_code: str | None = None
        self._last_sent_at = 0.0

    def should_send(self, command: ParsedCommand, now: float | None = None) -> bool:
        current_time = time.monotonic() if now is None else now
        if (
            command.code == self._last_code
            and current_time - self._last_sent_at < self.repeat_interval_seconds
        ):
            return False

        self._last_code = command.code
        self._last_sent_at = current_time
        return True


def normalize_text(text: str) -> str:
    """Normalize Chinese ASR text for simple keyword matching."""

    return _PUNCTUATION_PATTERN.sub("", str(text or "")).lower()
