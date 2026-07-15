#!/bin/sh
# This file is part of lx, a terminal log viewer and analyzer.
# Copyright (C) 2026 Augustin Jose
#
# SPDX-License-Identifier: GPL-2.0-or-later
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version. This program is distributed in
# the hope that it will be useful, but WITHOUT ANY WARRANTY. See the
# LICENSE file in the repository root for the full license text.
# Build and install lx (binary + man page) on Linux/macOS/other POSIX.
#
#   scripts/install.sh [PREFIX]      (default PREFIX: /usr/local)
#
# Elevates with sudo only when the destination is not writable, and
# refreshes the man database on systems that keep one (Linux).
set -e

cd "$(dirname "$0")/.."
PREFIX="${1:-${PREFIX:-/usr/local}}"

echo "==> building"
make

# test writability on the nearest existing ancestor, so a PREFIX that
# does not exist yet (e.g. ~/.local on a fresh box) works without sudo
probe="$PREFIX"
while [ ! -e "$probe" ]; do
    probe="$(dirname "$probe")"
done

SUDO=""
if [ ! -w "$probe" ] || { [ -d "$PREFIX/bin" ] && [ ! -w "$PREFIX/bin" ]; }
then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
        echo "==> $PREFIX is not writable; using sudo"
    else
        echo "error: $PREFIX is not writable and sudo is unavailable" >&2
        echo "hint: scripts/install.sh \$HOME/.local" >&2
        exit 1
    fi
fi

echo "==> installing to $PREFIX"
$SUDO make install PREFIX="$PREFIX"

# man database update (mandb on Linux; makewhatis on some BSDs; macOS
# indexes man pages on demand and needs nothing)
if command -v mandb >/dev/null 2>&1; then
    echo "==> updating man database"
    $SUDO mandb -q >/dev/null 2>&1 || true
elif command -v makewhatis >/dev/null 2>&1; then
    $SUDO makewhatis "$PREFIX/share/man" >/dev/null 2>&1 || true
fi

echo ""
echo "lx installed: $PREFIX/bin/lx"
echo "manual page:  man lx"
case ":$PATH:" in
    *":$PREFIX/bin:"*) ;;
    *) echo "note: $PREFIX/bin is not in your PATH" ;;
esac
