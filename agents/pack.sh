#!/bin/sh
set -e

VERSION=`echo "{version}" | npx screw-up -- format`
ZIPFILE="../artifacts/agent-rover-agent-win32-$VERSION.zip"

mkdir -p ../artifacts
rm -f $ZIPFILE
zip -9 $ZIPFILE dist/*.exe ../LICENSE ../README_pack.md
