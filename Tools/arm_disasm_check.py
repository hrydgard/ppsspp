#!/usr/bin/env python3
# Cross-checks our ARM32 disassembler (ext/disarm.cpp) against LLVM's.
#
# Runs the ArmDisasm unit test with ARM_DISASM_DUMP set, which makes it print every instruction word
# along with our disassembly, then disassembles the same words with llvm-mc and compares the two after
# normalizing away the syntax differences: case, absolute branch targets, register ranges in lists
# ("{d8-d11}"), the hs/lo condition names and number formatting. What's left over is printed, so an
# empty report means the two agree on mnemonic, data types, registers and immediates for every
# instruction in the test.
#
# Usage: python Tools/arm_disasm_check.py [path/to/PPSSPPUnitTest] [--llvm-mc=llvm-mc]

import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


def run_test(exe):
	env = dict(os.environ, ARM_DISASM_DUMP='1')
	out = subprocess.run([exe, 'ArmDisasm'], env=env, capture_output=True, text=True).stdout
	rows = []
	for line in out.splitlines():
		if line.startswith('DIS|'):
			_, ln, addr, words, ours, expected, code = line.split('|', 6)
			rows.append((int(ln), int(addr, 16), words.split(), ours.split('; '), code))
	return rows


def llvm_dis(llvm_mc, word):
	w = int(word, 16)
	inp = '0x%02x,0x%02x,0x%02x,0x%02x' % (w & 0xff, (w >> 8) & 0xff, (w >> 16) & 0xff, w >> 24)
	out = subprocess.run([llvm_mc, '--disassemble', '-triple=armv7a', '-mattr=+neon,+vfp4,+hwdiv-arm,+fp16,+mp'],
		input=inp, capture_output=True, text=True).stdout
	lines = [l.strip() for l in out.splitlines() if l.strip() and not l.strip().startswith('.text')]
	return lines[0].split('@')[0].strip() if lines else 'INVALID'


CONDS = '(eq|ne|cs|cc|hs|lo|mi|pl|vs|vc|hi|ls|ge|lt|gt|le)?'
BRANCHES = re.compile(r'^(b|bl|blx)%s$' % CONDS)
# Data processing and MSR, where LLVM prints an immediate with a non-canonical rotation as "#imm8, #rot".
ROTATED_IMM = re.compile(r'^(and|eor|sub|rsb|add|adc|sbc|rsc|tst|teq|cmp|cmn|orr|mov|bic|mvn|msr)s?%s$' % CONDS)


def expand_ranges(text):
	def expand(m):
		kind, first, last = m.group(1), int(m.group(2)), int(m.group(3))
		return ', '.join('%s%d' % (kind, i) for i in range(first, last + 1))
	return re.sub(r'\b([sdq])(\d+)-[sdq](\d+)\b', expand, text)


def tokens(text):
	text = expand_ranges(text.lower())
	toks = re.findall(r'[a-z][a-z0-9_.]*|-?0x[0-9a-f]+|-?\d+\.\d+(?:e[+-]\d+)?|-?\d+|[\[\]{}!^]', text)
	if not toks:
		return toks
	toks[0] = re.sub(r'(hs|lo)(?=\.|$)', lambda m: 'cs' if m.group(1) == 'hs' else 'cc', toks[0])
	out = [toks[0]]
	for t in toks[1:]:
		if re.fullmatch(r'-?0x[0-9a-f]+', t):
			t = int(t, 16)
		elif re.fullmatch(r'-?\d+\.\d+(e[+-]\d+)?', t):
			t = float(t)
		elif re.fullmatch(r'-?\d+', t):
			t = int(t)
		if isinstance(t, int) and -(1 << 31) <= t < 0:
			t &= 0xffffffff
		out.append(t)
	return out


def norm_ours(text):
	t = tokens(text)
	# We spell out the IA mode that LLVM leaves implicit.
	if t:
		t[0] = re.sub(r'^(ldm|stm)ia', r'\1', t[0])
	return t


def norm_llvm(text, addr):
	t = tokens(text)
	if not t:
		return t
	m = t[0]
	# Branch offsets are relative to the instruction plus 8; ours are absolute.
	if BRANCHES.match(m) and len(t) == 2 and isinstance(t[1], int):
		t[1] = (addr + 8 + t[1]) & 0xffffffff
	elif ROTATED_IMM.match(m) and re.search(r'#\d+, #\d+$', text):
		imm, rot = t[-2], t[-1]
		t = t[:-2] + [((imm >> rot) | (imm << (32 - rot))) & 0xffffffff]
	elif m == 'vorr' and len(t) == 4 and t[2] == t[3]:
		t = ['vmov', t[1], t[2]]
	elif m.startswith('vext.') and isinstance(t[-1], int):
		# We always print bytes.
		t[-1] *= int(m[5:]) // 8
		t[0] = 'vext.8'
	return t


def main():
	exe = 'build/PPSSPPUnitTest'
	llvm_mc = 'llvm-mc'
	for arg in sys.argv[1:]:
		if arg.startswith('--llvm-mc='):
			llvm_mc = arg.split('=', 1)[1]
		else:
			exe = arg
	rows = run_test(exe)
	if not rows:
		print('No output from the test - is %s built with the ArmDisasm test?' % exe)
		return 1
	words = [w for r in rows for w in r[2]]
	with ThreadPoolExecutor(16) as ex:
		ref = dict(zip(words, ex.map(lambda w: llvm_dis(llvm_mc, w), words)))
	mismatches = 0
	count = 0
	for line, addr, ws, ours, code in rows:
		for i, w in enumerate(ws):
			count += 1
			ours_text = ours[i] if i < len(ours) else ''
			theirs = ref[w]
			if norm_ours(ours_text) != norm_llvm(theirs, addr + i * 4):
				mismatches += 1
				print('line %d: %s  %-40s llvm: %s  (%s)' % (line, w, ours_text, theirs, code))
	print('%d of %d instructions differ from llvm-mc' % (mismatches, count))
	return 1 if mismatches else 0


if __name__ == '__main__':
	sys.exit(main())
