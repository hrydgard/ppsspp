#!/bin/sh
"$(cygpath "$(env | awk -F= '/^ProgramFiles\(x86\)=/ { print $2; }')/Microsoft Visual Studio/Installer/vswhere.exe")" "$@"