#!/bin/sh
# copy scripts into export/scripts and zip the delivery
cd "$(dirname "$0")/.."
rm -rf export/scripts; mkdir -p export/scripts/build export/scripts/tools
cp build/*.py build/*.sh export/scripts/build/
cp tools/*.py export/scripts/tools/
rm -f export/*.blend1
rm -f Octavia1U_Body_Interior_v2.zip
cd export && zip -qr ../Octavia1U_Body_Interior_v2.zip . -x "*.blend1" "*__pycache__*"
