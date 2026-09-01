#!/usr/bin/env bash
# Desktop-launcher entry point for the Music Library Cleanup TUI.
# Runs in a terminal (see Music-Library-Cleanup.desktop's Terminal=true)
# since this is a text UI, not a graphical one.
#
# Deliberately NOT `set -e`: this script's whole point is to always pause
# before the window closes so a crash/error is actually readable, and
# `set -e` would abort the script the instant a command fails -- before
# ever reaching the status check or the pause below.

cd "$(dirname "$0")" || {
    echo "Failed to cd into $(dirname "$0")"
    read -rp "Press Enter to close this window..."
    exit 1
}

if [ ! -f .venv/bin/activate ]; then
    echo "Virtual environment not found at $(pwd)/.venv -- run pip install -r requirements.txt first."
    read -rp "Press Enter to close this window..."
    exit 1
fi

source .venv/bin/activate
# Deliberately `python3 cleanup.py tui`, NOT `python3 -m libcleanup.cli tui`:
# energy.py's ProcessPoolExecutor uses Python 3.14's default "forkserver"
# start method, which re-imports the __main__ module in each worker
# process -- a `-m`-style launch gives __main__ a synthetic path with no
# real file behind it, which forkserver can't re-import, crashing with
# "ConnectionResetError" the instant the pool tries to spawn its first
# worker (confirmed root cause of a real crash in the Energy + BPM stage).
# Running the actual cleanup.py file gives __main__ a real, re-importable
# path, which resolves it -- confirmed fixed by direct reproduction.
python3 cleanup.py tui
status=$?

echo
if [ $status -ne 0 ]; then
    echo "cleanup TUI exited with an error (status $status)."
else
    echo "cleanup TUI closed."
fi
read -rp "Press Enter to close this window..."
