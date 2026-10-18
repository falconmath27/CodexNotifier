# Domain models (Task 2)

Public headers live under `include/dimepenny/domain`. These small value types are
header-only C++20 and do not depend on Qt, SQLite or the filesystem at runtime.
Include them through the existing `DimePenny::Core` CMake target.

| Model | Contract |
|---|---|
| `SessionId` | Required opaque registration ID; 1-128 ASCII letters, digits, `_`, `-`, `.`, `:`. Case-sensitive equality, ordering and `std::hash` support. No implicit string conversion or ID generation. |
| `ProjectInfo` | Required display name and absolute native filesystem path. No existence checks or path rewriting; the integration layer will verify access and canonical paths. |
| `PetIdentity` | Required ID, display name, appearance key and RGB color. SessionRegistry enforces unique pet IDs and appearance-key/color pairs across retained registrations. |
| `AgentState` | Eight planned lifecycle states with stable display names through `toString`. |
| `Session` | Owns its ID, project and pet values, terminal type, creation timestamp, optional Codex ID and optional positive process ID. Starts `Unregistered`. Only `AgentStateMachine` changes its lifecycle fields. |
| `AgentEvent` | Required event ID, target session, type and occurrence timestamp; optional source sequence and validated approval correlation metadata. |

Constructors throw `std::invalid_argument` for invalid data. Text fields reject
blank values and ASCII control characters. Callers at UI and protocol boundaries
must catch validation errors. Values are owned, with read-only accessors.

Event types are `SessionConnected`, `TaskStarted`, `TaskCompleted`,
`ApprovalRequested`, `ApprovalResolved`, `SessionDisconnected`, and `SessionError`.
Approval requests require a nonblank request ID and no decision; resolutions
require that ID and an explicit approve/deny decision. Other events reject
approval metadata. This models correlation only, not approval delivery or execution.

Timestamps are supplied by the caller (`std::chrono::system_clock::time_point`),
making tests deterministic. Sequence numbers may begin at zero; missing means
unknown. The [state machine](STATE_MACHINE.md) now enforces transitions, timestamp
and sequence ordering, and in-memory duplicate detection. Connection generations,
event persistence and JSON serialization are later tasks. Constructors do not
claim to validate ordering or global uniqueness. PID is metadata, never identity.

## Verification

`dimepenny.domain.models` is registered with CTest. While Qt/CMake are unavailable,
the same tests can be compiled directly from the project root in MSYS2 UCRT64:

```sh
mkdir -p tmp/build-validation
g++ -std=c++20 -Wall -Wextra -Wpedantic -Werror -Iinclude \
    tests/unit/domain_models_test.cpp -o tmp/build-validation/domain_models_test.exe
./tmp/build-validation/domain_models_test.exe
```
