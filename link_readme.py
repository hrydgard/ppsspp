#!/usr/bin/env python

import re
from time import sleep
from urllib.request import urlopen
from urllib.error import HTTPError
from lxml.html import parse

footer_delimiter = "\n\n[comment]: # (LINK_LIST_BEGIN_HERE)\n"
footer = ""
def replace_foo(match):
	first_char = match.group(1)
	id = match.group(2)
	replace = first_char + "[#"+id+"]"

	url = "https://github.com/hrydgard/ppsspp/issues/"+id
	title = None
	while title is None:
		try:
			p = parse(urlopen(url))
			title = p.find(".//title").text.split('by')[0].split('·')[0].strip()
			title = re.sub(r"\"", r'\\"', title)
		except HTTPError:
			print("Something went wrong, retrying in 10 sec...")
			sleep(10)

	global footer
	addition = "[#"+id+"]: https://github.com/hrydgard/ppsspp/issues/"+id+" \""+title+"\""
	footer += addition+"\n"
	print("Done: " + addition)
	return replace

f = open("README.md", "r+")
cont = f.read()

# We don't want to match issues id in title so stop before the link list
d = cont.find(footer_delimiter)
if (d != -1):
	footer = cont[d + len(footer_delimiter):]
	cont = cont[0 : d]

updated = re.sub(r"([^[])#(\d+)", replace_foo, cont)
f.seek(0)
f.write(updated)
f.write(footer_delimiter)
f.write(footer)
f.truncate()
f.close()
