#!/usr/bin/env python3
# Cross-checks our ARM64 disassembler (Core/Util/DisArm64.cpp) against LLVM's.
#
# Runs the Arm64Disasm unit test with ARM64_DISASM_DUMP set, which makes it print every instruction
# word along with our disassembly, then disassembles the same words with llvm-mc and compares the two
# after normalizing away the syntax differences: our compact vector syntax ("fadd.32 q1, q2, q3" for
# "fadd v1.4s, v2.4s, v3.4s"), absolute branch targets, and a handful of aliases we print differently.
# What's left over is printed, so an empty report means the two agree on mnemonic, registers and
# immediates for every instruction in the test.
#
# Usage: python Tools/arm64_disasm_check.py [path/to/PPSSPPUnitTest] [--llvm-mc=llvm-mc]

import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor


def run_test(exe):
	env = dict(os.environ, ARM64_DISASM_DUMP='1')
	out = subprocess.run([exe, 'Arm64Disasm'], env=env, capture_output=True, text=True).stdout
	rows = []
	for line in out.splitlines():
		if line.startswith('DIS|'):
			_, ln, addr, words, ours, expected, code = line.split('|', 6)
			rows.append((int(ln), int(addr, 16), words.split(), ours.split('; '), code))
	return rows


def llvm_dis(llvm_mc, word):
	w = int(word, 16)
	inp = '0x%02x,0x%02x,0x%02x,0x%02x' % (w & 0xff, (w >> 8) & 0xff, (w >> 16) & 0xff, w >> 24)
	out = subprocess.run([llvm_mc, '--disassemble', '-triple=aarch64', '-mattr=+crc,+v8.2a,+fullfp16'],
		input=inp, capture_output=True, text=True).stdout
	lines = [l.strip() for l in out.splitlines() if l.strip() and not l.strip().startswith('.text')]
	return lines[0].split('//')[0].strip() if lines else 'INVALID'


PCREL = {'b', 'bl', 'cbz', 'cbnz', 'tbz', 'tbnz', 'adr', 'adrp', 'ldr', 'ldrsw', 'prfm'}
CONDS = {'cs': 'hs', 'cc': 'lo'}
ALIASES = {'lslv': 'lsl', 'lsrv': 'lsr', 'asrv': 'asr', 'rorv': 'ror', 'not': 'mvn', 'ins': 'mov', 'umov': 'mov'}


def tokens(text):
	text = text.lower()
	# Arrangements and lane sizes: v1.4s, q2.s[3], { v1.b }[15].
	text = re.sub(r'\b([vqdshb]\d+)\.\d*[bhsdq]\b', r'\1', text)
	text = re.sub(r'\}\.[bhsd]\b', '}', text)
	# Our branch targets are 12 bare hex digits.
	toks = re.findall(r'\b[0-9a-f]{12}\b|[a-z][a-z0-9_.]*|-?0x[0-9a-f]+|-?\d+\.\d+|-?\d+', text)
	if not toks:
		return toks
	# Element sizes on our mnemonics: "fadd.32", "xtn.32.64".
	toks[0] = re.sub(r'(\.\d+)+$', '', toks[0])
	out = [toks[0]]
	for t in toks[1:]:
		if re.fullmatch(r'[0-9a-f]{12}', t):
			t = int(t, 16)
		elif re.fullmatch(r'[vqdshb]\d+', t):
			t = 'v' + t[1:]
		elif re.fullmatch(r'-?0x[0-9a-f]+', t):
			t = int(t, 16)
		elif re.fullmatch(r'-?\d+\.\d+', t):
			t = float(t)
		elif re.fullmatch(r'-?\d+', t):
			t = int(t)
		else:
			t = CONDS.get(t, t)
		out.append(t)
	return out


def signed(value, bits):
	value &= (1 << bits) - 1
	return value - (1 << bits) if value >> (bits - 1) else value


def norm_ours(text):
	t = tokens(text)
	if not t:
		return t
	t[0] = ALIASES.get(t[0], t[0])
	m = t[0]
	if m in ('sxtl', 'uxtl', 'sxtl2', 'uxtl2'):
		t = [('s' if m[0] == 's' else 'u') + 'shll' + m[4:]] + t[1:] + [0]
	elif m in ('movz', 'movn') and len(t) == 4:
		bits = 64 if t[1].startswith('x') else 32
		value = t[2] << t[3]
		if m == 'movn':
			value = ~value
		t = ['mov', t[1], signed(value, bits)]
	elif m == 'movk' and len(t) == 4:
		t = ['movk', t[1], t[2]] + (['lsl', t[3]] if t[3] else [])
	elif m == 'mov' and len(t) == 5 and t[3] in ('lsl', 'lsr', 'asr', 'ror'):
		t = ['orr', t[1], 'xzr' if t[1].startswith('x') else 'wzr'] + t[2:]
	return t


def norm_llvm(text, addr):
	t = tokens(text)
	if not t:
		return t
	if t[0] == 'umov':
		t[0] = 'mov'
	m = t[0]
	# PC-relative operands come out as offsets; ours are absolute.
	if (m in PCREL or m.startswith('b.')) and isinstance(t[-1], int) and re.search(r'#-?\d+\s*$', text):
		if m == 'adrp':
			t[-1] = (addr & ~0xfff) + t[-1]
		elif m in ('ldr', 'ldrsw', 'prfm') and '[' in text:
			pass
		else:
			t[-1] = addr + t[-1]
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
		print('No output from the test - is %s built with the Arm64Disasm test?' % exe)
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
