# RadioStation v0.1 — release notes (draft)

## Suggested tag

```sh
git tag -a v0.1 -m "RadioStation 0.1 — first public release"
git push origin v0.1
```

## GitHub Release body

---

**RadioStation 0.1** is the first public release — a Linux desktop console for
running a music station unattended.

### Highlights

- **Auto DJ** with weighted category rotation and artist/title separation.
- **Schedule blocks** and **clock wheels** — time-of-day templates that switch
  playlist, selection strategy and cart behaviour, and resume mid-hour after a
  restart.
- **Cart Wall** with schedule-driven automation: carts fire as a hard cut
  *between* songs or *overlaid* on the outgoing track. A new per-cart
  **"in-between only"** flag keeps jingles that carry their own music bed from
  ever being overlaid.
- **Dual-deck mixing** — equal-power automatic crossfades with idle-deck
  cue-ahead and manual override.
- **Mastering chain** — K-weighted loudness leveler, true-peak lookahead
  limiter, per-channel and master EQ, mic ducking.
- **Icecast/Shoutcast streaming** (MP3 / Ogg Vorbis / Opus).
- **Library** — SQLite with full-text search, smart playlists, star ratings,
  ReplayGain analysis and loudness history.
- Hand-painted **console UI** with fully dockable panels.

### Requirements

Linux, Qt 6, GStreamer 1.0 (base/good/libav plugins + an MP3 encoder), TagLib.
See the [README](../README.md) for build instructions.

### Known limitations

- Linux only; no packaged binaries yet — build from source.
- `AutoDjIntegrationTest` is timing-sensitive and can be flaky under heavy
  parallel test load; it passes when run on its own.

Full history in [CHANGELOG.md](../CHANGELOG.md).
