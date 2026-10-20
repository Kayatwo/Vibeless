# Contributing to Vibeless

Thank you for helping. Vibeless is a fork of [Dauntless Revived](https://github.com/mixutin/dauntless-revived);
a change that would help every user of that project is best proposed there (or to
[Undaunted](https://github.com/SyST3MDeV/Undaunted)), and Vibeless picks it up through its upstream
review ([UPSTREAM-SYNC.md](UPSTREAM-SYNC.md)).

## Before you start

1. For anything larger than a small fix, open an issue first that says what you want to change and
   why, so we can agree on the approach.
2. Keep the original 1.4.4 mechanics as the default. Content and gameplay changes belong in
   separately enabled packages or settings, not in the foundation's defaults.
3. Never commit game files, keys, tokens, `.env` files, databases, backups or logs. CI refuses them,
   including in commits a later commit undoes.
4. Security problems: report them privately ([SECURITY.md](SECURITY.md)), not in a public issue.
5. Contributions are licensed under AGPL-3.0-only, like the rest of the repository.

## Pull requests

- One topic per pull request, with a description of what changed, why, and how you tested it.
- Add or update tests for behaviour you change; the native rules that can run without Windows
  (`UndauntedInternalServer/tests`) build with g++ as well as MSVC.
- Record user-visible changes and new settings in [VIBELESS.md](VIBELESS.md).
- Keep upstream files close to upstream where you can, so that later upstream changes still apply.

## Checks

GitHub Actions runs these on every push and pull request ([`ci.yml`](.github/workflows/ci.yml)). Run
the ones your change touches before you push. They need Windows and Node.js 24, except the docs and
hygiene checks, which need only Node.js.

| Check | Run it yourself |
| --- | --- |
| Server packages (`UndauntedMetagame`, `UndauntedGateway`, `UndauntedContent`, `UndauntedDeployServer`): build and unit tests | In the package folder: `npm ci`, `npm run build`, `npm test` |
| Native regression tests (portable) | For each `UndauntedInternalServer/tests/*.cpp` that needs no Windows headers: `g++ -std=c++20 -Wall -Wextra -Werror <test>.cpp -o <test> && ./<test>` |
| Native runtime, default and core-only | In a VS2022 x64 developer prompt: `msbuild UndauntedInternalServer\UndauntedInternalServer.sln /m /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143`, and again with `/p:VibelessFeatures=0` |
| Launcher: typecheck, unit tests, installer | In `UndauntedLauncher`: `npm ci`, `npm run typecheck`, `npm test`, `npm run make` |
| Windows Server kit, in Windows PowerShell 5.1: every script parses, PSScriptAnalyzer finds no errors, and the kit's tests pass | `npm ci` in `UndauntedGateway` first. Then, in PowerShell from the repository root, `powershell -NoProfile -ExecutionPolicy Bypass -File deploy\windows-server\tests\Test-KitUnit.ps1`, and the same for `Test-DeployRemote.ps1`. Last, the same for `Test-Sandbox.ps1` with `-SandboxDir "$env:TEMP\dr-sandbox"` added (a few minutes; it uses loopback ports 62000-62499 only and deletes that folder at the end) |
| Inherited docs site: generated files are current, and the site builds the way GitHub Pages builds it | `node tools/sync-roadmap.js` and `node tools/build-llms.js` must leave `git diff` empty |
| No secrets or game files are committed, not even in a commit that a later one undoes, and the launcher version is valid | `node tools/ci/check-repo.js --history origin/main..HEAD` (without `--history`: the files only) |
