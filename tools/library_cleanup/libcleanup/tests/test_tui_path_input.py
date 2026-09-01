"""Terminal drag-and-drop shell-quotes a folder path that contains spaces
(konsole/GNOME Terminal wrap it in single quotes, xterm backslash-escapes
each space). The TUI's plain Input keeps that literally, so _roots_quiet
must strip it before Path() -- otherwise a leading quote becomes a real
path character and the run targets a bogus directory (confirmed real).
"""

import pytest

from libcleanup.tui import CleanupTUI

clean = CleanupTUI._clean_dropped_path


@pytest.mark.parametrize("raw, expected", [
    # konsole / GNOME Terminal: whole path single-quoted
    ("'/mnt/data/library/Excluded Audio'", "/mnt/data/library/Excluded Audio"),
    # double-quoted variant
    ('"/mnt/data/library/Excluded Audio"', "/mnt/data/library/Excluded Audio"),
    # xterm: backslash-escaped spaces (and a paren, for good measure)
    (r"/mnt/data/library/Excluded\ Audio\ (2)", "/mnt/data/library/Excluded Audio (2)"),
    # bare path with a space, no quoting -- left exactly as-is
    ("/mnt/data/library/Music DAP", "/mnt/data/library/Music DAP"),
    # plain path, the common case
    ("/mnt/data/library/Music", "/mnt/data/library/Music"),
    # trailing/leading whitespace from the drop
    ("  /mnt/data/library/Music  ", "/mnt/data/library/Music"),
    # quoted with surrounding whitespace
    ("  '/mnt/data/library/Excluded Audio'  ", "/mnt/data/library/Excluded Audio"),
    ("", ""),
])
def test_clean_dropped_path(raw, expected):
    assert clean(raw) == expected


def test_lone_leading_quote_is_not_stripped():
    # Not a matched pair -- don't chew a real (if odd) leading char.
    assert clean("'/weird/path") == "'/weird/path"
