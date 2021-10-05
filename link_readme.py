#!/usr/bin/env python
# -*- coding: utf-8 -*-

import re
from time import sleep
from urllib.request import urlopen
from urllib.error import HTTPError
from lxml.html import parse

footer_delimiter = "\n\n[comment]: # (LINK_LIST_BEGIN_HERE)\n"
footer = ""
linked_id = []

def add_bracket(match):
	first_char = match.group(1)
	id = match.group(2)
	replace = first_char + "[#"+id+"]"
	return replace

def add_link(match):
	id = match.group(1)
	replace = "#" + id
	if id in linked_id:
		return replace

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
	linked_id.append(id)
	print("Linked: " + addition)
	return replace

def already_added_id(match):
	linked_id.append(match.group(1))
	return "[#" + match.group(1) + "]:"

f = open("README.md", "r+")
cont = f.read()

# We don't want to match issues id in title so stop before the link list
d = cont.find(footer_delimiter)
if (d != -1):
	footer = cont[d + len(footer_delimiter):]
	cont = cont[0 : d]
	re.sub(r"\[#(\d+)\]:", already_added_id, footer)

# Add brackets if missing
added_bracket = re.sub(r"([^[])#(\d+)", add_bracket, cont)

# Add links if missing
updated = re.sub(r"#(\d+)", add_link, added_bracket)

f.seek(0)
f.write(updated)
f.write(footer_delimiter)
f.write(footer)
f.truncate()
f.close()
