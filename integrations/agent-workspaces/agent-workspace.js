import { createHash } from "node:crypto";

export default function agentWorkspace(pi) {
  pi.on("tool_call", (event, ctx) => {
    if (event.toolName !== "bash" || typeof event.input.command !== "string") return;
    const session = ctx.sessionManager.getSessionId();
    const agent = ctx.agent.id;
    if (!session || !agent) return { block: true, reason: "Cannot attribute shell to an OMP session/agent" };
    const key = createHash("sha256").update(`${session}\0${agent}`).digest("hex").slice(0, 24);
    // In-process agents share process.env; identity must travel with this call.
    return { input: { ...event.input, command: `export OMP_GUI_KEY=${key}\n${event.input.command}` } };
  });
}
