# lang

PPSSPP language ini repository

# Thanks for your interest in translating PPSSPP!

* Simply copy *en_US.ini* file to a new ini file with your language code, or use it to update an existing file with that name.
* To see a list of codes, view [this](http://stackoverflow.com/questions/3191664/list-of-all-locales-and-their-short-codes) page.

# Please note, while translating:
* Ampersands `&` on the *RIGHT* side of an equals sign denote an underlined keyboard hotkey.
* The hotkeys are only supported currently in the *DesktopUI* section, however.
* Example: `&File`. This will make it so when you press ALT + F on Windows, it'll open the File menu.

# Tools

* To remove a translation, use the following (where KeyWord is the key):

    find . -type f -print0 | xargs -0 sed -i /^KeyWord/d

* To change a translation key, use something like this:

    find . -type f -print0 | xargs -0 sed -i /^Key/NewKey

* Before you commit, use git diff to check that you don't delete too much
  or some unrelated key with the same prefix.

### Happy translating!
