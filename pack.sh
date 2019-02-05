#!/bin/bash
rm Nesmp.fw
TITLE="NES MP ($(date +%Y%m%d))"
./mkfw "$TITLE" tile.raw 0 16 2097152 nesmp build/nesemu-go.bin
mv firmware.fw Nesmp.fw
