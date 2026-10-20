# Interactive PowerShell integration — Task 15B

This is an **opt-in launcher for a normal interactive Codex CLI session**, not
`codex exec`, terminal scraping, or automatic attachment to existing terminals.
Start Dime&Penny, install/review the passive hooks once, then launch Codex through
the PowerShell script from your project directory. Each terminal receives one
independent pet connection, up to the existing ten reserved sessions.

## Contract and prerequisites

Verified against the official [Codex hooks documentation](https://developers.openai.com/codex/hooks)
on 2026-10-21. The installed `codex-cli 0.154.0` reports `hooks stable true` through
`codex features list`. Windows PowerShell 5.1 is supported; Codex must already be
installed and authenticated normally. The launcher does not change your model,
sandbox, approval policy, credentials, execution policy, or Codex configuration.

Build with the existing CMake instructions. Both `build/app/DimePenny.exe` and
`build/bridge/DimePennyInteractive.exe` need the matching Qt runtime on `PATH`.
For this checkout's MSYS2 installation, in the PowerShell window you will use:

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
```

When packaging, deploy the helper's Qt Core/Network/SQL dependencies as well as
the application's QML runtime. `cmake --install` alone does not deploy Qt.

## One-time hook setup (explicit review required)

From the Dime&Penny checkout, print the configuration:

```powershell
& './tools/Start-DimePennyCodex.ps1' -PrintHooks
```

The output is a `hooks.json` document pointing at the actual compiled helper.
It uses an encoded PowerShell invocation so paths containing spaces, apostrophes,
Unicode or `&` are not interpreted as shell syntax. This encoding is quoting,
not encryption; the script simply invokes the helper with `--hook`.

1. Review the generated commands. Add the entries to your user configuration's
   `hooks.json` (normally `%USERPROFILE%\.codex\hooks.json`, or your configured
   Codex home). Alternatively, use a trusted project's `.codex/hooks.json`.
2. **Preserve existing hooks.** If the file already exists, merge the five event
   arrays into its existing `hooks` object; do not overwrite it. Install these
   entries in only one layer, and do not duplicate them on repeated setup.
3. Open Codex, use `/hooks`, and review/trust these exact definitions. Project
   hook configuration also requires project trust. If hooks are disabled or
   managed policy forbids local hooks, this integration cannot receive events.
4. Exit that Codex session. The next launcher-created session must deliver a
   fresh `SessionStart` after trust is established.

The generator does **not** edit global configuration, grant trust, bypass policy,
or run a model task. Moving the helper or changing hook definitions requires
regenerating the commands and reviewing them again. Without launcher environment
variables, the installed hooks return `{}` and do nothing.

## Start from any project folder

Start the desktop app once. In each PowerShell terminal, change to the desired
project and run (adjust the repository location if needed):

```powershell
& 'C:\Users\ASUS\OneDrive\Desktop\Dime&penny\tools\Start-DimePennyCodex.ps1'
```

The launcher resolves your normal `codex` command, registers the project, prints
the **Dime&Penny session ID**, and invokes Codex in the **same console**. It does
not redirect Codex's input or output. Normal keyboard input, interactive approval
prompts and Codex arguments remain Codex's responsibility.

Use `-CodexCommand 'C:\path\to\codex.exe'` to select a different installation,
or `-BridgePath 'C:\path\to\DimePennyInteractive.exe'` for a different build.
Pass CLI arguments explicitly through `-CodexArguments @('--model', 'your-model')`.
Do not use noninteractive subcommands with this launcher.

The first free companion is assigned automatically: Dime, then the nine distinct
Pennies. A pet stays **Connecting** until the trusted `SessionStart` hook arrives.
If it stays there, inspect `/hooks`, confirm the configured helper path, then
exit and relaunch. A connected helper alone never fabricates activity.

### Reconnect the same Codex conversation to the same pet

Reservations survive exits and app restarts; they are not automatically freed.
Use the printed local Dime&Penny ID and Codex's own conversation ID separately:

```powershell
& 'C:\Users\ASUS\OneDrive\Desktop\Dime&penny\tools\Start-DimePennyCodex.ps1' `
    -SessionId '<DimePenny-session-id>' `
    -CodexArguments @('resume', '<Codex-session-id>')
```

The project must match, the pet must be disconnected, and the subsequent
`SessionStart.session_id` must match the recorded Codex identity. Reconnection
uses a fresh secret and resumes the persisted event sequence. Old secrets cannot
reactivate a pet. Do not use another thread's local ID to borrow its reserved pet.
The UI's simulated reconnect action is intentionally unavailable for real sessions.

## Observed lifecycle

| Official hook | Pet update |
| --- | --- |
| `SessionStart` | Connecting → Active after validating the Codex identity |
| `UserPromptSubmit` with a new `turn_id` | Active/Completed → Working |
| `Stop` for the current turn | Working → Completed |
| `Interrupt` for the current turn | Working → Active/idle, **not** Completed |
| `SessionEnd` | Disconnect this terminal's pet |
| Launcher exit, terminal process death, lost pipe, or heartbeat timeout | Disconnect this terminal's pet |

`Completed` here means **a Stop hook was observed**, not that tests passed or the
user's task succeeded. Another Stop hook can request continuation; its next
`UserPromptSubmit` starts another working turn. A different hook can also block
a submitted prompt, so Working represents the observed submission, not proof
that a model request is executing. No signal is invented to fill these gaps.

Permission decisions, approval resolution, arbitrary internal failures, subagent
activity and desktop/cloud sessions are **not** covered by this adapter. It does
not respond to approvals. `SessionStart` during compaction with the same identity
is idempotent. Changing conversations inside the same terminal (`/new`, a fork,
or another identity-changing command) disconnects the old pet; relaunch the wrapper
for the new conversation. Unknown/out-of-order events are rejected with a warning.

## Isolation, transport and failure behavior

The desktop exposes a versioned, current-user-only local named pipe. The hidden
helper keeps a lease for its parent PowerShell process using an OS process handle,
and sends heartbeats every two seconds. The desktop expires a silent lease after
ten seconds; broken pipes are handled immediately. A killed terminal is detected
by the helper's 500 ms parent check. A second terminal has a separate lease and
secret, even when both use the same project folder.

The private bootstrap response travels over redirected helper stdout, not a file
or command-line secret. The launcher temporarily sets `DIMEPENNY_HOOK_ENDPOINT`
and `DIMEPENNY_HOOK_TOKEN` only for its child process environment and restores
their previous values in `finally`. Tokens are not saved in SQLite or printed.
This is a **same-user trust boundary**, not isolation from malicious processes
already running as the user. Trusted Codex subprocesses inherit the hook secret.

The hook helper reads a bounded JSON object from stdin (maximum 1 MiB) and forwards
only event name, session ID, working directory, turn ID and source. Prompts,
assistant messages, transcript paths and tool inputs are discarded. It never
opens transcripts or inspects terminal text. It returns `{}` and exit zero even
when Dime&Penny is unavailable; a delivery failure emits a short stderr warning,
not a decision or additional model context.

The local bootstrap protocol is JSON-lines, `v: 1`, maximum 64 KiB per request,
with at most 32 transport connections and the existing ten-session registry limit.
Operations are `attach`, owner `ping`, and token-bound `hook`. Hook responses
acknowledge validation/queuing; final persistence is performed asynchronously by
the existing authenticated, length-prefixed bridge and event processor. A rejected
internal lifecycle message closes only that lease. Partial requests have a deadline;
oversized/malformed requests and unsupported versions are rejected.

Closing Dime&Penny disconnects the pets but **does not kill interactive Codex**.
The helper exits when it loses the app. Restart both the app and the launcher to
reconnect; there is no silent reattachment or text-based fallback. Closing the
activation window still closes the pet presentation as before.

## Verification

The automated suite uses real local sockets, temporary SQLite databases, native
helper subprocesses, generated hook commands, and a **fake** interactive Codex
executable. It checks console-input forwarding, project/argument preservation,
environment restoration, normal exit, killed-terminal cleanup, independent pets,
ten-session capacity, duplicates, cross-session rejection, stale turns, interruptions,
malformed/oversized messages, timeout, stable reconnect, and recovery.

No real Codex model request is part of these tests. Final real-world smoke test:
after installing/trusting hooks, launch two wrappers, submit a small task in each,
interrupt one, and exit it. Verify only its pet becomes idle/disconnected while the
other continues. This uses your normal Codex permissions and quota.
