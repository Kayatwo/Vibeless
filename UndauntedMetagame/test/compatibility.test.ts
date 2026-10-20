import { RemoveTestDb } from "./setup";
import "./authenv";
import "./matchmakingenv";
import { after, before, describe, it } from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { Call, StartApp, StopApp } from "./appclient";
import { GetDb } from "../src/db";
import { userapikeys, users } from "../src/db/schema";
import { HashUserAPIKey } from "../src/controllers/auth";
import { HandlePlayerMatchmaking, ResetMatchmakingForTests } from "../src/controllers/matchmaking";
import {
    CheckRequirements, CompatibilityProblems, LoadRequirements, MatchmakingCompatibilityProblem, NO_REQUIREMENTS,
    ParseClientReport, RecordClientReport, RequirementsDocument, ResetCompatibilityForTests, type Requirements
} from "../src/controllers/compatibility";

// Vibeless multiplayer compatibility contract (Phase 2). Mirrors UndauntedInternalServer/tests/compatibility.cpp.

const A = "a".repeat(64), B = "b".repeat(64), C = "c".repeat(64);
const REQ: Requirements = CheckRequirements({
    foundation: "0.1.0+2abd1ff00000",
    features: ["test-arena", "deepfrost"],
    packages: [
        { id: "backport.test-arena", version: "1.0.0", files: { "ZZ_TestArena144_P.pak": A } },
        { id: "backport.deepfrost-gnasher", version: "1.0.0", files: { "Frost144_Hunt_P.pak": B } },
        { id: "backport.thunderdeep-equipment", version: "1.0.0", files: {} }
    ]
});
const MATCHING = `vibeless-client 1\nfoundation 0.1.4\nfeature deepfrost\nfeature test-arena\nfile Frost144_Hunt_P.pak ${B}\nfile ZZ_TestArena144_P.pak ${A}\n`;

after(async () => {
    ResetCompatibilityForTests();
    await StopApp();
    RemoveTestDb(() => GetDb().$client.close());
});

describe("requirements", () => {
    it("are served in the line format the runtime parses, sorted", () => {
        assert.equal(REQ.foundation, "0.1.0");
        assert.equal(RequirementsDocument(REQ), [
            "vibeless-requirements 1",
            "foundation 0.1.0",
            "feature deepfrost",
            "feature test-arena",
            "package backport.deepfrost-gnasher 1.0.0",
            `file backport.deepfrost-gnasher Frost144_Hunt_P.pak ${B}`,
            "package backport.test-arena 1.0.0",
            `file backport.test-arena ZZ_TestArena144_P.pak ${A}`,
            "package backport.thunderdeep-equipment 1.0.0",
            ""
        ].join("\n"));
        assert.equal(RequirementsDocument(NO_REQUIREMENTS), "vibeless-requirements 1\n");
    });

    it("refuse unsafe or malformed entries", () => {
        const Bad = (Packages: unknown[], Features: unknown[] = []) => () => CheckRequirements({ foundation: null, features: Features, packages: Packages });
        assert.throws(Bad([{ id: "p", version: "1.0.0", files: { "../x.pak": A } }]), /invalid file entry/);
        assert.throws(Bad([{ id: "p", version: "1.0.0", files: { "x.pak": "ABC" } }]), /invalid file entry/);
        assert.throws(Bad([{ id: "p", version: "1.0", files: {} }]), /id, a version and files/);
        assert.throws(Bad([{ id: "p", version: "1.0.0", files: { "x.pak": A } }, { id: "q", version: "1.0.0", files: { "x.pak": B } }]), /listed twice/);
        assert.throws(Bad([], ["bad feature"]), /list of IDs/);
    });

    it("come from the composition when DR_TABLES_DIR is set, and a broken one stops the start", () => {
        assert.equal(LoadRequirements(""), NO_REQUIREMENTS);
        const Dir = fs.mkdtempSync(path.join(os.tmpdir(), "dr-compat-"));
        try {
            assert.throws(() => LoadRequirements(Dir), /composition\.json could not be read/);
            fs.writeFileSync(path.join(Dir, "composition.json"), JSON.stringify({ schema: 0, packages: [] }));
            assert.throws(() => LoadRequirements(Dir), /has no requirements/);
            fs.writeFileSync(path.join(Dir, "composition.json"), "﻿" + JSON.stringify({ requirements: REQ }));
            assert.deepEqual(LoadRequirements(Dir), REQ);
        } finally {
            fs.rmSync(Dir, { recursive: true, force: true });
        }
    });
});

describe("client reports", () => {
    it("parse the runtime's report", () => {
        assert.deepEqual(ParseClientReport(MATCHING.replace(/\n/g, "\r\n")), {
            foundation: "0.1.4", features: ["deepfrost", "test-arena"],
            files: { "Frost144_Hunt_P.pak": B, "ZZ_TestArena144_P.pak": A }
        });
        assert.deepEqual(ParseClientReport("vibeless-client 1\nfoundation 0.1.0\nfile X.pak missing\n").files, { "X.pak": null });
        assert.throws(() => ParseClientReport("hello"), /not a vibeless-client 1 report/);
        assert.throws(() => ParseClientReport("vibeless-client 1\nfile ../x.pak " + A), /line 2/);
        assert.throws(() => ParseClientReport("vibeless-client 1\nrun calc.exe"), /unexpected entry/);
    });

    it("are judged with the runtime's rules and sentences", () => {
        assert.deepEqual(CompatibilityProblems(REQ, ParseClientReport(MATCHING)), []);
        const Problems = CompatibilityProblems(REQ, {
            foundation: "0.2.0", features: ["deepfrost", "thunderdeep"],
            files: { "Frost144_Hunt_P.pak": C, "ZZ_TestArena144_P.pak": null }
        });
        assert.deepEqual(Problems, [
            "The server runs Vibeless 0.1.0; this game has Vibeless 0.2.0.",
            'The server uses the native feature "test-arena", which this game does not run.',
            'This game runs the native feature "thunderdeep", which the server does not use.',
            "backport.test-arena 1.0.0: ZZ_TestArena144_P.pak is missing.",
            "backport.deepfrost-gnasher 1.0.0: Frost144_Hunt_P.pak is a different version."
        ]);
        // A server without packages accepts a core client, not one that runs features.
        assert.deepEqual(CompatibilityProblems(NO_REQUIREMENTS, { foundation: "0.1.0", features: [], files: {} }), []);
        assert.equal(CompatibilityProblems(NO_REQUIREMENTS, { foundation: "0.1.0", features: ["deepfrost"], files: {} }).length, 1);
    });
});

describe("matchmaking", () => {
    it("is unchanged on a server without packages", async () => {
        ResetCompatibilityForTests(NO_REQUIREMENTS);
        ResetMatchmakingForTests();
        assert.equal(MatchmakingCompatibilityProblem("UID-core"), undefined);
        assert.equal(await HandlePlayerMatchmaking("ISLAND", "", "Hunt_Compat_A", "UID-core"), true);
    });

    it("refuses players without a matching report on a server with packages", async () => {
        ResetCompatibilityForTests(REQ);
        ResetMatchmakingForTests();
        assert.match(MatchmakingCompatibilityProblem("UID-silent")!, /no Vibeless compatibility report/);
        assert.equal(await HandlePlayerMatchmaking("ISLAND", "", "Hunt_Compat_B", "UID-silent"), false);

        RecordClientReport("UID-wrong", ParseClientReport("vibeless-client 1\nfoundation 0.1.0\n"));
        assert.match(MatchmakingCompatibilityProblem("UID-wrong")!, /incompatible client: .*test-arena/);
        assert.equal(await HandlePlayerMatchmaking("ISLAND", "", "Hunt_Compat_B", "UID-wrong"), false);

        RecordClientReport("UID-good", ParseClientReport(MATCHING));
        assert.equal(MatchmakingCompatibilityProblem("UID-good"), undefined);
        assert.equal(await HandlePlayerMatchmaking("ISLAND", "", "Hunt_Compat_B", "UID-good"), true);

        // A later, mismatched report (another game copy for the same account) replaces the good one.
        RecordClientReport("UID-good", ParseClientReport("vibeless-client 1\nfoundation 0.1.0\n"));
        assert.notEqual(MatchmakingCompatibilityProblem("UID-good"), undefined);
    });
});

describe("routes", () => {
    const KEY = `UUK_${"13579bdf02468ace".repeat(3)}`;

    before(async () => {
        GetDb().insert(users).values({ userId: "UID-compat", name: "CompatPlayer", notes: 0 }).run();
        GetDb().insert(userapikeys).values({ userId: "UID-compat", keyHash: HashUserAPIKey(KEY) }).run();
        await StartApp();
    });

    it("GET /vibeless/requirements needs no account", async () => {
        ResetCompatibilityForTests(REQ);
        const Reply = await Call("GET", "/vibeless/requirements");
        assert.equal(Reply.status, 200);
        assert.equal(Reply.text, RequirementsDocument(REQ));
    });

    it("POST /vibeless/compatibility records the caller's verdict", async () => {
        ResetCompatibilityForTests(REQ);
        const Post = (Body: string, Key?: string) => Call("POST", "/vibeless/compatibility", {
            headers: { "content-type": "text/plain", ...(Key ? { "x-undaunted-user-api-key": Key } : {}) }, raw: Body
        });
        assert.equal((await Post(MATCHING)).status, 401);
        const Bad = await Post("nonsense", KEY);
        assert.equal(Bad.status, 400);
        const Wrong = await Post("vibeless-client 1\nfoundation 0.1.0\n", KEY);
        assert.equal(Wrong.status, 200);
        assert.equal(Wrong.json.compatible, false);
        assert.ok(Wrong.json.problems.length >= 3);
        assert.notEqual(MatchmakingCompatibilityProblem("UID-compat"), undefined);
        const Good = await Post(MATCHING, KEY);
        assert.deepEqual(Good.json, { compatible: true, problems: [] });
        assert.equal(MatchmakingCompatibilityProblem("UID-compat"), undefined);
    });
});
