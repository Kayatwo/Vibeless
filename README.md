<p align="center">
  <a href="LICENSE.txt"><img alt="License: AGPL-3.0-only" src="https://img.shields.io/badge/license-AGPL--3.0--only-0D669C?style=flat-square&labelColor=031523"></a>
  <a href="https://github.com/Kayatwo/Vibeless/actions/workflows/ci.yml?query=branch%3Amain"><img alt="CI status on main" src="https://img.shields.io/github/actions/workflow/status/Kayatwo/Vibeless/ci.yml?branch=main&event=push&style=flat-square&labelColor=031523&label=CI"></a>
</p>

# Vibeless

**Vibeless is a maintained fork of [Dauntless Revived](https://github.com/mixutin/dauntless-revived)**,
the private-server revival of Dauntless (the official servers shut down on 30 May 2025). Like its
upstream, it runs the genuine **Dauntless 1.4.4** client against a server you host yourself.

Vibeless works towards a **mod system for 1.4.4**: content such as new creatures, equipment or
destinations ships as separate packages that a server enables per profile, instead of being built
into one combined release. The original 1.4.4 mechanics stay the default. The foundation in this
repository (the native runtime, deploy server, metagame and the tooling around them) provides what
those packages plug into, and checks that a client and a server agree on them before anyone joins.

> **Status: early development.** There are no releases, no launcher and no public server. Test
> builds are produced by CI (see [Building](#building)). Interfaces may change at any time.

Vibeless is an unofficial, non-commercial fan project. It is not affiliated with Phoenix Labs,
Epic Games, or the Dauntless Revived and Undaunted projects, and it is not an official release of
any of them.

## What this repository does not contain

- **No game files.** You need your own copy of the 1.4.4 client (`1.4.4_shipping`, changelist
  239827). Nothing here contains or links to game files.
- **No content packages.** The packaged content developed alongside Vibeless (for example the
  Deepfrost Gnasher and Thunderdeep Drask backports and a private Test Arena) is not published
  here: their native support code is in this repository, their cooked content, tables and recipes
  are not.
- **No credentials or player data.** CI checks every push for keys, databases and game files.

## What Vibeless changes

Relative to Dauntless Revived commit `c39bcfa` (tag `import/c39bcfa`); the full list, with commit
references and settings, is in [VIBELESS.md](VIBELESS.md).

- **Profiles instead of fixed paths.** The native runtime finds its profile from the game's own
  location (`vibeless-profile.json` or `DR_PROFILE_ROOT`) and keeps settings, logs and reports there.
  An unmodified game folder runs plain upstream behaviour.
- **Feature host.** The core owns every native hook. Native features subscribe to lifecycle events
  and run only when a profile enables them (`DR_FEATURES`); each game process writes a report of its
  features, hooks and errors.
- **Hunt tables by composition.** The foundation ships upstream's 1.4.4 tables. Packages add hunts,
  names and session rules through composed tables (`DR_TABLES_DIR`) instead of editing shared files.
- **Multiplayer compatibility contract.** A server publishes what a joining client needs (foundation
  version, native features, content file hashes). The client checks this before it joins and names
  every mismatch; the server refuses matchmaking to clients that do not match.
- **Adjustable inherited behaviour.** Cell fusion time (including the retail 22/46/70 h), automatic
  Press Start and DualSense support are settings; inventory overspend refusal is on by default.
- **Core-only build.** `/p:VibelessFeatures=0` builds the runtime without any feature code: the
  baseline for an unmodified 1.4.4 experience.
- **Reproducible foundation bundle.** CI builds the runtime deterministically and assembles a
  hash-manifested bundle with the services, a pinned Node.js and a source archive.
- **A published-source proxy loader** (`VibelessProxy/`) as a replacement for upstream's prebuilt
  `dxgi.dll` (tested in CI, not yet the default).

Upstream changes are reviewed and adopted selectively; the log and the rules are in
[UPSTREAM-SYNC.md](UPSTREAM-SYNC.md).

## Repository layout

| Path | What it is |
| --- | --- |
| `UndauntedInternalServer/` | Native runtime injected into the 1.4.4 client and its game servers (C++, MSVC). Vibeless adds the profile root, feature host, compatibility check and settings. |
| `UndauntedDeployServer/` | Starts and tracks game-server processes (TypeScript). |
| `UndauntedMetagame/` | Accounts, characters, inventory, matchmaking and the other backend services the client talks to (TypeScript). |
| `VibelessProxy/` | Published-source `dxgi.dll` proxy loader. |
| `tools/vibeless/` | Foundation bundle tool. |
| `UndauntedLauncher/`, `UndauntedGateway/`, `UndauntedDiscordBot/`, `UndauntedContent/`, `deploy/`, `friend-kit/`, `docs/`, `brand/` | Inherited from Dauntless Revived and kept building in CI. They still describe, publish to or download from the upstream project; Vibeless does not ship them. |
| `VIBELESS.md`, `UPSTREAM-SYNC.md`, `TOOLCHAIN.md` | Vibeless records: changes and settings, upstream review log, pinned toolchain. |
| `UPSTREAM-README.md` | Upstream's README, kept for reference. |

The folder names keep upstream's `Undaunted*` prefix so that upstream changes still apply.

## Building

Every push to `main` runs CI ([.github/workflows/ci.yml](.github/workflows/ci.yml)): the backend
test suites, native regression tests on Linux and Windows, the default and core-only runtime builds,
the proxy loader and the **foundation bundle**. The bundle is kept as the `vibeless-foundation`
artifact of the run for 7 days; it contains the runtime DLLs, both proxy loaders, the deploy server
and metagame with their production dependencies, migrations, configuration templates, notices, a
pinned `node.exe`, a source archive of the exact commit and `foundation-manifest.json` with every
file's SHA-256.

To build locally, use the toolchain in [TOOLCHAIN.md](TOOLCHAIN.md): Node.js (version in
`.node-version`) for the services, Visual Studio 2022 with the MSVC toolset for the runtime.
The generated 1.4.4 SDK headers are part of the repository.

## Settings

The foundation is configured through environment variables set per profile (fusion hours, Press
Start, DualSense, enabled features, composed tables, metagame address and others). They are listed
with their defaults in [VIBELESS.md](VIBELESS.md#profiles-and-settings). Upstream's own variables keep
their documented meaning.

## Security

Please report vulnerabilities privately, as described in [SECURITY.md](SECURITY.md).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Changes that belong in upstream code are best proposed to
[Dauntless Revived](https://github.com/mixutin/dauntless-revived) or
[Undaunted](https://github.com/SyST3MDeV/Undaunted) directly.

## Credits

Vibeless exists because of the work of others:

- **[Undaunted](https://github.com/SyST3MDeV/Undaunted)** by gwog (Gregory Morford,
  [SyST3MDeV](https://github.com/SyST3MDeV)), [EisigesEis](https://github.com/EisigesEis) and its other
  contributors: the server-mode DLL, deploy server, metagame and original launcher.
- **[Dauntless Revived](https://github.com/mixutin/dauntless-revived)** by
  [mixutin](https://github.com/mixutin) and its contributors: the fixes, saved progression, parties,
  tooling and documentation this fork starts from.
- **Harmonic's Dauntless 1.4.4 fork**, parts of which Dauntless Revived ports.
- **[MinHook](https://github.com/TsudaKageyu/minhook)** by Tsuda Kageyu (BSD-2-Clause).
- The 1.4.4 SDK headers were generated with **Dumper-7**.

Where each part comes from, and the notices that apply, is recorded in [NOTICE.md](NOTICE.md).
Dauntless is a trademark of its owners; game assets are not part of this project.

## License

GNU Affero General Public License v3.0 only (`AGPL-3.0-only`); see [LICENSE.txt](LICENSE.txt).
If you run a modified version of this server for other people, the AGPL requires you to offer them
the corresponding source code (section 13). The metagame advertises a source address for this purpose
(`SOURCE_URL`, by default this repository).
