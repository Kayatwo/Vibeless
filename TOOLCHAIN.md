# Vibeless toolchain and build inputs

Recorded 2026-10-19 (Phase 1). "Lab" means the BackportDev workspace that built the installed
release; "CI" means `.github/workflows/ci.yml` on GitHub-hosted runners.

## Native runtime (`UndauntedInternalServer`)

| Input | Lab (installed release) | CI | Status |
| --- | --- | --- | --- |
| Compiler | MSVC 14.39.33519, VS2022 Community | `windows-latest` image: `cl` 19.51.36260 (MSVC 14.51), toolset v143 (recorded 2026-10-19) | Hosted runners cannot pin MSVC. Every bundle records its `cl` version in `foundation-manifest.json` and a CI annotation; the lab's 14.39 build is not byte-comparable. |
| Windows SDK | 10.0.22621.0 | Runner default (`WindowsTargetPlatformVersion` 10.0) | Same as above. |
| Language | `stdcpplatest` (project); tests `/std:c++20` | Same project; tests `/std:c++20` (g++ for portable tests) | Pinned by source. |
| MinHook | Vendored, BSD-2-Clause (2009-2017) | Same files | Pinned by source. |
| Generated SDK | 4,148 files from Dumper-7 plus the `Basic.hpp` assert fix | Tracked in `UndauntedInternalServer/SDK` | **Pin:** LF-normalized aggregate `6884c491043d44e810e6a2c13c2c65797b2e9be9a6d0536853bb4f8e22f4bc7e` (sorted path, NUL, lowercase file SHA-256, LF), 63,849,377 bytes normalized. BackportDev's byte pin `41cb7207…629c` is the same content with CRLF. |

The DLL is not expected to be byte-identical across compilers or builds; a release records its own
DLL hash, compiler version and source commit. Observed 2026-10-19: commits `a204889` and `f5ff5a5`
have identical native sources, yet CI produced different DLL hashes (`0ea84484…` vs `fdd54412…`)
and proxy hashes on the same compiler. Since `56fddc3` the Release x64 build uses `/Brepro` and
`/PDBALTPATH:%_PDB%`: two clean builds in the Server DLL job and the independent build in the bundle
job (another runner) all gave `f89cb8ba861ccf1c4e127e6224fa93f31c3d8572ff6b589aca1c4608dd18ae40`.
CI now fails if two clean builds differ. This holds for the same compiler version; a different
MSVC still produces a different DLL. The proxy also builds with `/Brepro`; its cross-job hash is
not compared yet.

## Backend (`UndauntedDeployServer`, `UndauntedMetagame`)

| Input | Value | Status |
| --- | --- | --- |
| Lockfiles | `package-lock.json` v3 in each package | Pinned. |
| TypeScript | 6.0.3 (lockfile) | Pinned; the reconciliation gate used this version. |
| Key runtime packages | express 5.2.1, drizzle-orm 0.45.2, better-sqlite3 12.11.1 (native addon), pino 10.3.1, jsonwebtoken 9.0.3; metagame also ws 8.22.0, ltx 3.1.2, express-rate-limit 8.7.0 | Pinned by lockfile. |
| Node.js | **Bundles: exactly 24.21.0** (`/.node-version`, used by the bundle job; npm 11.19.0 as shipped with it). Other CI jobs: major 24. Lab: system Node, version not recorded; its service runner needs Node 20.12 or later. | Pinned for bundles. Profiles should run the bundle with the same Node release. |
| Files read at runtime from the metagame's working directory | `src/drizzle` migrations, `store-images/`, `package.json` (version), `.env` | Present in this repository; the foundation bundle must carry them (BackportDev currently reads them from an external lab folder). |

## Content build (BackportDev, not this repository)

The Test Arena writer targets .NET `net8.0` (SDK version not pinned yet) with locked NuGet packages
`UAssetAPI` 1.1.0 and `CUE4Parse` 1.2.1, currently restored from a lab-only local feed together
with a lab copy of `zlib-ng2.dll`. These belong to the content repository's own toolchain record.

## Inputs users supply

The Dauntless 1.4.4 client (`Dauntless-Win64-Shipping.exe` SHA-256
`d3d41e614908d2befd518b27046d9822d6130ef12ba3504babbdb786bef9cff4`) and its base packages. They are
never stored in this repository or its releases.
