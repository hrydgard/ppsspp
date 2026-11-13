#!/usr/bin/env python3
# Checks the Visual Studio .vcxproj.filters files for the mistakes hand edits make.
#
# A broken filters file usually still parses, and MSBuild ignores it, so the build stays green while
# Visual Studio's folder tree falls apart. This catches:
# - an item nested inside another item (a merge or paste that lost a closing tag)
# - an item whose filter, or a parent of that filter, isn't defined
# - an item in the filters file that isn't in the project, or the other way around
# - an item listed twice (except wildcards)
#
# Usage: python Tools/check_vcxproj_filters.py [file.vcxproj.filters ...]
# With no arguments, checks every .vcxproj.filters file git knows about.

import os
import subprocess
import sys
import xml.etree.ElementTree as ET

NS = '{http://schemas.microsoft.com/developer/msbuild/2003}'
ITEM_CHILDREN = {NS + 'Filter', NS + 'UniqueIdentifier', NS + 'Extensions', NS + 'ParseFiles', NS + 'SourceControlFiles'}


def tag(el):
	return el.tag[len(NS):] if el.tag.startswith(NS) else el.tag


def items(root):
	for group in root.findall(NS + 'ItemGroup'):
		for item in group:
			yield item


def check(path):
	errors = []
	try:
		root = ET.parse(path).getroot()
	except ET.ParseError as e:
		return ['not valid XML: %s' % e]

	defined = set()
	listed = {}
	for item in items(root):
		kind = tag(item)
		name = item.get('Include')
		for child in item:
			if child.tag not in ITEM_CHILDREN:
				errors.append('%s %s is nested inside %s %s' % (tag(child), child.get('Include'), kind, name))
		if kind == 'Filter':
			defined.add(name.lower())
			continue
		key = (kind, name.lower())
		# Visual Studio writes a wildcard item once per file it matches, so those repeat.
		if key in listed and '*' not in name:
			errors.append('%s %s is listed twice' % (kind, name))
		filt = item.find(NS + 'Filter')
		listed[key] = (name, filt.text if filt is not None else None)

	for name in sorted(defined):
		if '\\' in name and name.rsplit('\\', 1)[0] not in defined:
			errors.append('filter %s has no parent filter defined' % name)

	for (kind, _), (name, filt) in sorted(listed.items()):
		if filt and filt.lower() not in defined:
			errors.append('%s %s is in filter %s, which is not defined' % (kind, name, filt))

	project = path[:-len('.filters')]
	if os.path.exists(project):
		in_project = {}
		for item in items(ET.parse(project).getroot()):
			if item.get('Include') is not None and tag(item) not in ('ProjectConfiguration', 'ProjectReference', 'Filter'):
				in_project[(tag(item), item.get('Include').lower())] = item.get('Include')
		for key in sorted(set(in_project) - set(listed)):
			errors.append('%s %s is in the project but not in the filters file' % (key[0], in_project[key]))
		for key in sorted(set(listed) - set(in_project)):
			errors.append('%s %s is in the filters file but not in the project' % (key[0], listed[key][0]))

	return errors


def main():
	paths = sys.argv[1:]
	if not paths:
		out = subprocess.check_output(['git', 'ls-files', '*.vcxproj.filters'], text=True)
		paths = out.split()
	failed = False
	for path in paths:
		errors = check(path)
		for error in errors:
			print('%s: %s' % (path, error))
		failed = failed or bool(errors)
	if failed:
		sys.exit(1)
	print('Checked %d filters files, all OK.' % len(paths))


if __name__ == '__main__':
	main()
