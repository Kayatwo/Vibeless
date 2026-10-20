import './setup';
import { RemoveDeployTestDir } from './deployenv';
import assert from 'node:assert/strict';
import { after, test } from 'node:test';
import { EventEmitter } from 'node:events';
import PlayerTable from '../src/vendor/player_hunts_table.json';
import MatchmakerTable from '../src/vendor/matchmaker_hunts_table.json';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { GetTrainingDojoConnectionDetails, ResetGameserversForTests, Startup, StartupGameserverWithHuntIdAndPlayers, UseProcessFunctionsForTests } from '../src/controllers/gameservers';
import { CheckHuntRules, LoadHuntTables, ResetHuntTablesForTests } from '../src/controllers/hunttables';

let args: string[] = [];
UseProcessFunctionsForTests({ Spawn: (_command, argv) => {
    args = argv;
    return Object.assign(new EventEmitter(), { pid: 42000, unref() {} }) as any;
}, IsAlive: () => true });
after(() => { UseProcessFunctionsForTests({}); ResetGameserversForTests(); ResetHuntTablesForTests(); RemoveDeployTestDir(); });

test('every enabled catalog hunt supplies a map, matchmaker hunt and the complete party roster', async () => {
    let checked = 0;
    for (const [hunt, row] of Object.entries(PlayerTable[0].Rows) as [string, any][]) {
        const ids = row.MatchmakerHuntIDs.map((r: any) => r.RowName);
        if (!ids.length || ids.some((id: string) => !(id in MatchmakerTable[0].Rows))) continue;
        ResetGameserversForTests();
        await Startup();
        const players = ['UID-a', 'UID-b'];
        await StartupGameserverWithHuntIdAndPlayers(hunt, players);
        assert.match(args[2], /^\/Game\//, hunt);
        assert.ok(args[4] && args[4] !== 'NO_MM_HUNTID', hunt);
        assert.equal(args[5], players.map(player => `${player}:${hunt}`).join(','), hunt);
        checked++;
    }
    assert.ok(checked > 100, `only ${checked} catalog hunts checked`);
});

// Vibeless: the core tables are upstream's 1.4.4 tables; packages add hunts and rules by composition.
test('the bundled tables carry no package hunts or rules', () => {
    const tables = LoadHuntTables('');
    assert.equal(tables.Source, 'bundled');
    assert.deepEqual(tables.Rules, {});
    for (const id of Object.keys(tables.PlayerHunts[0].Rows)) assert.doesNotMatch(id, /Frost144|Thunderdeep144|TestArena144/);
});

function StockHunt() {
    return Object.entries(PlayerTable[0].Rows as Record<string, any>).find(([, row]) =>
        row.MatchmakerHuntIDs.length && row.MatchmakerHuntIDs.every((r: any) => r.RowName in MatchmakerTable[0].Rows))![0];
}

function WriteComposed(rules: unknown, extraRows: Record<string, unknown> = {}) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'dr-tables-'));
    fs.mkdirSync(path.join(dir, 'deploy'));
    const players = structuredClone(PlayerTable) as any;
    Object.assign(players[0].Rows, extraRows);
    fs.writeFileSync(path.join(dir, 'deploy', 'player_hunts_table.json'), JSON.stringify(players));
    fs.writeFileSync(path.join(dir, 'deploy', 'matchmaker_hunts_table.json'), '\uFEFF' + JSON.stringify(MatchmakerTable));
    fs.writeFileSync(path.join(dir, 'deploy', 'hunt_rules.json'), JSON.stringify(rules));
    return dir;
}

test('a composed hunt rule makes a private one-player session without a behemoth', async () => {
    const hunt = StockHunt();
    const dir = WriteComposed({ Rules: { [hunt]: { MaxPlayers: 1, Behemoth: 'none' } } });
    try {
        ResetHuntTablesForTests(LoadHuntTables(dir));
        ResetGameserversForTests();
        await Startup();
        await assert.rejects(StartupGameserverWithHuntIdAndPlayers(hunt, ['UID-a', 'UID-b']), /one player per private session/);
        await assert.rejects(StartupGameserverWithHuntIdAndPlayers(hunt, []), /one player per private session/);
        await StartupGameserverWithHuntIdAndPlayers(hunt, ['UID-a']);
        assert.equal(args[3], 'NO_BEHEMOTH');
        assert.equal(args[5], `UID-a:${hunt}`);
    } finally {
        ResetHuntTablesForTests();
        fs.rmSync(dir, { recursive: true, force: true });
    }
});

test('a larger party limit is enforced with its number', async () => {
    const hunt = StockHunt();
    const dir = WriteComposed({ Rules: { [hunt]: { MaxPlayers: 2 } } });
    try {
        ResetHuntTablesForTests(LoadHuntTables(dir));
        ResetGameserversForTests();
        await Startup();
        await assert.rejects(StartupGameserverWithHuntIdAndPlayers(hunt, ['UID-a', 'UID-b', 'UID-c']), /at most 2 players/);
        await StartupGameserverWithHuntIdAndPlayers(hunt, ['UID-a', 'UID-b']);
        assert.notEqual(args[3], 'NO_BEHEMOTH');
    } finally {
        ResetHuntTablesForTests();
        fs.rmSync(dir, { recursive: true, force: true });
    }
});

test('a composed table directory must be complete and valid', () => {
    const dir = WriteComposed({ Rules: {} });
    try {
        fs.rmSync(path.join(dir, 'deploy', 'matchmaker_hunts_table.json'));
        assert.throws(() => LoadHuntTables(dir), /matchmaker_hunts_table\.json could not be read/);
    } finally { fs.rmSync(dir, { recursive: true, force: true }); }
    const orphan = WriteComposed({ Rules: { Missing144_PlayerHunt: { MaxPlayers: 1 } } });
    try {
        assert.throws(() => LoadHuntTables(orphan), /Missing144_PlayerHunt names a player hunt that is not in player_hunts_table/);
    } finally { fs.rmSync(orphan, { recursive: true, force: true }); }
});

test('hunt rules accept only known, bounded fields', () => {
    assert.deepEqual(CheckHuntRules({ Rules: { A: { MaxPlayers: 1, Behemoth: 'none' } } }), { A: { MaxPlayers: 1, Behemoth: 'none' } });
    assert.throws(() => CheckHuntRules({}), /no Rules object/);
    assert.throws(() => CheckHuntRules({ Rules: { A: { MaxPlayers: 0 } } }), /1 to 12/);
    assert.throws(() => CheckHuntRules({ Rules: { A: { MaxPlayers: 1.5 } } }), /1 to 12/);
    assert.throws(() => CheckHuntRules({ Rules: { A: { Behemoth: 'gnasher' } } }), /only be "none"/);
    assert.throws(() => CheckHuntRules({ Rules: { A: { Free: true } } }), /unknown field Free/);
});

test('concurrent Training requests share a world with catalog rules and no fixed player roster', async () => {
    ResetGameserversForTests();
    const replies = await Promise.all(Array.from({length: 12}, () => GetTrainingDojoConnectionDetails()));
    assert.equal(new Set(replies.map(reply => reply.port)).size, 1);
    assert.match(args[2], /\?game=.*BPGM_ArchonTrainingGrounds_C\?MaxPlayers=12$/);
    assert.equal(args[4], 'CR19_MatchmakerHunt_ShatteredIsles_TrainingDojo');
    assert.equal(args[5], 'NO_EXPECTED_PLAYERS');
});
