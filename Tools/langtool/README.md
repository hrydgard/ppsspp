# Language ini file processor tool

To install Rust and cargo, [go here](https://www.rust-lang.org/learn/get-started).

To run, with rust installed, change to this Tools/langtool directory, then:

```bash
cargo run
```

(Or just move the binary out of target/ and run it directly).

To see command line usage, type:

```bash
cargo run -- --help
```

## Validating

```bash
cargo run -- validate
```

Checks every translated string in every language file against the English one it came from:
placeholders (`%1`, `%d`, ...) must survive translation, no line breaks or stray quotes, nothing
empty. Exits with a non-zero code if it finds anything, so it can be used as a check in a script.

The same checks run on anything the AI produces, before it gets written to a file.

## Finding missing keys

```bash
cargo run -- find-missing-keys
```

Scans the C++ source for translation lookups whose key isn't in en_US.ini: `T()` calls, settings pages,
config setting labels and their choices, popup choice lists and context menus. Each one is printed with
its section, the English default if the code has one, and where it is. It's a regex scan, so it knows
the patterns in `src/scan.rs` and nothing else. Technical names that are never translated (like
"XInput") show up too.

Keys built at runtime can't be checked. `find-missing-keys --dynamic` lists those lookups instead.

## Importing translations

```bash
cargo run -- import-language sv_SE translations.ini
```

Merges a file laid out like a language file (section headers, `key = translation`, keyed like
en_US.ini) into one language. Each string goes through the same checks as AI output and is written
with a `# AI translated` comment, or `# same as English` if it's identical to the English string.
Keys missing from the target file are reported, not added.

## AI translation

Some commands (`add-new-key-ai`, `add-new-key-value-ai`, `finish-language-with-ai`) use an LLM to
generate translations. Two providers are supported, pick one by setting its API key in the
environment:

* Claude (Anthropic): `ANTHROPIC_API_KEY`
* OpenAI: `OPENAI_API_KEY`

If both are set, Claude is used unless you say otherwise with `--provider openai`. To pick a
specific model, use `--model`, for example:

```bash
cargo run -- --model claude-sonnet-5 finish-language-with-ai sv_SE
cargo run -- --model gpt-4o finish-language-with-ai sv_SE
```

The provider is inferred from the model name, so `--provider` is only needed to disambiguate.

Strings that are deliberately the same as the English one (terms like "Vsync") are marked with a
`# same as English` comment. `finish-language-with-ai` skips those instead of paying to have them
"translated" again on every run, and writes the comment itself when the AI hands the English string
back unchanged. Delete the comment if you disagree with it and it'll be picked up again next time.

To autoformat the code, use:

```bash
cargo fmt
```

or set up your IDE to do that (it's already pre-set-up for VS Code, if you open the langtool folder directly).
