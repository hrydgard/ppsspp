---
description: Translate a UI string into all the languages in assets/lang, using Tools/langtool
argument-hint: <Section> "<Key>" ["<English string>"]
---

Add and/or translate a PPSSPP UI string.

What was asked for: `$ARGUMENTS`

Work out three things from that, and **say which values you settled on before you touch anything**:

- **Section** - a `[Section]` that exists in `assets/lang/en_US.ini`. Check that it does.
- **Key** - for a new string this is the English text itself, which is how keys are written here; for
  an existing one it is the key as `en_US.ini` spells it, character for character.
- **English string** - only for a new key. If the key is already in `en_US.ini` there is no English
  string to pass, and the job is filling in the languages where it is still untranslated.

The invocation may be `<Section> "<Key>" ["<English string>"]`, or an ordinary sentence naming the
section and the string, or just the string. Read it, don't split it on whitespace and hope: three
words off the front of a sentence are three arbitrary words, and translating those quietly writes
rubbish into 47 files. When the section or key isn't stated outright, find them - grep the C++ for
the string to see which `GetI18NCategory` it belongs to, and `en_US.ini` for whether the key is
already there. Ask only if that leaves it genuinely ambiguous.

Follow the workflow in docs/translations.md. Run langtool from
`Tools/langtool`:

1. **Work out what the string actually means before translating it.** This is the part the tool's
   own AI commands can't do, and the whole reason you're doing this instead of them:
   - Grep the C++ for the key to find the call site. What widget is it? A button, a checkbox
     label, a tooltip, a error message?
   - What do any `%1` / `%d` placeholders get substituted with at that call site?
   - How much room does the UI give it - is a long translation going to be clipped?
   - How are neighbouring keys in the same section already phrased in each language? That's your
     style guide, use it. Formality, terminology, whether English technical terms are kept or
     translated - each language file has already made those choices, so follow them.

   Say briefly what you found before you start translating.

2. Write the translations to a scratch file outside the repo, in this shape:

   ```ini
   [Single]
   en_US = Test string
   sv_SE = Teststräng
   lt-LT = Testeilutė
   ```

   One line per language, named after the ini file minus the extension (`lt-LT`, `he_IL_invert`,
   `zh_TW`, ...), plus an `en_US` line carrying the English string itself if you were given one -
   that's what creates the key in `en_US.ini`. For a key that already exists, that line has to be
   the existing English text character for character: it overwrites `en_US.ini` like any other
   language, so a stray reword there silently changes the source string every other language was
   translated from. Diff `en_US.ini` afterwards to confirm it didn't move.
   No trailing `# comments` on those lines, they'd end up inside the translation. Placeholders like `%1` and `%d` have to appear verbatim in the
   translation, in whatever position the target language needs them.

   **If you don't know a language well enough to be confident, leave it out.** Step 4 gives those
   languages the English string as a placeholder, which is much better than a confident guess that
   nobody in the project can read well enough to catch.

   If a language deliberately keeps the English string (a term like "Vsync" that language doesn't
   translate), that's different from not knowing - include it with the English text. It gets written
   with a `# same as English` comment, which is what stops langtool from trying to translate it
   again on every later run.

3. `cargo run -- import-single <scratch-file> "<Section>" "<Key>"`

   Note this overwrites any existing value for that key, so if the key already had human
   translations, check what you're about to replace first. The section has to exist already.

4. `cargo run -- add-new-key "<Section>" "<Key>"` - **always, every language file ends up with the
   key.** This writes `Key = Key` (plain English, no `# same as English` marker, so it still reads
   as outstanding work) into every language you skipped, and leaves the ones you translated alone.
   A missing key would fall back to the English string at runtime anyway, but then it looks exactly
   like a translated one in the files, and translators can't see what's left to do.

   Don't reach for `copy-missing-lines` to do this - it fills in placeholders repo-wide, and drags
   several hundred lines of unrelated housekeeping (other missing keys, obsolete keys commented out)
   into your diff.

5. `cargo run -- validate` - always, at the end. It must print `Found 0 problems.`

Finally, report which languages you translated and which you skipped and why, and leave the changes
uncommitted for review unless asked otherwise.
