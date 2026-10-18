# Agent state machine (Task 3)

`AgentStateMachine` is a header-only domain component. Its static `apply(Session&,
const AgentEvent&)` method returns a `TransitionResult`. It alone has access to the
session's private lifecycle fields. The application must serialize calls for each
session; these types do not provide locking.

## Transition table

An em dash means `InvalidTransition`. Every cell is covered by the tests.

| Current state | SessionConnected | TaskStarted | TaskCompleted | ApprovalRequested | ApprovalResolved | SessionDisconnected | SessionError |
|---|---|---|---|---|---|---|---|
| Unregistered | — | — | — | — | — | — | — |
| Connecting | Active | — | — | — | — | Disconnected | Error |
| Active | — | Working | — | — | — | Disconnected | Error |
| Working | — | — | Completed | ApprovalNeeded | — | Disconnected | Error |
| ApprovalNeeded | — | — | — | — | Working | Disconnected | Error |
| Completed | — | Working | — | — | — | Disconnected | Error |
| Error | — | — | — | — | — | Disconnected | — |
| Disconnected | — | — | — | — | — | — | — |

The local command `beginConnecting(session)` moves Unregistered, Disconnected or
Error to Connecting. Other source states reject it. A successful bridge handshake
is represented by SessionConnected. Recovering from Error requires this handshake;
TaskStarted cannot silently clear an error.

The local `deactivate(session)` command moves any state to Disconnected and clears
pending approvals without changing accepted-event history or ordering watermarks.
SessionRegistry uses it to deactivate registrations and release their capacity slots.

ApprovalResolved must match the pending request ID. Either decision resumes Working:
this describes the agent processing the decision, not permission to execute a denied
operation. Expiry, operation cancellation and decision delivery belong to the later
approval broker. Error and disconnect clear the pending approval.

## Ordering and rejection

Checks run in this order and leave all session fields unchanged on rejection:

1. `WrongSession`: event targets a different registration ID.
2. `DuplicateEvent`: its ID has already been accepted for this Session instance.
3. `MissingSequence`: a numbered stream subsequently omits a sequence number.
4. `OutOfOrder`: sequence is less than or equal to the last accepted sequence,
   timestamp is before session creation, or timestamp is before the last accepted event.
5. `InvalidTransition`: event is not allowed in the current state.
6. `ApprovalMismatch`: resolution does not match the pending request.

On success, `Applied` commits the new state, event ID, timestamp, sequence, and
pending approval together. Invalid events do not reserve their IDs or advance the
ordering watermark, so a corrected event can be retried. Duplicate events are
reported explicitly and are not reapplied.

Sequence numbers may start at any value, including zero. Gaps are allowed; this
layer does not buffer or reconstruct missing events. Once supplied, numbers must
keep increasing for the lifetime of the Session, including reconnects. Producers
must preserve that counter; a new stream requires a new session identity until an
explicit connection-generation protocol is implemented. Sequence wrap is rejected.

Equal timestamps are allowed because timestamp resolution is finite. Without
sequence numbers, timestamps plus transition rules provide only partial ordering:
they cannot distinguish otherwise valid delayed events with equal timestamps.
The bridge should supply sequences. Task/run correlation and authenticated stream
generations are future protocol work; this machine cannot detect every stale event
that carries fresh metadata. It performs no wall-clock comparison.

Accepted event IDs are retained in memory for the Session's lifetime (no eviction,
so old duplicates remain detectable). This uses memory proportional to accepted
events. Durable deduplication, bounded resource policy, and restart reconstruction
are later persistence/IPC work. Do not treat this in-memory implementation as
restart-safe event storage.

## Tests

CTest target: `dimepenny.domain.state_machine`. Direct compiler alternative:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude \
    tests/unit/agent_state_machine_test.cpp -o tmp/build-validation/agent_state_machine_test.exe
./tmp/build-validation/agent_state_machine_test.exe
```

Tests cover all 56 state/event combinations, all eight beginConnecting source
states, event ordering with/without sequences, duplicate IDs, rejected-event retries,
wrong-session isolation, both approval decisions, request matching, reconnects,
and sequence overflow.
