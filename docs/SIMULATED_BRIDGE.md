# Simulated session bridge (Task 7)

DimePennyBridge is a runnable fake-session client using the existing codec and
LocalServer. It never starts or inspects a real Codex process. SimulatedBridge is
also a reusable C++ component exposed through DimePenny::Core.

## Run from MSYS2 UCRT64

```sh
./build/bridge/DimePennyBridge.exe --demo
./build/bridge/DimePennyBridge.exe --demo --scenario duplicate
./build/bridge/DimePennyBridge.exe --demo --scenario reconnect
./build/bridge/DimePennyBridge.exe --demo --scenario malformed
```

Demo mode starts an isolated local server on a random endpoint and provisions a
secret in memory. It does not need the desktop placeholder running. Default ID:
simulated-1; override with --session ID. The project path is the current directory.
Logs show message types/sequences, never credentials or full registration payloads.

| Scenario | Behavior |
|---|---|
| normal | Register, send SessionConnected, TaskStarted and TaskCompleted, then disconnect |
| duplicate | Repeat the identical TaskCompleted event, including ID, timestamp and sequence |
| reconnect | Finish a cycle, disconnect, reconnect with the same identity/token, send a second cycle with continued sequences |
| malformed | Finish a cycle, send invalid framed JSON, and expect the server to disconnect |

Exit 0 means the scripted transport scenario completed; 1 means connection/scenario
failure or the 10-second overall deadline; 2 means invalid configuration. The demo
now uses EventProcessor and logs actual registry state changes. Its duplicate scenario
does not apply TaskCompleted twice. Dedicated event-processing tests verify domain
outcomes; transport enqueue alone is not proof of domain application.

## Existing server

```sh
./build/bridge/DimePennyBridge.exe --endpoint ENDPOINT --session SESSION_ID --scenario normal
```

The server owner must first provision the session with LocalServer::authorize and
privately supply the token in the child process's DIMEPENNY_AUTH_TOKEN environment
variable. Tokens are never accepted as command-line arguments. Separate processes
need distinct provisioned IDs/tokens. The embedded demo server deliberately does
not print or export credentials for other clients.

## Component contract

- Registration must be accepted with the matching reply ID before event sends.
- Heartbeat pings are answered automatically. Wrong-session, malformed and
  unexpected server messages fail the connection.
- Connection/registration deadline is 5 seconds. After registration the silence
  deadline is 20 seconds, reset by complete accepted server messages.
- Reads use bounded chunks; pending output is capped at four maximum frames.
- sendEvent accepts all protocol lifecycle event names, plus approval metadata
  when required. Invalid schemas throw ProtocolError. False means not ready,
  sequence exhausted or failed enqueue; true means queued, not acknowledged.
- replayLastEvent repeats the exact last event without advancing the sequence.
- sendMalformedFrame deliberately bypasses JSON validation for fault injection.
- connectToServer can be called again after disconnection. Decoder state resets;
  sequence and replay history survive for the instance lifetime. No automatic
  unlimited retry loop is introduced.
- A new process/instance starts at zero. Use a fresh session identity when the
  server retains an older stream's watermark; restart persistence is later work.
- Use in one Qt event-loop thread. It is a simulator, not the real Codex adapter.

## Verification

CTest includes dimepenny.ipc.bridge and four dimepenny.bridge.demo scenarios.
Integration coverage includes two clients, repeated heartbeat cycles, lifecycle
delivery, exact duplicate replay and domain rejection, reconnects, malformed JSON,
continued sequences, approval-response delivery, rejected authentication and server
shutdown. Tests require local named-pipe permission and make no remote network calls.
