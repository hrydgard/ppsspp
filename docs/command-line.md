# Command-line parsing

All command-line parsing for both the main app and the headless build belongs in `Core/CmdLine.cpp` /
`Core/CmdLine.h` (`CommandLineOptions`), not in the platform entry points (`Windows/main.cpp`,
`headless/Headless.cpp`, `UI/NativeApp.cpp`, etc.). Don't re-parse `argv` in those files - add a field
to `CommandLineOptions` instead.

## Declaring an option

Most options are declared in the `g_autoParams` table in `CmdLine.cpp` as:

```cpp
{offsetof(...), type, longName, shortName, docString, mode}
```

`mode` gates the option to `CmdLineMode::Application`, `::Headless`, or `::Both` (the default if the
field is omitted from the initializer).

The same long name can be reused for both modes with different types and meanings, because a given
`Parse()` call only matches params whose mode is `Both` or equal to the current mode. `--log` is the
example to know: a `String` "log to FILE" option in Application mode, a `Bool` "full log output" option
in Headless mode. They don't collide.

Options that can be repeated (e.g. `--ignore TESTNAME`, collected into a `std::vector<std::string>`), or
that don't fit the generic single-value table, need manual handling in the `else if` chain inside
`CommandLineOptions::Parse()` - the same way `--graphics=` and the `boot` filenames are handled.

## Getting a parsed option into the emulator

`ApplyToConfig()` is where parsed options are pushed into `g_Config` / `g_logManager`. Prefer wiring a
new option through there, so every platform gets it for free, rather than reading `CommandLineOptions`
fields ad-hoc at each call site.

`NativeInit()` in `UI/NativeApp.cpp` still takes `argc`/`argv`, because several platform entry points
pass them in, but it shouldn't read them directly: by the time `NativeInit()` runs,
`CommandLineOptions` should already hold everything.
