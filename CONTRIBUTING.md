# Contributing

## Bugs

Open an issue with:

- the cs2voice version (Settings, About)
- what you did and what happened
- `%LOCALAPPDATA%\cs2voice\cs2voice.log`
- for a crash, the newest report from `%LOCALAPPDATA%\cs2voice\crashes`
- if it is about one demo, a link to it (FACEIT match or a file host)

## Translations

Each language is one file in `ui/src/lang/`. Copy `en.ts`, translate the values, keep the keys and the `{placeholders}`, and add the language to `LANGS` in `ui/src/i18n.ts`.

## Code

Build steps are in the README. Keep pull requests small and about one thing. Match the style of the code around your change. Run `build.cmd check` before you send it.
