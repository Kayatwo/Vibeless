# Vibeless

Vibeless is the maintained foundation fork used by the Dauntless 1.4.4 backport project: the native
server/client runtime, deploy server, metagame and their release tooling. Content ports, package
recipes and the launcher live in a separate repository (Phase 0 decision D2). The 1.4.4 game client
is always supplied by the user and is never part of this repository or its releases.

## Branches and tags

| Ref | Meaning |
| --- | --- |
| `upstream/dauntless-revived` | Read-only mirror of `mixutin/dauntless-revived`, branch `dauntless-revived`. Never commit here. |
| `import/c39bcfa` → `c39bcfa1b1a626c888416ef32f8b420082d7ead7` | The upstream commit the backport lab was built from (v0.0.5, 2026-10-17). |
| `main` | Vibeless integration branch, rooted at `import/c39bcfa`. |
| `reconciled/installed-2026-10-19` → `9eaa6992f6141283738d676ef9bc457b01894c09` | Source equivalent to the backport release installed on 2026-10-19 (runtime `9fe01048…3546`, BackportDev tag `installed-2026-10-19-test-arena`). |

On 2026-10-20, before the repository became public, the Vibeless commits on `main` were rewritten
once to replace a personal author email with a GitHub no-reply address. Contents are unchanged; commit
IDs quoted in records written before then (for example in the BackportDev workspace) refer to the
earlier IDs. This file uses the new ones.

The two tags are recorded here because the session that created the repository could not push
tags. To create them from any clone:

```sh
git tag -a import/c39bcfa c39bcfa1b1a626c888416ef32f8b420082d7ead7 -m "Upstream dauntless-revived commit imported into BackportDev"
git tag -a reconciled/installed-2026-10-19 9eaa6992f6141283738d676ef9bc457b01894c09 -m "Fork source equivalent to the release installed on 2026-10-19"
git push origin import/c39bcfa reconciled/installed-2026-10-19
```

## Changes on `main` (relative to `import/c39bcfa`)

1. `TArray` allocation hardening, SDK delegate assert fix, crash-report script frames, array tests.
2. Backport native features from the installed release: shared creature services, Deepfrost
   Gnasher, Thunderdeep Drask, Test Arena and opt-in reward diagnostics. The parked development
   probes, the developer arena and the deploy server's lab-only arena reset were retired in Phase 2
   (decision D10); they remain in history.
3. Deploy/metagame TypeScript reproducing the installed JavaScript snapshots (see
   Reconciliation below).
4. Overspend refusal on by default; `INVENTORY_REFUSE_OVERSPEND=0` restores upstream clamping.
5. Repository rules: LF storage for text files (`.gitattributes`), Windows CI for SDK-dependent
   tests; Dependabot version updates disabled (dependencies follow the upstream sync policy).
6. Profile root instead of the hard-coded lab path (`ProfileRootPolicy.h`); the lab path itself was
   removed before the repository became public.
7. Adjustable foundation settings (`FoundationSettings.h`).
8. Published-source proxy loader (`VibelessProxy/`), built and tested in CI.
9. Core-only build: MSBuild `/p:VibelessFeatures=0` compiles out every backport, Test Arena,
   diagnostics and parked development call (the core-only 1.4.4 baseline). CI builds it and checks
   that no feature markers remain.
10. Foundation bundle: `tools/vibeless/make-foundation-bundle.mjs`, built by the CI job "Vibeless
    foundation bundle" and kept as the `vibeless-foundation` artifact for 7 days.
11. Feature host (`FeatureHost.h`, `VibelessFeatures.h`, Phase 2): the core claims every detour,
    so no two owners can hook one function; native features subscribe to lifecycle events (client
    tick, server tick, ProcessEvent intercept/before/after) and run only when enabled by
    `DR_FEATURES`. Each process writes `<profile>/reports/runtime-client.json` or
    `runtime-server-<port>.json` listing every feature's state and reason, every hook and its
    owner, and any errors.
12. Hunt tables by composition (Phase 2): the bundled deploy and metagame tables are upstream's
    1.4.4 tables again, plus an empty `hunt_rules.json`. Packages add their hunts, names and session
    rules (for example the Test Arena's one-player, no-behemoth session) through composed tables
    that `DR_TABLES_DIR` points to; the former Test Arena branch in `gameservers.ts` is gone.
13. Multiplayer compatibility contract (Phase 2; `Compatibility.h`, `VibelessCompatibility.h`,
    metagame `controllers/compatibility.ts`): the metagame publishes what a joining client needs at
    `GET /vibeless/requirements` (foundation version, native features, each client .pak with its
    SHA-256, from the composition's `requirements`). At start the client compares that with what it
    has, reports to `POST /vibeless/compatibility`, and on a mismatch names each problem in a message
    box and closes before joining a world. A server with packages refuses matchmaking (Ramsgate
    included) for players without a matching report; a server without packages accepts clients that
    run no features. Compositions made before this change must be composed again.

## Verification status (2026-10-20)

CI on `main` passes the backend suites (deploy, metagame, gateway, content), portable and Windows
native tests, the default and core-only DLL builds, the proxy loader test and the bundle assembly;
the full run on `4f6cee5` succeeded. The OpenBSD server-port job is intermittent on upstream and
Vibeless alike (its VM wrapper reports "ssh exited with code 1"); Vibeless does not ship the OpenBSD
port, so the job is marked `continue-on-error` and no longer decides the run.
First real-game test (2026-10-20, bundle `0.1.0+1f2bbf480c4f`, upstream proxy, fresh accounts): the
features runtime ran the tutorial, Ramsgate, the original Training Grounds, a pursuit hunt and the Test
Arena; the core-only runtime ran the tutorial, Ramsgate and Training Grounds; service logs were clean.
A second session confirmed that progress survives a full restart, and a connection watch saw no TCP
connection from the client, game servers or services to anything but the local PC (UDP and DNS not
observed). The Phase 2 build `f1bb657` (feature host, composed hunt tables) was checked in game on
2026-10-20: the features profile's runtime reports listed its four features and every hook owner with no
errors, the Test Arena session (now a composed hunt rule) spawned, despawned and returned to Ramsgate, and
Training Grounds stayed original; the core profile reported no features, used the bundled tables and ran
Ramsgate and Training Grounds. Not yet verified: backport pursuits on a progressed account, the Vibeless proxy loader in
game, clean-machine and offline installation, and upgrade/rollback.
The compatibility contract was checked in game on 2026-10-20 with bundle `0.1.0+a32fcb4b7765`: a
matching client was reported compatible and entered Ramsgate; with the Test Arena's `.pak` removed the
client named "backport.test-arena 1.0.0: ZZ_TestArena144_P.pak is missing." in its message box and
closed, and with the client check off (`DR_COMPATIBILITY_CHECK=off`) the metagame refused its
matchmaking for the same reason. (An earlier build flagged the internal `hunt-services` feature; fixed
in `63bb307`.)

## Profiles and settings

Vibeless features (backport creatures, Test Arena, diagnostics) run only when the game executable
sits at `<root>/game/Archon/Binaries/Win64/Dauntless-Win64-Shipping.exe` and `<root>` contains
`vibeless-profile.json` or equals `DR_PROFILE_ROOT`. The
profile's client and servers then use `<root>/user/client/` and `<root>/user/server-<port>/` as
UserDir and write diagnostics under `<root>/reports/`. Any other installation runs the upstream
behaviour.

| Environment variable | Read by | Values | Default |
| --- | --- | --- | --- |
| `DR_PROFILE_ROOT` | client, game servers | profile folder (must be the folder the game runs from) | unset |
| `DR_METAGAME_ADDRESS` | game servers | `host:port` of the metagame | `127.0.0.1:61100` |
| `DR_CELL_FUSION_HOURS` | client and game servers (same value) | `1`-`720`, or `retail`/`0` for the cooked 22/46/70 h | `1` |
| `DR_AUTO_PRESS_START` | client | on/off | on |
| `DR_DUALSENSE` | client | on/off | on |
| `INVENTORY_REFUSE_OVERSPEND` | metagame | `0` restores upstream clamping | refuse |
| `DR_FEATURES` | client, game servers (same value) | `none`, or native feature IDs (`hunt-services`, `deepfrost`, `thunderdeep`, `test-arena`, `reward-probes`), comma or space separated; required features are added | every feature except `reward-probes` |
| `DR_COMPATIBILITY_CHECK` | client | `off` keeps the game open on a mismatch (troubleshooting; the server still refuses matchmaking) | on |
| `DR_TABLES_DIR` | deploy, metagame | folder of composed tables (`deploy/player_hunts_table.json`, `deploy/matchmaker_hunts_table.json`, `deploy/hunt_rules.json`, `metagame/hunt_titles.json`); all must be present | bundled 1.4.4 tables |

Packages name the native features they need as capabilities (`native-feature:deepfrost`); the
launcher turns the enabled packages into `DR_FEATURES` and composes their tables into
`DR_TABLES_DIR`. Without a composed folder only the stock 1.4.4 hunts exist, so a profile that
enables a backport package must set both.

The upstream variables (for example `DR_SERVER_MAX_FPS`, `DR_RAMSGATE_POOL`, the metagame's
`STORE*` switches) keep their documented meaning. The Trials schedule rewrite is not a setting:
without it the cooked 1.4.4 schedule ended in March 2021.

## Reconciliation evidence

Native: every tracked BackportDev runtime file equals `UndauntedInternalServer/` after CRLF→LF
normalization. Backend: TypeScript 6.0.3 emit compared with the installed `deploy-dist` and
`metagame-dist`: 117 files byte-identical, both hunt tables equal as parsed JSON (the lab writer had
re-serialized `0.0` as `0`), and `gameservers.js` differing only by calling `node:timers`
`setTimeout` through its import (the same function as the global) and one line break. The native
DLL is not expected to be byte-identical to the installed one (different compiler build and
timestamps); functional equivalence is checked by tests and, separately, by gameplay.

## Updating from upstream

Review upstream before each foundation release. Fetch into `upstream/dauntless-revived`, classify
every new commit (security/fix, foundation feature, public-fleet operation, economy/store change),
cherry-pick fixes onto `main`, and leave economy and pricing changes out unless explicitly chosen.
Run the backend, native and save-compatibility checks on disposable databases, then record the
result in [UPSTREAM-SYNC.md](UPSTREAM-SYNC.md). Installed servers are never updated automatically.

## Releases

A foundation release records the Vibeless commit it was built from (metagame `build-info.json`,
`SOURCE_URL` and release manifest), includes `LICENSE.txt`, `NOTICE.md` and MinHook's license, and
keeps a corresponding source archive ready for anyone who receives it or plays on a server running
it (AGPL-3.0 section 13). Game files and credentials are never included. Until Vibeless has its own
published-source proxy loader (decision D4), the prebuilt `dxgi.dll` from upstream remains a pinned
input whose source is unpublished, so releases are not yet source-complete for that component.
