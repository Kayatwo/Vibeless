// Vibeless: hunt tables come from the bundled 1.4.4 tables, or from a profile's composed tables
// when DR_TABLES_DIR is set (packages add their hunts and rules there; see the package spec).
// The directory holds deploy/player_hunts_table.json, deploy/matchmaker_hunts_table.json and
// deploy/hunt_rules.json. A composed directory must be complete: a missing or invalid file stops
// startup with its name instead of silently falling back to the bundled tables. AGPL-3.0-only.
import { readFileSync } from "node:fs";
import path from "node:path";

import BundledPlayerHunts from "../vendor/player_hunts_table.json";
import BundledMatchmakerHunts from "../vendor/matchmaker_hunts_table.json";
import BundledHuntRules from "../vendor/hunt_rules.json";

type RowTable = { Rows: Record<string, any> }[];

// Per player-hunt session rules supplied by packages (e.g. a private one-player destination).
//   MaxPlayers  1..12: the session refuses more players than this
//   Behemoth    "none": start the map without a behemoth
export type HuntRule = { MaxPlayers?: number, Behemoth?: "none" };

export type HuntTableSet = {
    PlayerHunts: RowTable,
    MatchmakerHunts: RowTable,
    Rules: Record<string, HuntRule>,
    Source: string
};

let Loaded: HuntTableSet | undefined;

function ReadComposed(Directory: string, Name: string): unknown {
    const File = path.join(Directory, "deploy", `${Name}.json`);
    try {
        return JSON.parse(readFileSync(File, "utf8").replace(/^﻿/, ""));
    } catch (error) {
        throw new Error(`Composed table ${File} could not be read: ${(error as Error).message}`);
    }
}

function CheckRowTable(Value: unknown, Name: string): RowTable {
    const Rows = Array.isArray(Value) ? (Value[0] as any)?.Rows : undefined;
    if (!Rows || typeof Rows !== "object") throw new Error(`Table ${Name} has no Rows`);
    return Value as RowTable;
}

export function CheckHuntRules(Value: unknown, Name = "hunt_rules"): Record<string, HuntRule> {
    const Rules = (Value as any)?.Rules;
    if (!Rules || typeof Rules !== "object" || Array.isArray(Rules)) throw new Error(`Table ${Name} has no Rules object`);
    for (const [HuntId, Rule] of Object.entries(Rules as Record<string, any>)) {
        if (!Rule || typeof Rule !== "object" || Array.isArray(Rule)) throw new Error(`${Name}: rule ${HuntId} is not an object`);
        for (const Key of Object.keys(Rule)) {
            if (Key !== "MaxPlayers" && Key !== "Behemoth") throw new Error(`${Name}: rule ${HuntId} has unknown field ${Key}`);
        }
        if (Rule.MaxPlayers !== undefined && (!Number.isInteger(Rule.MaxPlayers) || Rule.MaxPlayers < 1 || Rule.MaxPlayers > 12)) {
            throw new Error(`${Name}: rule ${HuntId} MaxPlayers must be an integer from 1 to 12`);
        }
        if (Rule.Behemoth !== undefined && Rule.Behemoth !== "none") throw new Error(`${Name}: rule ${HuntId} Behemoth may only be "none"`);
    }
    return Rules as Record<string, HuntRule>;
}

export function LoadHuntTables(Directory = process.env.DR_TABLES_DIR): HuntTableSet {
    if (!Directory) {
        return {
            PlayerHunts: BundledPlayerHunts as unknown as RowTable,
            MatchmakerHunts: BundledMatchmakerHunts as unknown as RowTable,
            Rules: CheckHuntRules(BundledHuntRules),
            Source: "bundled"
        };
    }
    const Tables: HuntTableSet = {
        PlayerHunts: CheckRowTable(ReadComposed(Directory, "player_hunts_table"), "player_hunts_table"),
        MatchmakerHunts: CheckRowTable(ReadComposed(Directory, "matchmaker_hunts_table"), "matchmaker_hunts_table"),
        Rules: CheckHuntRules(ReadComposed(Directory, "hunt_rules")),
        Source: Directory
    };
    // A rule for a hunt that no table provides points at a package composed without its rows.
    for (const HuntId of Object.keys(Tables.Rules)) {
        if (!Object.prototype.hasOwnProperty.call(Tables.PlayerHunts[0].Rows, HuntId)) {
            throw new Error(`hunt_rules: rule ${HuntId} names a player hunt that is not in player_hunts_table`);
        }
    }
    return Tables;
}

export function HuntTables(): HuntTableSet {
    Loaded ??= LoadHuntTables();
    return Loaded;
}

export function HuntRuleFor(PlayerHuntId: string): HuntRule | undefined {
    const Rules = HuntTables().Rules;
    return Object.prototype.hasOwnProperty.call(Rules, PlayerHuntId) ? Rules[PlayerHuntId] : undefined;
}

export function ResetHuntTablesForTests(Tables?: HuntTableSet) {
    Loaded = Tables;
}
