# Session registry (Task 4)

`SessionRegistry` owns sessions by value and exposes const views. Include
`dimepenny/domain/session_registry.h` through `DimePenny::Core`.

## Registration and pet assignment

`registerSession(Session)` accepts a fresh Unregistered session with its caller-selected
`PetIdentity`. Successful registration permanently binds that pet to the registration
and starts Connecting. It does not imply an authenticated bridge connection: a valid
SessionConnected event must still arrive. Authentication belongs to the IPC layer.

Registration rejects duplicate application session IDs, present Codex IDs, pet IDs,
and identical appearance-key/RGB pairs. Reservations include inactive records.
Display names, project directories and PIDs may be shared. IDs are case-sensitive.
The registry does not create sprites or generate random pet identities; appearance
selection remains the activation UI's job. It validates and retains the supplied assignment.

## Capacity and lifecycle

- Capacity is 10 enabled registrations, including Connecting, Error and temporarily
  Disconnected sessions. This keeps a slot available for their reconnection.
- `deactivate(id)` makes a registration inactive and its lifecycle Disconnected,
  clears pending approval correlation, and releases its capacity slot. No synthetic
  bridge event is created. Accepted event IDs and sequence/timestamp watermarks remain.
- `reconnect(id)` re-enables an inactive record if capacity permits, or reconnects an
  enabled Error/Disconnected record without taking another slot. It starts Connecting
  using the existing state machine and preserves pet, project and identity metadata.
- Failed reconnections leave state and capacity unchanged. Connected/Connecting
  sessions cannot reconnect again. Repeated deactivation returns AlreadyInactive.
- Retained inactive records may make `size()` exceed 10. There is no deletion or
  persistence yet; reservations and history last for this registry's lifetime.

`isActive`, `activeCount` and `activeSessions` refer to enabled registrations, not
the narrower AgentState::Active value. `activeSessions` returns views sorted by
SessionId; `find` also finds inactive records and returns nullptr for missing IDs.
Views survive insertions and lifecycle changes; they must not outlive the registry
or its replacement. Callers must serialize access.

## Events and results

`applyEvent` routes by event.sessionId, rejects missing/inactive sessions, then
delegates to AgentStateMachine. `RegistryEventResult` is a variant containing either
RegistryResult for registry rejection or TransitionResult from the state machine.
Other registry operations return RegistryResult directly.

State-machine ordering rules still apply across reconnects; counters must not reset.
The registry does not authenticate event sources or introduce connection generations.
Const views prevent callers from bypassing routing or changing registry-owned pets.

## Verification

CTest target `dimepenny.domain.registry` covers identity collisions, lookup,
capacity/reclaimed slots, retained reservations, event isolation, inactive rejection,
approval interruption, error recovery, and stable pet assignments through reconnects.
