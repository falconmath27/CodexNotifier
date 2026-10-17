# Dime&Penny — Learn-and-Build Task Flow

## Working method

Every milestone follows the same cycle:

1. Learn the minimum required concept.
2. Complete a small isolated exercise.
3. Apply it to Dime&Penny.
4. Test the feature.
5. Explain the code in your own words.
6. Record what you learned.
7. Continue only after the milestone works.

Do not study entire C++, OS, networking, or AI courses before building.

## Architecture companion

The detailed system design, deployment boundaries, module ownership, IPC contract, persistence model, failure behavior, and security rules are defined in [ARCHITECTURE.md](ARCHITECTURE.md).

The key architecture decision is to build the Windows MVP as a **local modular monolith**:

- One persistent Qt desktop process owns session state, pets, approvals, and SQLite persistence.
- One thin bridge connects each registered Codex session to the app through versioned local IPC.
- The domain state machine is the only component that changes lifecycle state.
- The UI renders state but does not contain business rules.
- Context linking remains isolated behind explicit source selection and user review.

---

# Phase 0 — Define the MVP

## Learn

- What an MVP is
- Functional versus non-functional requirements
- Basic software architecture
- Breaking a large problem into milestones

## Tasks

- Define what counts as an Dime&Penny session.
- Define the pet states:
  - Unregistered
  - Connecting
  - Active
  - Working
  - Approval needed
  - Completed
  - Error
  - Disconnected
- Define the first-version constraints:
  - Windows first
  - Maximum 10 sessions
  - One pet per registered Codex session
  - Unique session identity
  - Unique pet appearance
  - No real context merging yet
- Write acceptance criteria for the MVP.

## Deliverable

A short requirements document and state-transition diagram.

## Completion check

You can explain exactly what Dime&Penny version 1 will and will not do.

---

# Phase 1 — Modern C++ Foundations

## Learn

- Variables and fundamental types
- Functions
- Header and source files
- References and pointers
- Classes and objects
- Constructors
- Encapsulation
- Enums
- `std::string`
- `std::vector`
- `std::unordered_map`
- `std::optional`
- Stack versus heap
- RAII and smart pointers
- Basic error handling

## Practice exercises

- Create a `Pet` class with a name, color, and state.
- Create a `Session` class with an ID and project directory.
- Store several sessions in an `unordered_map`.
- Change a pet's state using validated methods.
- Prevent two sessions from using the same ID.

## Dime&Penny tasks

Create these initial models:

```text
SessionId
Session
PetIdentity
AgentState
AgentEvent
ProjectInfo
```

Create a `SessionRegistry` that can:

- Register a session
- Reject duplicate sessions
- Find a session
- Remove or deactivate a session
- Enforce the 10-session limit
- Return all active sessions

## Deliverable

A console application that registers simulated Dime&Penny sessions.

## Completion check

You can explain ownership, object lifetime, references, containers, and why each class exists.

---

# Phase 2 — Build System, Git and Testing

## Learn

- Git commits and branches
- CMake targets
- Libraries versus executables
- Debug and release builds
- Assertions
- Unit testing
- Arrange–Act–Assert testing

## Tasks

Create a project structure similar to:

```text
Dime&Penny/
├── CMakeLists.txt
├── src/
├── include/
├── tests/
├── assets/
├── docs/
└── tools/
```

Add tests for:

- Successful registration
- Duplicate registration
- Maximum capacity
- Invalid session ID
- Deactivation
- State transitions

## Deliverable

A CMake project that builds the application and runs its tests.

## Completion check

A fresh clone can be configured, compiled, and tested using documented commands.

---

# Phase 3 — Events, JSON and State Machines

## Learn

- State machines
- Valid and invalid transitions
- Event-driven programming
- JSON objects and arrays
- Serialization and deserialization
- Event IDs
- Timestamps
- Idempotency and duplicate detection

## Practice exercises

- Convert an `AgentEvent` to JSON.
- Parse JSON back into an event.
- Reject malformed messages.
- Send the same event twice and process it only once.

## Dime&Penny tasks

Implement events such as:

```text
SessionConnected
TaskStarted
TaskCompleted
ApprovalRequested
ApprovalResolved
SessionDisconnected
SessionError
```

Implement the state transition rules.

Examples:

```text
Active → Working
Working → Completed
Working → ApprovalNeeded
ApprovalNeeded → Working
Any connected state → Disconnected
```

## Deliverable

A console event simulator that changes the correct session state from JSON events.

## Completion check

Malformed, duplicate, or out-of-order events do not corrupt session state.

---

# Phase 4 — Operating-System Fundamentals

## Learn

- Program versus process
- Process ID
- Parent and child processes
- Threads
- Standard input, output, and error
- Exit codes
- Environment variables
- Working directories
- Process termination
- Why a terminal is not the same as the program running inside it

## Practice exercises

- Launch a child process.
- Capture its output.
- Read its exit code.
- Detect when it terminates.
- Start two processes from different directories.
- Distinguish them using stable metadata.

## Dime&Penny tasks

Design a session identity containing:

```text
Dime&Penny registration ID
Codex session ID
Process ID where applicable
Project directory
Terminal type
Creation time
Connection state
```

Do not rely only on a process ID because operating systems can reuse process IDs.

## Deliverable

A process-monitoring experiment that detects when a test process starts and exits.

## Completion check

You can explain how Dime&Penny distinguishes the terminal, Codex process, project, and registered session.

---

# Phase 5 — IPC and the Dime&Penny Bridge

## Learn

- Inter-process communication
- Client-server architecture
- Named pipes and local sockets
- Request-response messages
- Timeouts
- Heartbeats
- Reconnection
- Message framing
- Failure handling

## Practice exercises

- Build a local server.
- Connect one client.
- Send a JSON message.
- Connect multiple clients.
- Disconnect and reconnect a client.
- Reject an invalid registration message.

## Dime&Penny tasks

Use:

```text
QLocalServer
QLocalSocket
JSON messages
```

Create a small bridge that sends simulated Codex events to Dime&Penny.

Define a protocol such as:

```text
register
registration_result
event
heartbeat
approval_response
disconnect
error
```

Each message should include:

```text
protocol version
message ID
session ID
timestamp
message type
payload
```

## Deliverable

Several simulated sessions send events to one persistent Dime&Penny backend.

## Completion check

Every event is attributed to the correct registered session.

---

# Phase 6 — Qt Desktop Foundations

## Learn

- Qt application structure
- Widgets and layouts
- Signals and slots
- Qt event loop
- Timers
- Dialogs
- Model-view separation
- `QSystemTrayIcon`
- `QProcess`
- Safe communication between backend logic and UI

## Practice exercises

- Open a basic Qt window.
- Add a button and label.
- Connect a signal to a slot.
- Update a label using a timer.
- Display a system-tray notification.
- Show backend state without placing backend logic in the widget.

## Dime&Penny tasks

Build the activation dialog:

- Connection method
- Agent name
- Terminal type
- Project directory
- Pet color
- Current capacity
- Validation errors
- Activation status

## Deliverable

The user can register a simulated session through the interface.

## Completion check

The UI does not mark a session active until backend verification succeeds.

---

# Phase 7 — Build the Desktop Pet

## Learn

- Transparent windows
- Frameless windows
- Always-on-top behavior
- Window positioning
- Mouse events
- Custom painting
- Timed sprite animation
- Basic accessibility and visual feedback

## Tasks

- Create one floating pet window.
- Make it draggable.
- Add idle and working animations.
- Add completed, approval-needed, error, and disconnected appearances.
- Add a tooltip with session and project information.
- Add a context menu:
  - Show details
  - Open project
  - Reconnect
  - Deactivate
  - Exit Dime&Penny

## Deliverable

One simulated session controls one desktop pet.

## Completion check

Every state is visually distinguishable without relying solely on color.

---

# Phase 8 — Multiple Concurrent Sessions

## Learn

- Concurrency versus parallelism
- Race conditions
- Mutexes
- Atomics
- Producer-consumer pattern
- Thread-safe queues
- Deadlocks
- Event ordering
- UI-thread restrictions

## Practice exercises

- Send events from multiple simulated clients.
- Reproduce a race condition.
- Protect shared state.
- Process events through a queue.
- Maintain UI responsiveness during blocking work.

## Dime&Penny tasks

- Support up to 10 sessions.
- Give each pet a stable identity.
- Enforce distinguishable appearances.
- Handle near-simultaneous events.
- Deduplicate repeated events.
- Prevent one disconnected client from affecting another pet.
- Keep blocking operations away from the UI thread.

## Deliverable

Ten simulated sessions can run without incorrect pet attribution.

## Completion check

A stress test cannot make one session's event update another session's pet.

---

# Phase 9 — SQLite Persistence

## Learn

- Tables, rows, and primary keys
- Foreign keys
- Basic SQL
- Transactions
- Schema migrations
- Repository pattern
- Persisted state versus live state

## Suggested tables

```text
sessions
pet_identities
projects
events
registrations
context_links
settings
schema_migrations
```

## Tasks

- Save registrations.
- Save event history.
- Save pet identity and appearance.
- Restore registrations after restart.
- Verify restored sessions before marking them active.
- Keep history when a session is deactivated.
- Add basic schema versioning.

## Deliverable

Dime&Penny restarts without losing session history or pet assignments.

## Completion check

Restored sessions remain disconnected until successfully reverified.

---

# Phase 10 — Real Codex Integration

## Learn

- Current Codex CLI configuration
- Available lifecycle event mechanisms
- Session identifiers
- Approval behavior
- Input and output contracts
- Integration versioning
- Secure handling of event data

## Tasks

- Verify the current official Codex documentation.
- Record the supported integration contract.
- Replace simulated events gradually.
- Start with task-completion events.
- Add task-started events.
- Add errors and disconnections.
- Add permission requests only after passive events work.
- Preserve a simulation mode for testing.

## Deliverable

A real Codex session triggers the correct Dime&Penny pet state and notification.

## Completion check

Dime&Penny uses verified events instead of guessing activity from terminal text.

---

# Phase 11 — Permission Handling

## Learn

- Authentication versus authorization
- Least privilege
- Safe defaults
- Timeouts and cancellation
- Request correlation
- Audit logs
- Replay and duplicate protection

## Tasks

- Display the requesting session clearly.
- Display the project directory.
- Display the requested operation.
- Associate the response with the exact request ID.
- Support approve and deny.
- Default to denial when Dime&Penny cannot respond safely.
- Handle expired requests.
- Never silently approve.
- Record the decision without storing unnecessary sensitive content.

## Deliverable

A permission request can be resolved through the correct pet.

## Completion check

A response can never be applied to another session or an expired request.

---

# Phase 12 — Reliability and Packaging

## Learn

- Integration testing
- Fault injection
- Structured logging
- Crash recovery
- Application packaging
- Configuration management
- Security basics

## Tasks

Test:

- Backend starts before the bridge.
- Bridge starts before the backend.
- Duplicate registration.
- Malformed JSON.
- Session termination.
- Backend restart.
- Application restart.
- Ten simultaneous sessions.
- Slow or frozen client.
- Missing project directory.
- Database failure.
- Approval timeout.

Package:

- Dime&Penny executable
- Qt runtime dependencies
- Assets
- Database initialization
- Configuration
- Installation and uninstall instructions

## Deliverable

A Windows MVP that another person can install and test.

## Completion check

The program handles expected failures without losing registrations or mixing sessions.

---

# Phase 13 — Context Linking

Begin only after the notification and session system is reliable.

## Learn

- Context windows and token budgets
- Structured project summaries
- File metadata
- Chunking
- Full-text search
- Relevance ranking
- Deduplication
- Summarization
- Stale information
- Repository boundaries

## Initial approach

Start with:

```text
Structured project summaries
Selected files
Git status and recent changes
SQLite full-text search
User-reviewed task brief
```

Do not begin with a vector database.

## Tasks

- Select two registered pets.
- Retrieve their project metadata.
- Let the user choose what context may be shared.
- Produce a structured combined brief.
- Show sources for important statements.
- Let the user edit the brief.
- Choose:
  - Create a new agent
  - Continue in Agent A
  - Continue in Agent B
- Preserve both original histories.

## Deliverable

Two projects produce a reviewable cross-project task brief.

## Completion check

The user controls what context is shared and which agent receives it.

---

# Phase 14 — Advanced Retrieval and Orchestration

## Learn

- Embeddings
- Semantic search
- Retrieval-augmented generation
- Tool calling
- MCP fundamentals
- Agent orchestration
- Evaluation
- Conflict detection
- Permission boundaries

## Tasks

- Compare keyword and semantic retrieval.
- Add embeddings only if they improve measured retrieval quality.
- Detect contradictory project information.
- Preserve source references.
- Validate generated task briefs.
- Enforce repository and permission boundaries.
- Add cross-project task result verification.

## Deliverable

Reliable context-assisted coordination across Dime&Penny sessions.

---

# Recommended milestone order

```text
Requirements
    ↓
C++ models
    ↓
Tests and CMake
    ↓
Events and state machine
    ↓
Processes
    ↓
IPC
    ↓
Qt activation interface
    ↓
Desktop pet
    ↓
Multiple sessions
    ↓
Persistence
    ↓
Real Codex integration
    ↓
Permission handling
    ↓
Packaging
    ↓
Context linking
    ↓
Advanced orchestration
```

# Suggested study schedule

Assuming approximately 1–2 hours per day:

| Period | Focus |
|---|---|
| Weeks 1–2 | C++ fundamentals and session models |
| Week 3 | CMake, Git, and testing |
| Week 4 | JSON, events, and state machines |
| Week 5 | Processes and OS concepts |
| Week 6 | IPC and simulated sessions |
| Weeks 7–8 | Qt interface and desktop pet |
| Weeks 9–10 | Concurrency and multiple sessions |
| Week 11 | SQLite and restart recovery |
| Weeks 12–13 | Real Codex integration |
| Week 14 | Permission handling |
| Weeks 15–16 | Reliability, packaging, and MVP release |
| Later | Context linking and orchestration |

Treat this as a flexible sequence, not a deadline.

# Rules for learning effectively

- Type the important code yourself.
- Do not copy code you cannot explain.
- Keep functions and milestones small.
- Commit after every working feature.
- Write tests before changing critical session behavior.
- Maintain a `LEARNING_NOTES.md`.
- Record bugs and their root causes.
- Rebuild concepts inside Dime&Penny instead of completing unrelated tutorials.
- Use simulation before connecting real Codex sessions.
- Do not add advanced AI features until session attribution is reliable.

# First task

Begin with the smallest useful Dime&Penny model:

1. Install the C++ and Qt development environment.
2. Create the CMake project.
3. Define `AgentState`.
4. Define `Session`.
5. Define `PetIdentity`.
6. Implement `SessionRegistry`.
7. Write registration and duplicate-detection tests.
8. Build a console demonstration.
9. Explain how object ownership works.
10. Commit the working milestone.

The first milestone is complete when the console application can safely register simulated sessions, enforce the 10-session limit, reject duplicates, and display every session with its assigned pet identity.
