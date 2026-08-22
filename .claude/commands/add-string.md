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

1. If an English string was given above:
   `cargo run -- add-new-key-value "$1" "$2" "$3"`
   That adds the key to every language file with the English text as a placeholder.

2. **Work out what the string actually means before translating it.** This is the part the tool's
   own AI commands can't do, and the whole reason you're doing this instead of them:
   - Grep the C++ for the key to find the call site. What widget is it? A button, a checkbox
     label, a tooltip, a error message?
   - What do any `%1` / `%d` placeholders get substituted with at that call site?
   - How much room does the UI give it - is a long translation going to be clipped?
   - How are neighbouring keys in the same section already phrased in each language? That's your
     style guide, use it. Formality, terminology, whether English technical terms are kept or
     translated - each language file has already made those choices, so follow them.

   Say briefly what you found before you start translating.

3. Write the translations to a scratch file outside the repo, in this shape:

   ```ini
   [Single]
   sv_SE = Teststräng
   lt-LT = Testeilutė
   ```

   One line per language, named after the ini file minus the extension (`lt-LT`, `he_IL_invert`,
   `zh_TW`, ...). No trailing `# comments` on those lines, they'd end up inside the translation.
   Placeholders like `%1` and `%d` have to appear verbatim in the translation, in whatever position
   the target language needs them.

   **If you don't know a language well enough to be confident, leave it out.** The English string
   stays as the fallback, which is normal and fine - much better than a confident guess that nobody
   in the project can read well enough to catch.

4. `cargo run -- import-single <scratch-file> "$1" "$2"`

   Note this overwrites any existing value for that key, so if the key already had human
   translations, check what you're about to replace first.

5. `cargo run -- validate` - always, at the end. It must print `Found 0 problems.`

Finally, report which languages you translated and which you skipped and why, and leave the changes
uncommitted for review unless asked otherwise.
