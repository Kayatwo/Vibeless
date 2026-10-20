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
| `reconciled/installed-2026-10-19` → `b6e049ebef73f4e33a136cc8f4af779ded249e79` | Source equivalent to the backport release installed on 2026-10-19 (runtime `9fe01048…3546`, BackportDev tag `installed-2026-10-19-test-arena`). |

The two tags are recorded here because the session that created the repository could not push
tags. To create them from any clone:

```sh
git tag -a import/c39bcfa c39bcfa1b1a626c888416ef32f8b420082d7ead7 -m "Upstream dauntless-revived commit imported into BackportDev"
git tag -a reconciled/installed-2026-10-19 b6e049ebef73f4e33a136cc8f4af779ded249e79 -m "Fork source equivalent to the release installed on 2026-10-19"
git push origin import/c39bcfa reconciled/installed-2026-10-19
```

## Changes on `main` (relative to `import/c39bcfa`)

1. `TArray` allocation hardening, SDK delegate assert fix, crash-report script frames, array tests.
2. Backport native features from the installed release: shared creature services, Deepfrost
   Gnasher, Thunderdeep Drask, Test Arena, opt-in reward diagnostics and parked development
   probes (compiled out). Lab-specific paths are still present and are being replaced by
   configuration.
3. Deploy/metagame TypeScript reproducing the installed JavaScript snapshots (see
   Reconciliation below).
4. Overspend refusal on by default; `INVENTORY_REFUSE_OVERSPEND=0` restores upstream clamping.
5. Repository rules: LF storage for text files (`.gitattributes`), Windows CI for SDK-dependent
   tests; Dependabot version updates disabled (dependencies follow the upstream sync policy).
6. Profile root instead of the hard-coded lab path (`ProfileRootPolicy.h`).
7. Adjustable foundation settings (`FoundationSettings.h`).
8. Published-source proxy loader (`VibelessProxy/`), built and tested in CI.
9. Core-only build: MSBuild `/p:VibelessFeatures=0` compiles out every backport, Test Arena,
   diagnostics and parked development call (the core-only 1.4.4 baseline). CI builds it and checks
   that no feature markers remain.
10. Foundation bundle: `tools/vibeless/make-foundation-bundle.mjs`, built by the CI job "Vibeless
    foundation bundle" and kept as the `vibeless-foundation` artifact for 7 days.

## Verification status (2026-10-19)

CI on `main` passes the backend suites (deploy, metagame, gateway, content), portable and Windows
native tests, the default and core-only DLL builds, the proxy loader test and the bundle assembly;
the full run on `101e7e1` succeeded. The OpenBSD server-port job is intermittent on upstream and
Vibeless alike (its VM wrapper reports "ssh exited with code 1"); Vibeless does not ship the OpenBSD
port, so the job is marked `continue-on-error` and no longer decides the run.
First real-game test (2026-10-20, bundle `0.1.0+f9f1ecbe6bb2`, upstream proxy, fresh accounts): the
features runtime ran the tutorial, Ramsgate, the original Training Grounds, a pursuit hunt and the Test
Arena; the core-only runtime ran the tutorial, Ramsgate and Training Grounds; service logs were clean.
A second session confirmed that progress survives a full restart, and a connection watch saw no TCP
connection from the client, game servers or services to anything but the local PC (UDP and DNS not
observed). Not yet verified: backport pursuits on a progressed account, the Vibeless proxy loader in
game, clean-machine and offline installation, and upgrade/rollback.

## Profiles and settings

Vibeless features (backport creatures, Test Arena, diagnostics) run only when the game executable
sits at `<root>/game/Archon/Binaries/Win64/Dauntless-Win64-Shipping.exe` and `<root>` is the
original BackportDev lab, contains `vibeless-profile.json`, or equals `DR_PROFILE_ROOT`. The
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
