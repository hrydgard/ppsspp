# Patching files from a script

Most edits should go through an editor tool that does exact string replacement. This document is about
the cases where you reach for a script instead - a repetitive change across many files, say - and the
two ways that silently corrupts a diff on Windows.

The short versions of both live in `AGENTS.md` (General instructions, rules 5 and 6). This is the detail.

## Line endings

Which ending a file has depends on where it was checked out. Markdown, `.cpp` and most sources are
stored in git with LF, and a Windows checkout converts them to CRLF on the way out; a Linux checkout
leaves them alone. So the same file - `.vcxproj`, `.vcxproj.filters`, `android/jni/Android.mk`,
`libretro/Makefile.common`, `AGENTS.md`, much of the source - is CRLF in one working copy and LF in
another. Don't hardcode either, and don't "fix" a file's endings to match what a doc claims.

If you patch one with a script, read **and** write with `newline=''`, which keeps whatever was there.
Reading with Python's default universal-newline translation and writing with `newline=''` silently
converts the whole file, turning a two-line addition into a 5000-line diff.

Check `git diff --stat` before committing - a whole-file rewrite is obvious there and invisible in the
editor.

## Backslashes in a bash heredoc

**On Windows, don't put a backslash in a heredoc at all.** The Bash tool hands the command to Git
Bash as a `bash -c` argument. The MSYS runtime builds bash's `argv` from the Windows command line, and
in doing so turns `\\` into `\`, before bash parses anything. So a quoted delimiter (`<<'PY'`), which
normally suppresses all substitution, doesn't help, and neither do single quotes. Bash itself is not
at fault: the same script run from a file (`bash file.sh`, `python file.py`) keeps every backslash.
The script Python receives is not the one you wrote:

| You write in the heredoc | Python actually sees | Result |
|---|---|---|
| `"\r\n"` | `"\r\n"` | fine - survives, because you *want* Python to interpret it |
| `"\\n"` (intending a literal `\n` in the output) | `"\n"` | **a real newline is written into the file** |
| `'foo \\\r\n'` as a match anchor | `'foo \<CR><LF>'` | **anchor silently doesn't match**, reported as "anchor missing" |

The tell for the first case is a compiler error like `C2001: newline in string literal`; the second case
produces no error at all, just a patch that quietly did nothing. Both are invisible in the heredoc you
wrote.

A lone backslash like the one in `"\r\n"` survives, but a rule of "only some backslashes are safe" has
proven too easy to misjudge (matching a `\` line continuation in a makefile went wrong exactly this
way), so `.claude/hooks/check_heredoc_backslash.py` refuses any Windows Bash command that has both a
heredoc and a backslash. Instead, either:

- use an exact-string-replacement editor tool instead (no shell in the path - the best option for the
  common case of "insert a few lines of C++ that contain `\n`"), or
- write the script to a file and run `python thescript.py`, or
- build the backslash as `chr(92)` so no literal backslash appears in the heredoc at all.

Note this is about the *Python source*, not the data: reading and rewriting a CRLF file with
`newline=''` and `\r\n` anchors, from a script file, is the normal way to patch files here.
