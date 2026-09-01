"""Shared "is this an intro/outro/filler track, not a real song" vocabulary,
used by intro_outro.py. Kept separate from text_normalize.py -- that module
answers "are these two strings the same identity", this one answers "does
this string name a piece of station filler rather than a song".
"""

import re

# Word-boundary so "Introspection" or "Identity" never match on a bare
# substring -- these only fire on the whole word.
_FILLER_PATTERN = re.compile(
    r"\b(intro|outro|interlude|skit|station\s?id(ent)?|jingle|sweeper|liner)\b",
    re.IGNORECASE,
)

# A full song is essentially never under this long; below it with no name
# hint is ambiguous enough to flag for review rather than guess either way.
SHORT_TRACK_SECONDS = 60.0


def is_filler_name(*names: str | None) -> bool:
    """True if any of title/artist/filename-stem names a filler track."""
    return any(name and _FILLER_PATTERN.search(name) for name in names)
