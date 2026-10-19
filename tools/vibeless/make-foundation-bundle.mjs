// Assembles a Vibeless foundation bundle: one folder that a profile installs as a whole.
// No dependencies beyond Node and npm. Run from the repository root after building:
//   - UndauntedDeployServer and UndauntedMetagame: npm ci && npm run build
//   - UndauntedInternalServer: Release x64 (UndauntedInternalServer.dll)
//   - optionally VibelessProxy/build.cmd (candidate proxy loader)
//
//   node tools/vibeless/make-foundation-bundle.mjs --out <empty folder> --dll <UndauntedInternalServer.dll>
//        [--proxy <Vibeless dxgi.dll>] [--core-dll <core-only UndauntedInternalServer.dll>]
//        [--node <node.exe to ship as runtime/node.exe>] [--msvc "<cl version>"]
//
// The bundle never contains game files, credentials, databases or a filled-in .env.
import { createHash } from "node:crypto";
import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

const UPSTREAM_DXGI_SHA256 = "9a431d7b6fd20c43fa92bebd91c3bc023ec7a3fcbc52871c41f4df293d4b0d1f";
const GAME_EXE_SHA256 = "d3d41e614908d2befd518b27046d9822d6130ef12ba3504babbdb786bef9cff4";

const args = Object.fromEntries(process.argv.slice(2).reduce((pairs, value, index, all) => {
    if (value.startsWith("--")) pairs.push([value.slice(2), all[index + 1]]);
    return pairs;
}, []));
const root = process.cwd();
if (!fs.existsSync(path.join(root, "VIBELESS-VERSION"))) throw new Error("Run from the Vibeless repository root");
if (!args.out || !args.dll) throw new Error("Usage: --out <empty folder> --dll <UndauntedInternalServer.dll> [--proxy <dxgi.dll>] [--msvc <version>]");
const out = path.resolve(args.out);
if (fs.existsSync(out) && fs.readdirSync(out).length) throw new Error(`Output folder is not empty: ${out}`);

const sha256 = (file) => createHash("sha256").update(fs.readFileSync(file)).digest("hex");
const run = (command, commandArgs, cwd = root) => execFileSync(command, commandArgs, { cwd, encoding: "utf8", stdio: ["ignore", "pipe", "inherit"], shell: process.platform === "win32" && command === "npm" }).trim();
const copy = (from, to) => fs.cpSync(path.join(root, from), path.join(out, to), { recursive: true, errorOnExist: true, force: false });

const commit = run("git", ["rev-parse", "HEAD"]);
const dirty = run("git", ["status", "--porcelain"]).length > 0;
const version = fs.readFileSync(path.join(root, "VIBELESS-VERSION"), "utf8").trim();
fs.mkdirSync(out, { recursive: true });

// Native runtime and proxy loader
fs.mkdirSync(path.join(out, "native"), { recursive: true });
fs.copyFileSync(path.resolve(args.dll), path.join(out, "native", "UndauntedInternalServer.dll"));
const upstreamDxgi = path.join(root, "UndauntedLauncher", "assets", "dxgi.dll");
if (sha256(upstreamDxgi) !== UPSTREAM_DXGI_SHA256) throw new Error("Upstream dxgi.dll differs from its pin");
fs.copyFileSync(upstreamDxgi, path.join(out, "native", "dxgi.dll"));
if (args.proxy) {
    fs.mkdirSync(path.join(out, "native", "candidate-proxy"), { recursive: true });
    fs.copyFileSync(path.resolve(args.proxy), path.join(out, "native", "candidate-proxy", "dxgi.dll"));
}
if (args["core-dll"]) {
    fs.mkdirSync(path.join(out, "native", "core-only"), { recursive: true });
    fs.copyFileSync(path.resolve(args["core-dll"]), path.join(out, "native", "core-only", "UndauntedInternalServer.dll"));
}

// Backend services: compiled output plus production dependencies from the lockfile
for (const [source, target, extra] of [
    ["UndauntedDeployServer", "deploy", []],
    ["UndauntedMetagame", "metagame", ["src/drizzle", "store-images"]]
]) {
    if (!fs.existsSync(path.join(root, source, "dist", "server.js"))) throw new Error(`${source} is not built (npm run build)`);
    for (const item of ["dist", "package.json", "package-lock.json", ...extra]) copy(path.join(source, item), path.join(target, item));
    run("npm", ["ci", "--omit=dev", "--no-audit", "--no-fund"], path.join(out, target));
    // Command shims are for npm scripts, not for running the service, and differ per platform.
    fs.rmSync(path.join(out, target, "node_modules", ".bin"), { recursive: true, force: true });
}
if (!fs.existsSync(path.join(out, "metagame", "dist", "build-info.json"))) throw new Error("metagame build-info.json missing");

// Configuration templates (filled in per profile by setup; never shipped filled in)
fs.mkdirSync(path.join(out, "config"), { recursive: true });
copy("UndauntedDeployServer/.env.example", "config/deploy.env.example");
copy("UndauntedMetagame/.env.example", "config/metagame.env.example");
fs.writeFileSync(path.join(out, "config", "vibeless.env.example"), [
    "# Vibeless foundation settings (see VIBELESS.md). The same values must reach the client and game servers.",
    "# DR_PROFILE_ROOT=",
    "# DR_METAGAME_ADDRESS=127.0.0.1:61100",
    "# DR_CELL_FUSION_HOURS=1        # 1-720, or retail",
    "# DR_AUTO_PRESS_START=on",
    "# DR_DUALSENSE=on",
    "# INVENTORY_REFUSE_OVERSPEND=1  # 0 restores upstream clamping",
    `# SOURCE_URL must offer this bundle's source/vibeless-${commit}.tar.gz (AGPL-3.0 section 13).`,
    ""
].join("\n"));

// Notices and corresponding source
fs.mkdirSync(path.join(out, "licenses"), { recursive: true });
for (const file of ["LICENSE.txt", "NOTICE.md", "VIBELESS.md", "TOOLCHAIN.md"]) copy(file, path.join("licenses", file));
const minhook = fs.readFileSync(path.join(root, "UndauntedInternalServer", "MinHook", "MinHook.h"), "utf8");
fs.writeFileSync(path.join(out, "licenses", "MinHook-LICENSE.txt"), minhook.slice(0, minhook.indexOf("*/") + 2) + "\n");
// Node runtime, so a profile does not depend on the machine's Node (better-sqlite3 is built for it)
if (args.node) {
    const nodeExe = path.resolve(args.node);
    const nodeVersion = execFileSync(nodeExe, ["--version"], { encoding: "utf8" }).trim();
    const pinned = "v" + fs.readFileSync(path.join(root, ".node-version"), "utf8").trim();
    if (nodeVersion !== pinned) throw new Error(`Node ${nodeVersion} is not the pinned ${pinned}`);
    fs.mkdirSync(path.join(out, "runtime"), { recursive: true });
    fs.copyFileSync(nodeExe, path.join(out, "runtime", path.basename(nodeExe)));
    const nodeLicense = path.join(path.dirname(nodeExe), "LICENSE");
    if (fs.existsSync(nodeLicense)) fs.copyFileSync(nodeLicense, path.join(out, "licenses", "Node.js-LICENSE.txt"));
}

fs.mkdirSync(path.join(out, "source"), { recursive: true });
const archive = path.join(out, "source", `vibeless-${commit}.tar.gz`);
run("git", ["archive", "--format=tar.gz", `--prefix=vibeless-${commit}/`, "-o", archive, "HEAD"]);

// Manifest: every file with its SHA-256, plus what produced it
const files = {};
const walk = (folder) => {
    for (const entry of fs.readdirSync(folder, { withFileTypes: true })) {
        const full = path.join(folder, entry.name);
        if (entry.isSymbolicLink()) throw new Error(`Link in bundle: ${full}`);
        if (entry.isDirectory()) walk(full);
        else files[path.relative(out, full).split(path.sep).join("/")] = sha256(full);
    }
};
walk(out);
const manifest = {
    schema: 1,
    name: "vibeless-foundation",
    version: `${version}+${commit.slice(0, 12)}${dirty ? ".dirty" : ""}`,
    source: { repository: "https://github.com/Kayatwo/Vibeless", commit, dirty, archive: `source/vibeless-${commit}.tar.gz` },
    upstream: { repository: "https://github.com/mixutin/dauntless-revived", base: "c39bcfa1b1a626c888416ef32f8b420082d7ead7" },
    builtAt: new Date().toISOString(),
    toolchain: { node: process.version, npm: run("npm", ["--version"]), msvc: args.msvc ?? "unrecorded" },
    game: { client: "Dauntless 1.4.4 (CL 239827)", exeSha256: GAME_EXE_SHA256, supplied: "by the user; never bundled" },
    proxyLoader: { installed: "native/dxgi.dll", origin: "upstream prebuilt, source unpublished", candidate: args.proxy ? "native/candidate-proxy/dxgi.dll" : null },
    runtimes: { features: "native/UndauntedInternalServer.dll", coreOnly: args["core-dll"] ? "native/core-only/UndauntedInternalServer.dll" : null },
    node: args.node ? { path: "runtime/node.exe", version: execFileSync(path.resolve(args.node), ["--version"], { encoding: "utf8" }).trim() } : null,
    files: Object.fromEntries(Object.entries(files).sort(([a], [b]) => a.localeCompare(b)))
};
fs.writeFileSync(path.join(out, "foundation-manifest.json"), JSON.stringify(manifest, null, 2) + "\n");
console.log(`Foundation bundle ${manifest.version}: ${Object.keys(files).length} files in ${out}`);
