#!/bin/sh
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
