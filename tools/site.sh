#!/bin/sh
# The page's firmware next to it in site/fw, as CI publishes it: the whole image (boot loader, partitions, app), for the
# web installer, M5 Burner and M5 Launcher. After pio run.
set -e
cd "$(dirname "$0")/.."
version="$(git describe --tags --always --dirty)"
mkdir -p site/fw
cp .pio/build/cardputer/firmware.factory.bin site/fw/cardbuddy.bin
printf '{"name": "cardbuddy", "version": "%s", "new_install_prompt_erase": true, "builds": [{"chipFamily": "ESP32-S3", "parts": [{"path": "cardbuddy.bin", "offset": 0}]}]}\n' "$version" >site/fw/manifest.json
echo "site/fw: $(ls site/fw | tr '\n' ' ')"
