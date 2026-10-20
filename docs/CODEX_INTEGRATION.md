# Codex integration contract — Task 15

For normal interactive PowerShell sessions, see the separate
[Task 15B launcher and trusted-hook adapter](INTERACTIVE_POWERSHELL.md).
The limitations below apply to the original noninteractive exec adapter.

Verified on 2026-10-21 against the official [non-interactive documentation](https://developers.openai.com/codex/noninteractive) and the installed `codex-cli 0.154.0` (`--version`, `exec --help`). The implementation targets **new, single-turn `codex exec --json` processes launched by Dime&Penny**, not arbitrary running terminals or Codex desktop conversations.

## Supported mapping

| Structured stdout event | Dime&Penny lifecycle |
| --- | --- |
| `thread.started` with a nonempty `thread_id` | `SessionConnected` |
| `turn.started` after thread start | `TaskStarted` |
| `turn.completed` after turn start | `TaskCompleted` |
| `turn.failed` | `SessionError` |
| Nonzero/crashed process exit, malformed/oversized JSON, or exit without completion | `SessionError` (adapter failure) |

`item.*`, retry diagnostics (`error`), and unknown additive event types do not drive pet state. stderr is drained without interpreting or persisting it. Successful process exit alone is never treated as completion. A run cannot switch its thread ID, and duplicate start/completion notifications do not generate repeated transitions. Each decoder is scoped to one owned process, so events from separate runs cannot share parser state.

Approval-needed/resolved are **not implemented for this exec adapter**: the verified exec JSON contract does not establish a supported interactive approval-response channel. No approval is inferred from text and no response is fabricated. A future app-server adapter can implement richer interactive behavior separately. Existing simulated approval tests remain available.

## Launch a real run

Use an installed native `codex.exe`, already authenticated through the normal Codex CLI. Do not point at the npm `.cmd` or PowerShell wrapper. Dime&Penny never reads or copies the Codex credential file. A real run can consume your Codex quota and reads the explicitly selected project.

```powershell
& './build/app/DimePenny.exe' `
  --codex-executable 'C:/path/to/codex.exe' `
  --project 'C:/path/to/project' `
  --prompt-file 'C:/path/to/prompt.txt' `
  --companion penny
```

Close any existing Dime&Penny instance first (the persistence database has a single-writer lock). A free pet slot/color is required. `--companion dime` is also supported when Dime is available. Ordinary activation through the window remains **simulation mode**.

The native process is launched directly, without a shell, using `exec --json --sandbox read-only --color never -`. The prompt is UTF-8 on stdin, not a command-line argument. The adapter does not widen permissions, bypass approvals, skip repository checks, select a model, or disable user/project configuration. Existing CLI configuration, auth, tool integrations and repository requirements still apply. Dime&Penny's IPC token is removed from the child environment. Codex's own stdout items and stderr may contain sensitive material; this adapter does not log or persist their content.

## Identity, transport and recovery

Real and simulated sources share the authenticated `BridgeClient` transport. `SimulatedBridge` remains a source-compatible alias with explicit replay/malformed-frame test hooks. Both sources still pass through the same event processor, state machine and persistence checks. The real adapter emits `SessionConnected` only after the documented thread-start event, not merely because the IPC bridge registered.

The source marker (`source/<local-session-id> = codex-exec`) is saved before registration, and the observed thread ID is saved separately as `codex-thread/<local-session-id>`. Local pet/session identity does not change when the Codex thread ID arrives. Prompt contents are not saved by Dime&Penny. The bridge remains connected after a finished run to display its terminal result until app shutdown; it does not mean a model is still working.

On restart, real sessions retain their assignments/history and remain disconnected. They are never automatically converted into simulated sessions. Reconnecting/resuming an old real Codex thread is intentionally not supported by this first adapter. Start a new explicit run in an available slot; existing reserved slots are not automatically released. Closing the app terminates its owned Codex processes. A broken or rejected IPC lifecycle connection also stops the owned adapter; there is no fallback to reading terminal output.

## Verification boundary

Tests cover chunked JSONL, invalid order, identity changes, duplicate notifications, transient diagnostics, missing completion, limits, process failures, fake-process launch arguments, the IPC/UI path, and recovery source separation. A deterministic native test executable supplies documented JSON fixtures; **no live model request was run during implementation**. Actual account/model access and an end-to-end real task still require an explicit user-run smoke test. Recheck this contract before adopting a changed CLI event schema.
