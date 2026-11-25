#!/usr/bin/env python3
# Splits one WAV holding several sound effects, separated by silence, into one file per effect.
#
#   python Tools/sfx_split.py all.wav 0.1 effect1.wav effect2.wav ...
#   python Tools/sfx_split.py all.wav 0.1 names.txt
#
# The whole file is first normalized by its peak (one gain for everything, so the effects keep
# their relative levels). An effect starts where the level rises above the threshold, and runs on
# until the next effect or until its tail has died out to --floor, so tails quieter than
# the threshold are kept. Gaps shorter than --min-gap don't split an effect. Each effect is cut at
# zero crossings and gets a short fade at each end, so it can't click.
#
# A names file has one output filename per line (blank lines and lines starting with # are
# skipped); relative names in it are relative to the file itself. If the number of effects found
# doesn't match the number of names, nothing is written. A name of - skips that effect.

import argparse
import os
import struct
import sys

import numpy as np

WAVE_FORMAT_PCM = 1
WAVE_FORMAT_IEEE_FLOAT = 3
WAVE_FORMAT_EXTENSIBLE = 0xFFFE


def read_wav(path):
	with open(path, 'rb') as f:
		data = f.read()
	if data[0:4] != b'RIFF' or data[8:12] != b'WAVE':
		raise ValueError('%s is not a WAV file' % path)
	fmt = None
	samples = None
	pos = 12
	while pos + 8 <= len(data):
		chunk_id = data[pos:pos + 4]
		size = struct.unpack('<I', data[pos + 4:pos + 8])[0]
		body = data[pos + 8:pos + 8 + size]
		if chunk_id == b'fmt ':
			tag, channels, rate, _, _, bits = struct.unpack('<HHIIHH', body[:16])
			if tag == WAVE_FORMAT_EXTENSIBLE:
				tag = struct.unpack('<H', body[24:26])[0]
			fmt = (tag, channels, rate, bits)
		elif chunk_id == b'data':
			samples = body
		pos += 8 + size + (size & 1)
	if fmt is None or samples is None:
		raise ValueError('%s has no fmt or data chunk' % path)

	tag, channels, rate, bits = fmt
	if tag == WAVE_FORMAT_IEEE_FLOAT and bits == 32:
		x = np.frombuffer(samples, dtype='<f4').astype(np.float64)
	elif tag == WAVE_FORMAT_IEEE_FLOAT and bits == 64:
		x = np.frombuffer(samples, dtype='<f8').astype(np.float64)
	elif tag == WAVE_FORMAT_PCM and bits == 16:
		x = np.frombuffer(samples, dtype='<i2') / 32768.0
	elif tag == WAVE_FORMAT_PCM and bits == 24:
		b = np.frombuffer(samples[:len(samples) // 3 * 3], dtype=np.uint8).reshape(-1, 3).astype(np.int32)
		v = b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16)
		v = np.where(v >= 0x800000, v - 0x1000000, v)
		x = v / 8388608.0
	elif tag == WAVE_FORMAT_PCM and bits == 32:
		x = np.frombuffer(samples, dtype='<i4') / 2147483648.0
	else:
		raise ValueError('%s: unsupported format (tag %d, %d bits)' % (path, tag, bits))
	frames = len(x) // channels
	return x[:frames * channels].reshape(frames, channels), rate


def write_wav16(path, x, rate):
	pcm = np.clip(np.round(x * 32767.0), -32768, 32767).astype('<i2')
	channels = x.shape[1]
	raw = pcm.tobytes()
	header = b'RIFF' + struct.pack('<I', 36 + len(raw)) + b'WAVE'
	header += b'fmt ' + struct.pack('<IHHIIHH', 16, WAVE_FORMAT_PCM, channels, rate, rate * channels * 2, channels * 2, 16)
	header += b'data' + struct.pack('<I', len(raw))
	with open(path, 'wb') as f:
		f.write(header + raw)


def envelope(x, window):
	# Peak level per block of frames, smeared over the neighbouring block so zero crossings don't
	# read as silence.
	a = np.abs(x).max(axis=1)
	padded = np.concatenate([np.zeros(window // 2), a, np.zeros(2 * window)])
	out = np.empty(len(a))
	for start in range(0, len(a), window):
		out[start:start + window] = padded[start:start + 2 * window].max()
	return out


def find_effects(x, rate, threshold, min_gap, floor, preroll):
	window = max(1, int(rate * 0.005))
	env = envelope(x, window)
	loud = env >= threshold
	if not loud.any():
		return []

	# Runs of loud frames, merging runs separated by less than min_gap.
	edges = np.flatnonzero(np.diff(np.concatenate([[0], loud.astype(np.int8), [0]])))
	runs = list(zip(edges[0::2], edges[1::2]))
	merged = [list(runs[0])]
	for start, end in runs[1:]:
		if start - merged[-1][1] < min_gap * rate:
			merged[-1][1] = end
		else:
			merged.append([start, end])

	quiet = env < floor
	effects = []
	prev_end = 0
	for i, (start, end) in enumerate(merged):
		limit = merged[i + 1][0] if i + 1 < len(merged) else len(x)
		# Back the start up over the attack, but only by preroll: the previous tail may never get
		# quite down to the floor.
		s = start
		lo = max(prev_end, start - int(rate * preroll))
		while s > lo and not quiet[s - 1]:
			s -= 1
		# Let the tail run until it dies out or the next effect begins.
		e = end
		while e < limit and not quiet[e]:
			e += 1
		effects.append((s, e))
		prev_end = e
	return effects


def zero_crossing_before(mono, pos, lo):
	# Last frame at or before pos where the signal crosses zero, or lo if there's none.
	for i in range(pos, lo, -1):
		if mono[i] == 0.0 or (mono[i - 1] < 0.0) != (mono[i] < 0.0):
			return i
	return lo


def zero_crossing_after(mono, pos, hi):
	for i in range(pos, hi - 1):
		if mono[i] == 0.0 or (mono[i] < 0.0) != (mono[i + 1] < 0.0):
			return i + 1
	return hi


def cut_effect(x, start, end, rate, fade_in_ms, fade_ms):
	mono = x.sum(axis=1)
	end = min(end, len(x))
	# Cutting at a crossing of the mono sum; the fade below takes both channels to zero anyway.
	s = zero_crossing_before(mono, start, max(0, start - rate // 100))
	e = zero_crossing_after(mono, end, min(len(x), end + rate // 100))
	y = x[s:e].copy()
	# An effect can start while the previous one is still ringing.
	fade_in = min(len(y), int(rate * fade_in_ms / 1000.0))
	if fade_in > 0:
		y[:fade_in] *= np.linspace(0.0, 1.0, fade_in)[:, None]
	fade = min(len(y), int(rate * fade_ms / 1000.0))
	if fade > 0:
		ramp = 0.5 + 0.5 * np.cos(np.linspace(0.0, np.pi, fade))
		y[-fade:] *= ramp[:, None]
	return y, s, e


def read_names(path):
	base = os.path.dirname(path)
	names = []
	with open(path, 'r', encoding='utf-8') as f:
		for line in f:
			line = line.strip()
			if not line or line.startswith('#'):
				continue
			names.append(line if os.path.isabs(line) else os.path.join(base, line))
	return names


def main():
	parser = argparse.ArgumentParser(description='Split a WAV of sound effects separated by silence into one file per effect.')
	parser.add_argument('input', help='WAV file holding all the effects')
	parser.add_argument('threshold', type=float, help='level (0-1, after normalization) below which audio counts as a gap between effects')
	parser.add_argument('outputs', nargs='+', help='output WAV filenames in order, or a single text file listing them')
	parser.add_argument('--min-gap', type=float, default=0.2, help='shortest gap in seconds that separates two effects (default 0.2)')
	parser.add_argument('--floor', type=float, default=0.003, help='level where a tail counts as silent and the effect ends (default 0.003, -50 dB)')
	parser.add_argument('--preroll', type=float, default=10.0, help='how far before the threshold is crossed an effect may start, in ms (default 10)')
	parser.add_argument('--fade-in', type=float, default=1.0, help='fade-in length at the start of each effect in ms (default 1)')
	parser.add_argument('--fade', type=float, default=5.0, help='fade-out length at the end of each effect in ms (default 5)')
	parser.add_argument('--peak', type=float, default=1.0, help='peak level to normalize the whole file to (default 1.0)')
	args = parser.parse_args()

	if len(args.outputs) == 1 and not args.outputs[0].lower().endswith('.wav'):
		names = read_names(args.outputs[0])
	else:
		names = args.outputs

	x, rate = read_wav(args.input)
	peak = np.abs(x).max()
	if peak == 0.0:
		print('%s is silent' % args.input)
		return 1
	x = x * (args.peak / peak)
	print('%s: %d Hz, %d channels, %.2f s, peak %.3f (gain %.2f dB)' % (
		args.input, rate, x.shape[1], len(x) / rate, peak, 20 * np.log10(args.peak / peak)))

	effects = find_effects(x, rate, args.threshold, args.min_gap, args.floor, args.preroll / 1000.0)
	cuts = [cut_effect(x, s, e, rate, args.fade_in, args.fade) for s, e in effects]
	for i, (y, s, e) in enumerate(cuts):
		name = names[i] if i < len(names) else '-'
		print('  %2d: %6.3f - %6.3f s (%.3f s)  %s' % (i + 1, s / rate, e / rate, (e - s) / rate, name))

	if len(cuts) != len(names):
		print('Found %d effects but got %d names, not writing anything. Try another threshold or --min-gap.' % (len(cuts), len(names)))
		return 1

	written = 0
	for (y, _, _), name in zip(cuts, names):
		if os.path.basename(name) == '-':
			continue
		write_wav16(name, y, rate)
		written += 1
	print('Wrote %d files.' % written)
	return 0


if __name__ == '__main__':
	sys.exit(main())
