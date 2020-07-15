#!/usr/bin/env python

import re
import time
import urllib.request
from urllib.error import HTTPError
import time
from lxml.html import parse

footer = ""
def replace_foo(match):
	first_char = match.group(1)
	id = match.group(2)
	replace = first_char + "[#"+id+"][]"

	url = "https://github.com/hrydgard/ppsspp/issues/"+id
	title = None
	while title is None:
		try:
			page = urllib.request.urlopen(url)	
			p = parse(page)
			title = p.find(".//title").text.split('by')[0].split('·')[0].strip()
			title = re.sub(r"\"", r'\\"', title)
		except HTTPError:
			print("Something went wrong, retrying in 10 sec...")
			time.sleep(10)
			pass

	global footer
	addition = "[#"+id+"]: https://github.com/hrydgard/ppsspp/issues/"+id+" \""+title+"\""
	print("Done: " + addition)
	footer += addition+"\n"
	return replace

f = open("README.md", "r+")
cont = f.read()
updated = re.sub(r"([^[])#(\d+)", replace_foo, cont)
f.seek(0)
f.write(updated)
f.write(footer)
f.truncate()
f.close()
