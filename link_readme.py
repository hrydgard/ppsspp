#!/bin/python

import re
f = open("README.md", "r+")
cont = f.read()
updated = re.sub(r"([^[])#(\d+)", r"\1[#\2](https://github.com/hrydgard/ppsspp/issues/\2)", cont)
f.seek(0)
f.write(updated)
f.truncate()
f.close()
