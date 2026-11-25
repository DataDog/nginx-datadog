"""Unit tests for `docker_logs`; they need no docker compose services."""

import io
import struct
import unittest

from . import docker_logs

STDOUT = 1
STDERR = 2


def frame(stream: int, data: bytes) -> bytes:
    return struct.pack(">BxxxL", stream, len(data)) + data


def split_lines(frames: list[bytes]) -> list[tuple[str, str]]:
    lines: list[tuple[str, str]] = []
    docker_logs.split_lines(
        docker_logs.read_multiplexed(io.BytesIO(b"".join(frames))),
        lambda stream, line: lines.append((stream, line)))
    return lines


class TestSplitLines(unittest.TestCase):

    def test_whole_lines(self) -> None:
        self.assertEqual(
            split_lines([
                frame(STDOUT, b"one\ntwo\n"),
                frame(STDERR, b"three\n"),
            ]),
            [("stdout", "one"), ("stdout", "two"), ("stderr", "three")],
        )

    def test_long_line_split_by_lines_of_other_stream(self) -> None:
        self.assertEqual(
            split_lines([
                frame(STDOUT, b"long "),
                frame(STDERR, b"notice 1\n"),
                frame(STDOUT, b"line"),
                frame(STDERR, b"notice 2\nnotice 3\n"),
                frame(STDOUT, b"!\nshort\n"),
            ]),
            [
                ("stderr", "notice 1"),
                ("stderr", "notice 2"),
                ("stderr", "notice 3"),
                ("stdout", "long line!"),
                ("stdout", "short"),
            ],
        )

    def test_character_split_across_frames(self) -> None:
        euro = "€".encode("utf8")
        self.assertEqual(
            split_lines(
                [frame(STDOUT, euro[:1]),
                 frame(STDOUT, euro[1:] + b"\n")]),
            [("stdout", "€")],
        )

    def test_unterminated_lines_at_end(self) -> None:
        self.assertEqual(
            split_lines([frame(STDOUT, b"out"),
                         frame(STDERR, b"err")]),
            [("stdout", "out"), ("stderr", "err")],
        )

    def test_truncated_frame(self) -> None:
        with self.assertRaises(EOFError):
            split_lines([frame(STDOUT, b"abc\n")[:-1]])


if __name__ == "__main__":
    unittest.main()
