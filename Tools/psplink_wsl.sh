#!/bin/bash
# Connects a PSP running PSPLink to WSL and starts usbhostfs_pc, so pspsh / gentest.py work from WSL.
# Does steps 10-12 of https://pspdev.github.io/psplink/windows.html from inside WSL, through usbipd-win.
#
# Usage: Tools/psplink_wsl.sh [port]   (default port 3000)
#
# The PSP has to be plugged in with PSPLink running. The first time, Windows has to share the
# device, which needs an administrator: the script asks for that through a UAC prompt. After that
# the binding persists, and the auto-attach it starts re-attaches the PSP after a PSPLink reset.

set -u
PORT=${1:-3000}
PSP_ID=054c:01c9   # "PSP Type B", the device PSPLink presents
USBIPD="/mnt/c/Program Files/usbipd-win/usbipd.exe"
# Windows binaries aren't necessarily on the WSL PATH.
POWERSHELL=/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe
LOGDIR=${TMPDIR:-/tmp}

if [ ! -x "$USBIPD" ]; then
	echo "usbipd-win isn't installed (expected $USBIPD). Get it from https://github.com/dorssel/usbipd-win" >&2
	exit 1
fi

line=$("$USBIPD" list 2>/dev/null | tr -d '\r' | grep -i " $PSP_ID ")
if [ -z "$line" ]; then
	echo "Windows doesn't see the PSP ($PSP_ID). Plug it in and start PSPLink, then run this again." >&2
	exit 1
fi
busid=$(echo "$line" | awk '{print $1}')
echo "PSP at busid $busid: $(echo "$line" | sed 's/  */ /g' | cut -d' ' -f3-)"

case "$line" in
	*"Not shared"*)
		echo "Sharing it with WSL needs an administrator - accept the UAC prompt on the Windows side."
		"$POWERSHELL" -NoProfile -Command "Start-Process -Verb RunAs -Wait -FilePath '$(wslpath -w "$USBIPD")' -ArgumentList 'bind','--busid','$busid'"
		if "$USBIPD" list 2>/dev/null | tr -d '\r' | grep -i " $PSP_ID " | grep -q "Not shared"; then
			echo "Binding failed (UAC declined?)." >&2
			exit 1
		fi
		;;
esac

# --auto-attach stays running and re-attaches whenever the PSP drops off the bus (PSPLink resets do that).
attached() {
	"$USBIPD" list 2>/dev/null | tr -d '\r' | grep -i " $PSP_ID " | grep -q "Attached"
}

# An auto-attach sticks to the busid it started with, so one left over from a session where the PSP
# was in another USB port waits for that port forever. Replace it unless it has the PSP attached.
if pgrep -f "usbipd.exe attach.*$PSP_ID" > /dev/null && ! attached; then
	echo "Replacing a stale auto-attach."
	pkill -f "usbipd.exe attach.*$PSP_ID"
	sleep 1
fi
if pgrep -f "usbipd.exe attach.*$PSP_ID" > /dev/null; then
	echo "An auto-attach is already running."
else
	nohup "$USBIPD" attach --wsl --hardware-id "$PSP_ID" --auto-attach > "$LOGDIR/psplink_usbipd.log" 2>&1 &
	echo "Started auto-attach (log: $LOGDIR/psplink_usbipd.log)."
fi

for i in $(seq 1 30); do
	attached && break
	sleep 0.5
done
if ! attached; then
	echo "The PSP didn't attach to WSL. See $LOGDIR/psplink_usbipd.log." >&2
	exit 1
fi
echo "Attached to WSL."

if pgrep -x usbhostfs_pc > /dev/null; then
	echo "usbhostfs_pc is already running - not starting another."
else
	if ! command -v usbhostfs_pc > /dev/null; then
		echo "usbhostfs_pc isn't on PATH (it comes with the pspdev toolchain)." >&2
		exit 1
	fi
	# It serves its working directory to the PSP as host0:, so start it from where you want that to be.
	nohup usbhostfs_pc -b "$PORT" > "$LOGDIR/usbhostfs_pc.log" 2>&1 &
	echo "Started usbhostfs_pc on port $PORT, serving $(pwd) as host0: (log: $LOGDIR/usbhostfs_pc.log)."
fi

for i in $(seq 1 20); do
	grep -q "Connected to device" "$LOGDIR/usbhostfs_pc.log" 2>/dev/null && break
	sleep 0.5
done
if grep -q "Connected to device" "$LOGDIR/usbhostfs_pc.log" 2>/dev/null; then
	echo "usbhostfs_pc connected. Try: pspsh -p $PORT -e ls"
else
	echo "usbhostfs_pc hasn't reported a connection yet - check $LOGDIR/usbhostfs_pc.log." >&2
	exit 1
fi
