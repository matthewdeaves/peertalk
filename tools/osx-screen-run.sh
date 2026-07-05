#!/usr/bin/env bash
#
# Run a PeerTalk test app in a VISIBLE Terminal window on an OS X Mac's own
# screen, driven over SSH (osascript). Uses the native POSIX fat binary --
# i.e. only what every OS X version 10.3-10.7 actually supports (BSD sockets),
# so it runs on the Intel mini and every PPC Mac alike.
#
#   usage: osx-screen-run.sh <ssh-host> [app] [remote-dir]
#   e.g.:  osx-screen-run.sh quicksilver test_lifecycle
#
# Assumes the binary is already on the Mac at <remote-dir>/<app> (push it with
# rsync first) and that the same user is logged in at the Mac's console.
# Optionally start a local peer with:  ./build/test_lifecycle --name POSIXHOST
set -euo pipefail

host="${1:?usage: osx-screen-run.sh <ssh-host> [app] [remote-dir]}"
app="${2:-test_lifecycle}"
dir="${3:-/tmp/pt}"
name="$(echo "$host" | tr '[:lower:]' '[:upper:]')"

# Tell Terminal.app (on the Mac's logged-in desktop) to open a window and run
# the app, so it is visible on the Mac's screen -- not just headless over SSH.
ssh -o BatchMode=yes "$host" \
  "osascript \
     -e 'tell application \"Terminal\" to activate' \
     -e 'tell application \"Terminal\" to do script \"$dir/$app --name $name\"'"

echo "Launched $app in a Terminal window on $host's screen (--name $name)."
echo "For a two-peer run, start a local partner:  ./build/$app --name POSIXHOST"
