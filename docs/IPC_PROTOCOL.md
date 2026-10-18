# Local IPC protocol v1 (Task 5)

Implementation: `include/dimepenny/ipc/protocol.h` and `src/ipc/protocol.cpp`,
linked through `DimePenny::Core`. This is a codec and incremental frame decoder.
Socket serving, authentication, timers and domain dispatch are subsequent tasks.

## Framing

Each frame contains a 4-byte **unsigned big-endian** body length, followed by
exactly that many bytes of UTF-8 JSON. The prefix counts body bytes, not characters,
and excludes itself. No delimiter, terminating NUL, compression or byte-order mark
is used. Bodies must be 1 through 65,536 bytes (64 KiB), inclusive.

`encodeFrame(message)` validates and serializes a complete frame. `encodeJson` and
`decodeJson` provide the same validation without transport framing. Errors throw
`ProtocolError` with a generic diagnostic; raw payload content is not included.

Maintain one `FrameDecoder` per connection. `feed(bytes)` accepts at most 65,540
bytes per call and returns zero or more validated messages. Read socket data in
chunks bounded by `maxReadBytes`; do not pass unbounded `readAll()` output. Split
headers, split UTF-8 characters, split bodies and combined frames are supported.
Pending storage is bounded to one header or one body. The returned batch is also
bounded by the read size plus the previously buffered partial frame.

Call `finish()` on EOF: partial headers/bodies are errors. Invalid framing, JSON or
payload validation permanently fails the decoder and clears buffered data. The
server must close that connection, not scan for a guessed next frame. If any frame
in a feed call fails, no messages from that call are returned; earlier successful
feed calls remain delivered. Reconnection requires a new decoder. Allocation errors
also fail the decoder and propagate to the caller.

## Envelope

All six members are mandatory; unknown members, nulls, duplicate JSON member names
(including escaped equivalents) and malformed UTF-8 are rejected.

```json
{
  "protocolVersion": 1,
  "messageId": "message-42",
  "sessionId": "registration-7",
  "timestamp": "2026-10-18T10:00:00.000Z",
  "type": "event",
  "payload": {"eventType": "TaskStarted", "sequence": "42"}
}
```

- `protocolVersion`: JSON integer 1. No implicit version fallback.
- `messageId`: sender-issued unique ID. Retries reuse the same ID.
- `sessionId`: stable Dime&Penny registration ID, including initial registration.
  Never use a PID as the registration identity.
- `timestamp`: valid UTC date/time with exactly three fractional digits and `Z`.
- `type`: one of the seven names below, case-sensitive.
- `payload`: validated object with exactly the required and permitted optional fields.

Envelope IDs and request/reply IDs are 1-128 ASCII letters, digits, `_`, `-`, `.`,
or `:`. UUIDs are permitted but not required. General metadata strings are nonblank,
valid Unicode, exclude ASCII control characters, and are at most 256 UTF-8 bytes
unless a different bound is specified below. Optional fields are omitted, not null.

## Payloads

| Type | Direction | Required fields | Optional fields |
|---|---|---|---|
| `register` | Bridge to app | `project`, `pet`, `terminalType` | `codexSessionId`, `processId`, `authToken` |
| `registration_result` | App to bridge | `inReplyTo`, `accepted` | `reason` (required when rejected, forbidden when accepted) |
| `event` | Bridge to app | `eventType`, `sequence` | `approval` (required only for approval events) |
| `heartbeat` | Either | `kind` | `inReplyTo` (required for pong, forbidden for ping) |
| `disconnect` | Either | `reason` | None |
| `error` | Either | `code`, `message` | `inReplyTo` |
| `approval_response` | App to bridge | `requestId`, `decision` | None |

### Registration

```json
{
  "project": {"name": "Dime&Penny", "directory": "C:\\Projects\\DimePenny"},
  "pet": {
    "id": "pet-7", "name": "Penny", "appearanceKey": "round-striped",
    "color": {"red": 240, "green": 100, "blue": 25}
  },
  "terminalType": "Windows Terminal",
  "codexSessionId": "codex-session-7",
  "processId": 1234
}
```

`project` requires name and directory; directory is at most 4096 UTF-8 bytes and
must be drive-rooted or a UNC server/share path. Parsing does not prove filesystem
existence, canonical location or access permission. `pet` requires id, name,
appearanceKey and a color object with integer red/green/blue channels from 0-255.
`processId`, when supplied, is an integer from 1 through 4,294,967,295.

Task 6 adds `authToken`: exactly 64 lowercase hexadecimal characters. The codec
allows its omission for legacy codec callers, but LocalServer requires it for
every registration. A trusted application must provision it for the session first;
see [server authentication](LOCAL_SERVER.md). It is stripped before registration
messages are emitted to application observers.

The registry assigns the supplied pet to the session, subject to its uniqueness
checks. `registration_result.inReplyTo` names the register message. `accepted` is a
JSON boolean. Rejection reason is nonblank text up to 2048 UTF-8 bytes. A result
must not be accepted as proof of connection by the UI until the future server's
authentication/registration flow has completed.

### Lifecycle events

`eventType` is one of SessionConnected, TaskStarted, TaskCompleted,
ApprovalRequested, ApprovalResolved, SessionDisconnected or SessionError.

`sequence` is a **decimal string**, not a JSON number, in the uint64 range
0-18446744073709551615. No sign, whitespace or leading zeros (except `"0"`). This
avoids loss of integer precision in JSON consumers. Every event must include it;
the domain model's optional sequence is not used for wire events. Preserve increasing
numbers across reconnects as required by the current state machine.

ApprovalRequested requires `approval: {"requestId": "r-1"}`. ApprovalResolved
requires `approval: {"requestId": "r-1", "decision": "approve"}` or `"deny"`.
Non-approval events forbid that field. `approval_response` is a separate user-decision
command, not evidence that the agent has resumed.

The future domain adapter maps envelope messageId to AgentEvent.eventId, sessionId
to the target session, and timestamp to occurredAt. State transitions, replay
tracking and approval matching remain the registry/state machine's responsibility.

### Heartbeat, disconnect and errors

A heartbeat `{"kind":"ping"}` requests a response. Its peer returns
`{"kind":"pong","inReplyTo":"ping-message-id"}` with a new envelope messageId
and the same sessionId. Pong does not trigger another reply. Scheduling, deadlines
and checking outstanding ping IDs belong to the server, not the codec.

`disconnect.reason` and `error.message` are nonblank text up to 2048 UTF-8 bytes.
`error.code` is an identifier; `inReplyTo` optionally identifies the failed request.
An error message is diagnostic and must not independently alter another session's
state. On malformed input without a trustworthy session ID, close the connection;
do not invent an identity to send an error. Never echo secrets in diagnostics.

The codec cannot authenticate session ownership or enforce message direction.
The server must bind an authenticated connection to a single session, enforce
direction and registration order, limit queued output, and handle timeouts.

## Tests

`dimepenny.ipc.protocol` runs under CTest. It exercises all message and event kinds,
Unicode round-trips, byte-at-a-time and every two-part split, combined frames,
version/field/type validation, duplicate members, timestamp and sequence bounds,
oversized/zero-length frames, malformed JSON/UTF-8, and incomplete EOF.
