// Vibeless multiplayer compatibility contract (Phase 2). AGPL-3.0-only.
//
// The profile's composition (DR_TABLES_DIR/composition.json, written by the package composer) says
// what a joining client needs: the foundation version, the native features its packages use and each
// client-side .pak with its SHA-256. This module serves that as GET /vibeless/requirements, checks the
// state clients report to POST /vibeless/compatibility, and lets matchmaking refuse a player whose
// last report does not match. A server without packages has no requirements and accepts any client
// that runs no features. The rules mirror UndauntedInternalServer/Compatibility.h.
import fs from "node:fs";
import path from "node:path";

export type RequiredPackage = { id: string, version: string, files: Record<string, string> };
export type Requirements = { foundation: string | null, features: string[], packages: RequiredPackage[] };
export type ClientState = { foundation: string, features: string[], files: Record<string, string | null> };

const ID = /^[A-Za-z0-9._-]{1,96}$/;
const PAK = /^[A-Za-z0-9._-]{1,124}\.pak$/;
const SHA256 = /^[0-9a-f]{64}$/;
const VERSION = /^\d+\.\d+\.\d+$/;

export const NO_REQUIREMENTS: Requirements = { foundation: null, features: [], packages: [] };

export function CheckRequirements(Value: unknown, Source = "requirements"): Requirements {
    const Req = Value as any;
    if (!Req || typeof Req !== "object") throw new Error(`${Source}: not an object`);
    const Foundation = Req.foundation == null ? null : String(Req.foundation).split("+")[0];
    if (Foundation != null && !VERSION.test(Foundation)) throw new Error(`${Source}: foundation must be a version`);
    if (!Array.isArray(Req.features) || Req.features.some((F: unknown) => typeof F !== "string" || !ID.test(F))) {
        throw new Error(`${Source}: features must be a list of IDs`);
    }
    if (!Array.isArray(Req.packages)) throw new Error(`${Source}: packages must be a list`);
    const Names = new Set<string>();
    for (const Pkg of Req.packages) {
        if (!Pkg || !ID.test(Pkg.id ?? "") || !VERSION.test(Pkg.version ?? "") || !Pkg.files || typeof Pkg.files !== "object") {
            throw new Error(`${Source}: every package needs an id, a version and files`);
        }
        for (const [Name, Hash] of Object.entries(Pkg.files)) {
            if (!PAK.test(Name) || Name.includes("..") || typeof Hash !== "string" || !SHA256.test(Hash)) {
                throw new Error(`${Source}: ${Pkg.id} has an invalid file entry ${Name}`);
            }
            if (Names.has(Name)) throw new Error(`${Source}: ${Name} is listed twice`);
            Names.add(Name);
        }
    }
    return { foundation: Foundation, features: [...Req.features].sort(), packages: Req.packages };
}

// Without DR_TABLES_DIR the server runs the stock tables and has no requirements. With it, the
// composition must carry requirements (composer from Phase 2 on); a missing or broken file stops start.
export function LoadRequirements(Directory = process.env.DR_TABLES_DIR): Requirements {
    if (!Directory) return NO_REQUIREMENTS;
    const File = path.join(Directory, "composition.json");
    let Value: any;
    try {
        Value = JSON.parse(fs.readFileSync(File, "utf8").replace(/^﻿/, ""));
    } catch (error) {
        throw new Error(`Composition ${File} could not be read: ${(error as Error).message}`);
    }
    if (Value?.requirements == undefined) throw new Error(`Composition ${File} has no requirements (compose it again)`);
    return CheckRequirements(Value.requirements, File);
}

export function HasRequirements(Req: Requirements) {
    return Req.features.length > 0 || Req.packages.length > 0;
}

export function RequirementsDocument(Req: Requirements): string {
    const Lines = ["vibeless-requirements 1"];
    if (Req.foundation) Lines.push(`foundation ${Req.foundation}`);
    for (const Feature of Req.features) Lines.push(`feature ${Feature}`);
    for (const Pkg of [...Req.packages].sort((A, B) => A.id.localeCompare(B.id))) {
        Lines.push(`package ${Pkg.id} ${Pkg.version}`);
        for (const [Name, Hash] of Object.entries(Pkg.files).sort(([A], [B]) => A.localeCompare(B))) Lines.push(`file ${Pkg.id} ${Name} ${Hash}`);
    }
    return Lines.join("\n") + "\n";
}

export function ParseClientReport(Text: string): ClientState {
    const Lines = String(Text).split(/\r?\n/);
    if (Lines[0] !== "vibeless-client 1") throw new Error("not a vibeless-client 1 report");
    if (Lines.length > 600) throw new Error("report too long");
    const State: ClientState = { foundation: "", features: [], files: {} };
    for (const [Index, Line] of Lines.slice(1).entries()) {
        if (Line.length === 0) continue;
        const Fields = Line.split(" ");
        const Bad = (Why: string) => new Error(`line ${Index + 2}: ${Why}`);
        if (Fields[0] === "foundation" && Fields.length === 2 && VERSION.test(Fields[1])) State.foundation = Fields[1];
        else if (Fields[0] === "feature" && Fields.length === 2 && ID.test(Fields[1])) State.features.push(Fields[1]);
        else if (Fields[0] === "file" && Fields.length === 3 && PAK.test(Fields[1]) && (Fields[2] === "missing" || SHA256.test(Fields[2]))) {
            State.files[Fields[1]] = Fields[2] === "missing" ? null : Fields[2];
        } else throw Bad("unexpected entry");
    }
    return State;
}

function MajorMinor(Version: string){
    return Version.split(".").slice(0, 2).join(".");
}

// Same rules and sentences as Compatibility::Check in the runtime.
export function CompatibilityProblems(Req: Requirements, Client: ClientState): string[] {
    const Problems: string[] = [];
    if (!HasRequirements(Req) && Client.features.length === 0) return Problems;
    if (Req.foundation && MajorMinor(Req.foundation) !== MajorMinor(Client.foundation)) {
        Problems.push(`The server runs Vibeless ${Req.foundation}; this game has Vibeless ${Client.foundation || "(unknown)"}.`);
    }
    for (const Feature of Req.features) {
        if (!Client.features.includes(Feature)) Problems.push(`The server uses the native feature "${Feature}", which this game does not run.`);
    }
    for (const Feature of [...new Set(Client.features)].sort()) {
        if (!Req.features.includes(Feature)) Problems.push(`This game runs the native feature "${Feature}", which the server does not use.`);
    }
    for (const Pkg of Req.packages) {
        for (const [Name, Hash] of Object.entries(Pkg.files)) {
            const Found = Client.files[Name];
            if (Found == undefined) Problems.push(`${Pkg.id} ${Pkg.version}: ${Name} is missing.`);
            else if (Found !== Hash) Problems.push(`${Pkg.id} ${Pkg.version}: ${Name} is a different version.`);
        }
    }
    return Problems;
}

// ---- per-player state (in memory: clients report on every start) ----

type Verdict = { Problems: string[], At: number };
const Verdicts = new Map<string, Verdict>();
const MAX_PLAYERS_TRACKED = 10000;
let Loaded: Requirements | undefined;

export function ServerRequirements(): Requirements {
    Loaded ??= LoadRequirements();
    return Loaded;
}

export function RecordClientReport(UserId: string, Client: ClientState, Req = ServerRequirements()): string[] {
    const Problems = CompatibilityProblems(Req, Client);
    Verdicts.delete(UserId);
    if (Verdicts.size >= MAX_PLAYERS_TRACKED) Verdicts.delete(Verdicts.keys().next().value!);
    Verdicts.set(UserId, { Problems, At: Date.now() });
    return Problems;
}

// Why this player may not be matched now, or undefined. Only servers with requirements check.
export function MatchmakingCompatibilityProblem(UserId: string, Req = ServerRequirements()): string | undefined {
    if (!HasRequirements(Req)) return undefined;
    const Verdict = Verdicts.get(UserId);
    if (Verdict == undefined) return "no Vibeless compatibility report from this client (it needs the server's packages)";
    if (Verdict.Problems.length > 0) return `incompatible client: ${Verdict.Problems.join(" ")}`;
    return undefined;
}

export function ResetCompatibilityForTests(Req?: Requirements) {
    Verdicts.clear();
    Loaded = Req;
}
