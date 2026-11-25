#!/bin/sh
# Splits source_assets/new_sfx.wav into the UI sound effects. The last effect is skipped.
cd "$(dirname "$0")/.." || exit 1
python Tools/sfx_split.py --peak 0.7 source_assets/new_sfx.wav 0.1 \
	assets/sfx_toggle_on.wav \
	assets/sfx_toggle_off.wav \
	assets/sfx_confirm.wav \
	assets/sfx_back.wav \
	assets/sfx_select.wav \
	-
