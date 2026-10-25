---
name: print-advisor-mcp
description: Build or document the MCP sidecar and its connection to the Creality Print Print Advisor module for Claude Code and Codex.
---

Use this skill for MCP tool schemas, stdio server work, local application transport, and client setup documentation.

## Boundaries

- Claude Code or Codex is the MCP host and supplies the LLM. Creality Print contains the domain service and application state, not an LLM integration.
- MCP adapter owns protocol framing and tool schemas. The native Print Advisor owns validation and domain behavior. Do not duplicate profile logic in the adapter.
- The former `MCPChatPanel`/`CxAgentClientBridge` code (Creality's in-app AI chat) was removed in cleanup phase F3; it was an application-side agent integration, never an external MCP server. `SlicerBridge` (`simple/bridge/`) stays and is what the Print Advisor reuses.
- Start with read-only tools. Do not expose a mutation tool until Creality Print can independently verify explicit in-app approval.

## Tool contract

Use narrow, typed schemas and bounded output. Include context/version identifiers, preset identity, units, structured errors, and only settings needed for diagnosis. Define stable errors for app unavailable, stale context, invalid input, unsupported setting, timeout, and approval required. Make requests idempotent where feasible; never silently retry a mutation.

## Local transport and trust

Prefer a client-launched stdio MCP sidecar and a narrow authenticated loopback channel to the running application, unless repository constraints establish a better supported option. Verify available dependencies and current official SDK/client documentation before choosing implementation details. Bind only to loopback, use a short-lived session secret, cap message sizes and timeouts, validate every payload, and shut down cleanly. Do not accept arbitrary file paths or shell commands.

Never include secrets in MCP tool descriptions, prompts, results, or logs. Do not assume client-side tool confirmation is the same as approval in the Creality Print UI.

## Documentation

Document installation, launch command, client configuration, supported client versions, trust implications, verification, and troubleshooting for Claude Code and Codex. Keep configuration examples separate if their formats differ and verify each against official documentation at implementation time.
