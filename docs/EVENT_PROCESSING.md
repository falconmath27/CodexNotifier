# Event processing (Task 8)

EventProcessor owns a LocalServer and SessionRegistry and connects them within one
Qt event-loop thread. Its public registry view is const. The simulator's embedded
demo server now uses this composition, so received lifecycle events update domain
state rather than only being logged. The desktop placeholder is not wired yet.

## Admission

The server authenticates the connection's session-specific token first. Its new
registration handler then validates/converts metadata and admits the session into
the registry before registration_result accepted=true is sent. New registrations
start Connecting; only an accepted SessionConnected lifecycle event makes them
Active. Domain collisions or invalid registration metadata close the connection.

Reconnects must preserve project, pet, Codex ID, terminal and process metadata.
They reuse the existing record, pet and event history, and start Connecting. An
identity-changing registration is rejected. Updating metadata is a separate future
operation; no silent overwrite is performed. The bare LocalServer can still be
used without an admission handler for transport-only tests.

## Processing path

1. LocalServer decodes bounded frames and validates the wire schema.
2. It compares every message sessionId with the authenticated socket binding.
3. It emits authenticatedMessage with that independently held binding and the message.
4. EventProcessor validates the schema/size again at its application boundary,
   verifies the binding match and checks that the session is currently connected.
5. The converter maps messageId to AgentEvent.eventId, sessionId to SessionId,
   timestamp to system-clock occurredAt, and the decimal sequence string to uint64.
   All eight lifecycle event types (including TaskInterrupted) and approval correlation metadata are supported.
6. The registry dispatches to that session's state machine. The existing accepted-ID
   history detects duplicates before applying transitions. Ordering and approval
   request checks remain enforced by the state machine.
7. Applied events emit stateChanged. Every processing attempt emits eventProcessed
   with an explicit result. Invalid events return a correlated, generic error to the
   originating connection. Duplicates are successful no-ops without another state
   update or an error response; the wire protocol has no general event ACK yet.

ProcessingResult distinguishes Applied, Duplicate, InvalidMessage, WrongSession,
NotAuthenticated, InvalidTransition, OutOfOrder, ApprovalMismatch,
UnavailableSession and Ignored. Diagnostic error messages return Ignored and never
change lifecycle state. Registration, heartbeat and disconnect messages follow the
server's dedicated paths, not the lifecycle converter.

`process(boundSessionId, message)` is a trusted adapter/test entry point. Its bound
ID must come from an authenticated transport, not from user JSON. Network callers
cannot invoke it directly. The processor's production connection uses the server's
authenticatedMessage signal with a direct same-thread connection.

## Failure and isolation rules

- Validation failure, wrong-session identity, stale sequence/timestamp, mismatched
  approval and invalid transition leave domain state/history unchanged.
- Event IDs are scoped to a session. The same ID in two sessions is not a collision.
- Accepted IDs and sequence watermarks survive reconnects in memory. Rejected IDs
  are not consumed; a corrected event may reuse its ID and sequence.
- Transport disconnect, timeout, malformed input or server shutdown marks only the
  affected session Disconnected and clears pending approval correlation. Its enabled
  registration slot remains reserved for reconnect. Explicit deactivation is separate.
- Domain timestamps must fit system_clock::duration; out-of-range wire dates are
  rejected before arithmetic conversion.
- History remains in memory and grows with accepted events, as documented for the
  state machine. Persistence, stream-generation identity and restart recovery are
  later tasks. Same-ID changed-content retries are treated as duplicates and cannot
  replace an accepted event.

## Verification

CTest target dimepenny.application.events covers authenticated admission, duplicate
suppression, independent per-session IDs, cross-session attempts, malformed schemas,
invalid versions and timestamps, stale ordering, all lifecycle event mappings,
approval correlation, diagnostic errors, reconnect history and stable pet identity.
It also runs real SimulatedBridge events through the complete socket-to-domain path,
checks malformed-frame isolation, and verifies pet-identity conflicts are rejected
before the bridge becomes ready. All four demo scenarios now use EventProcessor.
