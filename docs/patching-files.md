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

**Don't feed Python to `bash -c` via a heredoc when the code contains backslashes.** The Git Bash /
MinGW layer strips one level of backslash escaping on the way in, *even with a quoted delimiter*
(`<<'PY'`), which normally suppresses all substitution. So the script Python receives is not the one
you wrote:

| You write in the heredoc | Python actually sees | Result |
|---|---|---|
| `"\r\n"` | `"\r\n"` | fine - survives, because you *want* Python to interpret it |
| `"\\n"` (intending a literal `\n` in the output) | `"\n"` | **a real newline is written into the file** |
| `'foo \\\r\n'` as a match anchor | `'foo \<CR><LF>'` | **anchor silently doesn't match**, reported as "anchor missing" |

The tell for the first case is a compiler error like `C2001: newline in string literal`; the second case
produces no error at all, just a patch that quietly did nothing. Both are invisible in the heredoc you
wrote.

The rule: **a heredoc is fine as long as every backslash in the script is one you want Python to
interpret. The moment you need a literal backslash in the *output*, stop.** Then either:

- use an exact-string-replacement editor tool instead (no shell in the path - the best option for the
  common case of "insert a few lines of C++ that contain `\n`"), or
- write the script to a file and run `python thescript.py`, or
- build the backslash as `chr(92)` so no literal backslash appears in the heredoc at all.

Note this is about the *Python source*, not the data: reading and rewriting a CRLF file with
`newline=''` and `\r\n` anchors works fine in a heredoc, and is the normal way to patch files here.
