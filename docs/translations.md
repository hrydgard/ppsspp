# Translated UI strings (assets/lang)

**When implementing new UI, translations come last, in their own commit after everything else is
done.** Write the English strings, get the feature built and working, commit that - then stop and ask
the user to check the English wording before translating anything. The English string is what all ~47
languages get derived from, so rewording it afterwards means redoing the whole sweep.

One .ini file per language, keyed by section and key against `assets/lang/en_US.ini`. **Don't hand-edit
the ~47 files**, and don't run the AI commands in `Tools/langtool` either - you can read the call site,
which its fixed prompt can't. Do the translating yourself and let the tool do the file surgery. Run it
from `Tools/langtool`:

1. Work out what the string actually means before translating it: find where it's used in the C++,
   what any `%1`/`%d` placeholders get substituted with, how long it can be without breaking the
   layout, and how neighboring keys are already phrased in each language (that's your style guide).
2. Write a scratch file, one line per language, named after the ini file minus the extension, with
   the English string under `en_US`:
   ```ini
   [Single]
   en_US = Test string
   sv_SE = Teststräng
   lt-LT = Testeilutė
   ```
   No trailing `# comments` on those lines, they'd end up inside the translation. Placeholders have to
   survive verbatim. If you don't know a language well enough, leave it out - a key that's missing from
   a language file falls back to the English string at runtime, which is much better than a confident
   guess. If a language deliberately keeps the English string (a term like "Vsync" that isn't
   translated), do include it with the English text - it gets written with a `# same as English`
   comment, which stops langtool from trying to translate it again on every later run.
3. `cargo run -- import-single <scratch-file> <Section> "<Key>"` writes them all in, including a new
   key in en_US.ini, tagged `# AI translated` except for the en_US line, which is the string the rest
   were translated from. Note it overwrites existing values for that key, so take care with keys that
   already have human translations, and that the section has to exist already - langtool won't create
   one.
4. `cargo run -- validate` at the end, always. It checks that placeholders survived and exits
   non-zero if anything is off.

Optionally follow up with `cargo run -- copy-missing-lines` to give the languages you skipped the
English string as a placeholder. The other mechanical jobs (renaming and moving keys, sorting
sections) are langtool commands too - prefer them over editing the ini files by hand.

