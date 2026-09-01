"""Folder -> Type mapping for files that go through the full pipeline.

Any top-level folder under the library root is treated as a Song source by
default -- per user instruction, this covers both the original named
folders (Easy/Hard/Rythmic/JDM/Random) and anything new added later
(OST/Ambient/Classics content, freshly-added artist-named folders like
"Journey/" or manually pre-sorted Artist folders -- all have real artists
and go through the same dedup/retag/energy/restructure treatment).

The only exception is exclude.py's WHOLESALE_EXCLUDED_FOLDERS (RAW,
GTA_Radio, Jingles, Sleep, Audio Books, Playlists), which are physically
relocated out of the library root by the `exclude` stage before `scan`
ever runs -- checked here too as a defensive fallback in case `exclude`
hasn't run yet.
"""

from .exclude import WHOLESALE_EXCLUDED_FOLDERS


def top_level_folder(relative_path) -> str:
    return relative_path.parts[0] if relative_path.parts else ""


def classify_type(relative_path) -> str | None:
    """Returns "Song" for any file under a top-level folder that isn't a
    wholesale exception, or None if it should be skipped."""
    folder = top_level_folder(relative_path)
    if folder in WHOLESALE_EXCLUDED_FOLDERS:
        return None
    return "Song"
