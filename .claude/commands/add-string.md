---
description: Translate a UI string into all the languages in assets/lang, using Tools/langtool
argument-hint: <Section> "<Key>" ["<English string>"]
---

Add and/or translate a PPSSPP UI string.

- Section: `$1`
- Key: `$2`
- English string: `$3` - if this is empty, the key already exists in `assets/lang/en_US.ini` and
  you're only filling in the languages where it's still untranslated.

Follow the workflow in AGENTS.md under "Translated UI strings (assets/lang)". Run langtool from
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
   that's what creates the key in `en_US.ini`. No trailing `# comments` on those lines, they'd end
   up inside the translation. Placeholders like `%1` and `%d` have to appear verbatim in the
   translation, in whatever position the target language needs them.

   **If you don't know a language well enough to be confident, leave it out.** A key that's missing
   from a language file falls back to the English string at runtime, which is normal and fine - much
   better than a confident guess that nobody in the project can read well enough to catch.

   If a language deliberately keeps the English string (a term like "Vsync" that language doesn't
   translate), that's different from not knowing - include it with the English text. It gets written
   with a `# same as English` comment, which is what stops langtool from trying to translate it
   again on every later run.

3. `cargo run -- import-single <scratch-file> "$1" "$2"`

   Note this overwrites any existing value for that key, so if the key already had human
   translations, check what you're about to replace first. The section has to exist already.

4. `cargo run -- validate` - always, at the end. It must print `Found 0 problems.`

Finally, report which languages you translated and which you skipped and why, and leave the changes
uncommitted for review unless asked otherwise. If you want the languages you skipped to carry the
English string as a visible placeholder rather than just falling back to it, that's
`cargo run -- copy-missing-lines`.
