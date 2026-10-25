#!/usr/bin/env python3
# Copies the source list in sources.mk into ffmpeg.vcxproj and UWP/ffmpeg_UWP/ffmpeg_UWP.vcxproj.
# Run it after changing sources.mk.

import os
import re

DIR = os.path.dirname(os.path.abspath(__file__))
# Each project, with the path from it to the sources.
PROJECTS = [('ffmpeg.vcxproj', '..\\ffmpeg'), ('../../UWP/ffmpeg_UWP/ffmpeg_UWP.vcxproj', '..\\..\\ext\\ffmpeg')]


def main():
	with open(os.path.join(DIR, 'sources.mk'), 'r') as f:
		sources = re.findall(r'lib[a-z]+/[A-Za-z0-9_/]+\.c', f.read())

	for name, src in PROJECTS:
		path = os.path.join(DIR, name)
		with open(path, 'r', newline='') as f:
			text = f.read()
		nl = '\r\n' if '\r\n' in text else '\n'
		# Files in different libraries share names (utils.c, options.c), so each library gets its own
		# object directory.
		items = ''.join('    <ClCompile Include="%s\\%s" ObjectFileName="$(IntDir)%s\\" />%s' %
			(src, s.replace('/', '\\'), os.path.dirname(s).replace('/', '\\'), nl) for s in sources)
		group = '  <ItemGroup Label="Sources">' + nl + items + '  </ItemGroup>'
		text, count = re.subn(r'  <ItemGroup Label="Sources">.*?</ItemGroup>', lambda m: group, text, flags=re.S)
		if count != 1:
			raise Exception('No sources ItemGroup in ' + name)
		with open(path, 'w', newline='') as f:
			f.write(text)
		print('%s: %d files' % (name, len(sources)))


if __name__ == '__main__':
	main()
