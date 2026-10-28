#!/usr/bin/env python3
"""Deterministic coverage of paint's streamed initial-frame race."""
from types import SimpleNamespace
from unittest.mock import patch
from desktop_test import Desktop, area
from gui_test import Paint, WHITE, BLACK, FOCUS
from console_test import TestFailure

paint = Paint(SimpleNamespace(), area())
page, black, red = paint.page_at(20, 20), paint.swatch(0), paint.swatch(4)
class Frame:
    def __init__(self, complete, brush_ready=None):
        self.complete = complete
        self.brush_ready = complete if brush_ready is None else brush_ready
    def pixel(self, x, y):
        px, py = paint.at(paint.palette)
        if (x, y) == (px, py + 10):
            return FOCUS
        if (x, y) == page:
            return WHITE
        if (x, y) == black:
            return BLACK
        if (x, y) == red:
            return (0xCC, 0x22, 0x22) if self.complete else BLACK
        raise AssertionError((x, y))

# The old white-page-only predicate returns this incomplete screen and its
# immediate red-swatch assertion fails, reproducing the original interleaving.
partial, complete = Frame(False), Frame(True)
assert partial.pixel(*page) == WHITE
assert partial.pixel(*red) != (0xCC, 0x22, 0x22)
label_pending = Frame(True, False)
frames = iter([partial, label_pending, complete])
d = SimpleNamespace(screen=lambda: next(frames))
d.wait = lambda predicate, what: Desktop.wait(d, predicate, what)
paint.g.d = d
with patch('desktop_test.time.sleep'), patch('gui_test.read_text', side_effect=lambda s, *args: 'Brush 4' if s.brush_ready else ''):
    assert paint.initial_frame() is complete
# A permanently wrong palette must still fail, without weakening assertions.
d.screen = lambda: partial
with patch('desktop_test.time.time', side_effect=[0, 11]), patch('gui_test.read_text', return_value='Brush 4'):
    try:
        paint.initial_frame()
    except TestFailure as error:
        assert 'complete palette' in str(error)
    else:
        raise AssertionError('accepted a permanently incorrect palette')
print('PASS: partial paint frame waits; complete frame passes; wrong palette fails')
