# PreToolUse hook for the Bash tool: refuse heredocs that contain a backslash on Windows.
#
# On Windows, Claude Code hands the command to Git Bash as a `bash -c` argument, and the MSYS
# runtime's Windows-command-line parsing turns \\ into \ before bash sees it - even inside a quoted
# heredoc (<<'EOF') or single quotes. A Python patch script fed that way silently matches or writes
# the wrong text. Commands run from a file are not affected.

import json
import sys

if sys.platform != "win32":
	sys.exit(0)

try:
	command = json.load(sys.stdin).get("tool_input", {}).get("command", "")
except Exception:
	sys.exit(0)

if "<<" not in command or "\\" not in command:
	sys.exit(0)

reason = (
	"Blocked: this command has a heredoc and a backslash. On Windows the Bash tool loses one level "
	"of backslashes (\\\\ arrives as \\) before bash parses the command, even in quoted heredocs "
	"(<<'EOF') and single quotes, so the script silently gets different text. Use the Edit tool, or "
	"Write the script to a file (e.g. in the scratchpad) and run it with `python file.py`."
)
print(json.dumps({
	"hookSpecificOutput": {
		"hookEventName": "PreToolUse",
		"permissionDecision": "deny",
		"permissionDecisionReason": reason,
	}
}))
