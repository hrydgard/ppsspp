#!/bin/bash
# Connects the PSPs running PSPLink to WSL and starts a usbhostfs_pc for each, so pspsh / gentest.py
# work from WSL. Does steps 10-12 of https://pspdev.github.io/psplink/windows.html from inside WSL,
# through usbipd-win.
#
# Usage: run it from pspautotests: ../Tools/psplink_wsl.sh
#
# Handles one or two PSPs. The first bridge is on port 3000 and serves the current directory as
# host0:. The second is on port 4000 and serves tests/, so the two consoles' output files don't
# collide; gentest.py -p 4000 and tests/net/adhoc/pair/record.py expect that layout. Which PSP gets
# which port isn't up to us, so at the end it prints each port's firmware.
#
# The PSPs have to be plugged in with PSPLink running. The first time, Windows has to share each
# one, which needs an administrator: the script asks for that through a UAC prompt. After that the
# binding persists, and the auto-attach it starts re-attaches a PSP after a PSPLink reset.

set -u
PSP_ID=054c:01c9   # "PSP Type B", the device PSPLink presents
PORTS=(3000 4000)
USBIPD="/mnt/c/Program Files/usbipd-win/usbipd.exe"
# Windows binaries aren't necessarily on the WSL PATH.
POWERSHELL=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
LOGDIR=${TMPDIR:-/tmp}

if [ ! -x "$USBIPD" ]; then
	echo "usbipd-win isn't installed (expected $USBIPD). Get it from https://github.com/dorssel/usbipd-win" >&2
	exit 1
fi
if ! command -v usbhostfs_pc > /dev/null || ! command -v pspsh > /dev/null; then
	echo "usbhostfs_pc or pspsh isn't on PATH (they come with the pspdev toolchain)." >&2
	exit 1
fi

psp_lines() {
	"$USBIPD" list 2>/dev/null | tr -d '\r' | grep -i " $PSP_ID "
}
state_of() {
	psp_lines | awk -v b="$1" '$1 == b' | grep -q "$2"
}

mapfile -t busids < <(psp_lines | awk '{print $1}')
if [ ${#busids[@]} -eq 0 ]; then
	echo "Windows doesn't see a PSP ($PSP_ID). Plug it in and start PSPLink, then run this again." >&2
	exit 1
fi
if [ ${#busids[@]} -gt ${#PORTS[@]} ]; then
	echo "Found ${#busids[@]} PSPs, only connecting the first ${#PORTS[@]}."
	busids=("${busids[@]:0:${#PORTS[@]}}")
fi
echo "PSPs at busid: ${busids[*]}"

roots=("$(pwd)")
if [ ${#busids[@]} -gt 1 ]; then
	if [ ! -d tests ]; then
		echo "Two PSPs: run this from pspautotests, the second bridge serves its tests/ directory." >&2
		exit 1
	fi
	roots+=("$(pwd)/tests")
fi

for b in "${busids[@]}"; do
	if state_of "$b" "Not shared"; then
		echo "Sharing $b with WSL needs an administrator - accept the UAC prompt on the Windows side."
		"$POWERSHELL" -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '$(wslpath -w "$USBIPD")' -ArgumentList 'bind','--busid','$b'"
		if state_of "$b" "Not shared"; then
			echo "Binding $b failed (UAC declined?)." >&2
			exit 1
		fi
	fi
done

# One auto-attach per busid: it stays running and re-attaches whenever that PSP drops off the bus
# (PSPLink resets do that). One that waits for a busid no PSP is at any more (the PSP moved to another
# USB port), or an older one by hardware ID (which only follows one PSP), is replaced.
while read -r pid cmd; do
	b=$(echo "$cmd" | sed -n 's/.*--busid \([^ ]*\).*/\1/p')
	if [ -z "$b" ] || [[ ! " ${busids[*]} " == *" $b "* ]]; then
		echo "Stopping a stale auto-attach: $cmd"
		kill "$pid"
	fi
done < <(pgrep -af "usbipd.exe attach" | grep -- "--auto-attach")
for b in "${busids[@]}"; do
	if pgrep -f "usbipd.exe attach.*--busid $b " > /dev/null; then
		echo "Auto-attach for $b is already running."
	else
		nohup "$USBIPD" attach --wsl --busid "$b" --auto-attach >> "$LOGDIR/psplink_usbipd_$b.log" 2>&1 &
		echo "Started auto-attach for $b (log: $LOGDIR/psplink_usbipd_$b.log)."
	fi
done

for b in "${busids[@]}"; do
	for i in $(seq 1 30); do
		state_of "$b" "Attached" && break
		sleep 0.5
	done
	if ! state_of "$b" "Attached"; then
		echo "$b didn't attach to WSL. See $LOGDIR/psplink_usbipd_$b.log." >&2
		exit 1
	fi
done
echo "Attached to WSL."

# The directory a running bridge serves: its first root argument, or else its working directory.
bridge_root() {
	local args a root="" skip=0
	mapfile -d '' -t args < "/proc/$1/cmdline"
	for a in "${args[@]:1}"; do
		if [ $skip = 1 ]; then
			skip=0
			continue
		fi
		case "$a" in
			-b|-p|-f|-t) skip=1 ;;
			-*) ;;
			*) root=$a; break ;;
		esac
	done
	if [ -z "$root" ]; then
		readlink "/proc/$1/cwd"
	else
		(cd "/proc/$1/cwd" && realpath "$root")
	fi
}

# Start the bridges one at a time: each claims whichever attached PSP no other bridge has.
for i in "${!busids[@]}"; do
	port=${PORTS[$i]}
	root=${roots[$i]}
	log="$LOGDIR/usbhostfs_pc_$port.log"
	pid=$(pgrep -f "usbhostfs_pc -b $port( |$)" | head -n 1)
	if [ -n "$pid" ]; then
		have=$(bridge_root "$pid")
		if [ "$have" = "$root" ]; then
			echo "usbhostfs_pc is already running on port $port."
			continue
		fi
		echo "Restarting usbhostfs_pc on port $port, which serves $have instead of $root."
		kill "$pid"
		sleep 1
	fi
	(cd "$root" && nohup usbhostfs_pc -b "$port" > "$log" 2>&1 &)
	echo "Started usbhostfs_pc on port $port, serving $root as host0: (log: $log)."
	for j in $(seq 1 20); do
		grep -q "Connected to device" "$log" 2>/dev/null && break
		sleep 0.5
	done
	if ! grep -q "Connected to device" "$log" 2>/dev/null; then
		echo "usbhostfs_pc on port $port hasn't reported a connection yet - check $log." >&2
		exit 1
	fi
done

for i in "${!busids[@]}"; do
	port=${PORTS[$i]}
	ver=$(timeout 10 pspsh -p "$port" -e pspver 2>/dev/null | tr -d '\r' | sed -n 's/^Version: \([0-9.]*\).*/\1/p')
	echo "Port $port: firmware ${ver:-not answering}, host0: is ${roots[$i]}"
done
