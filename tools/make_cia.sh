#!/bin/bash
# Attribution du CIA Roblo3DS : bannertool (banniere + icone) puis makerom.
# Noms cohérents avec le Makefile (TARGET=Roblo3DS) et app.rsf
# (Title=Roblo3DS, ProductCode=CTR-H-RBLX, UniqueId=0xc7a23).
set -e

APP_NAME="Roblo3DS"
APP_TITLE="Roblo3DS"
APP_DESC="Mini Roblox pour Nintendo 3DS"
APP_AUTHOR="Ismahane"
UNIQUE_ID="0xc7a23"
PRODUCT_CODE="CTR-H-RBLX"

mkdir -p build

echo "=== bannertool : banniere (banner.png + audio.wav -> .bnr) ==="
bannertool makebanner -i assets/banner.png -a assets/audio.wav -o build/banner.bnr

echo "=== bannertool : icone (icon.png 48x48 -> .icn) ==="
bannertool makesmdh -i assets/icon.png -s "$APP_DESC" -l "$APP_TITLE $UNIQUE_ID" -p "$APP_AUTHOR" -o build/icon.icn

echo "=== makerom : $APP_NAME.cia ==="
rm -f "$APP_NAME.cia"
makerom -f cia \
    -o "$APP_NAME.cia" \
    -elf "$APP_NAME.elf" \
    -rsf app.rsf \
    -icon build/icon.icn \
    -banner build/banner.bnr \
    -target t \
    -exefslogo

echo "=== OK : $APP_NAME.cia genere ==="
ls -l "$APP_NAME.cia"
