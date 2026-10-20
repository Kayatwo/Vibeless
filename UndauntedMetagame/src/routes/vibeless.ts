// Vibeless routes (Phase 2 compatibility contract). AGPL-3.0-only.
import express, { Router } from "express";
import { rateLimit } from "express-rate-limit";
import { HasUndauntedUserApiKey } from "../middleware/HasUndauntedUserApiKey";
import { ParseClientReport, RecordClientReport, RequirementsDocument, ServerRequirements } from "../controllers/compatibility";
import { logger } from "../logger";

export const vibelessRouter = Router();

// What a client needs to join. Public on purpose: it holds package IDs, versions and file hashes only.
vibelessRouter.get("/vibeless/requirements", (_req, res) => {
    res.type("text/plain").send(RequirementsDocument(ServerRequirements()));
});

const ReportRateLimit = rateLimit({
    keyGenerator: (req) => "compat:" + (req as any).UndauntedUserInfo.UserId,
    windowMs: 60000, limit: 10, standardHeaders: "draft-8", legacyHeaders: false, message: { error: "rate_limited" }
});

vibelessRouter.post("/vibeless/compatibility", HasUndauntedUserApiKey, ReportRateLimit,
    express.text({ type: "text/plain", limit: "64kb" }), (req: any, res) => {
        const UserId = req.UndauntedUserInfo.UserId as string;
        let Problems: string[];
        try {
            Problems = RecordClientReport(UserId, ParseClientReport(typeof req.body === "string" ? req.body : ""));
        } catch (error) {
            res.status(400).json({ error: (error as Error).message });
            return;
        }
        if (Problems.length) logger.warn(`Client of ${UserId} is incompatible: ${Problems.join(" ")}`);
        else logger.info(`Client of ${UserId} matches the server's packages`);
        res.json({ compatible: Problems.length === 0, problems: Problems });
    });
