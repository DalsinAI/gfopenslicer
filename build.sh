#!/bin/sh
# Host build and the tests. The Amiga binary needs m68k-amigaos-gcc, which
# this script does not require.
cd "$(dirname "$0")"
make test
