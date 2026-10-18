# Local IPC server (Task 6)

`dimepenny::ipc::LocalServer` is a reusable Qt event-loop component implemented
with QLocalServer and QLocalSocket. It is linked through DimePenny::Core. Construct,
configure and use it in one thread with a running Qt event loop; do not move it
between threads. The placeholder desktop application does not start it yet.

## Trusted provisioning and connection flow

1. The application calls `authorize(sessionId)` for a session it intends to admit.
   The returned token contains 256 random bits from Qt's system random generator,
   encoded as 64 lowercase hex characters. Deliver it privately to that bridge.
   Credential provisioning UI/process launch is subsequent work; do not publish
   tokens, put them in command-line arguments, or write them into logs.
2. Call `listen(endpoint)` with an application-selected local name. The endpoint
   requests Qt's UserAccessOption. Listen failures are returned with errorString;
   the server never removes an existing endpoint to force a successful listen.
3. The bridge's first frame must be a valid `register` with `payload.authToken`.
   It must match the provisioned secret for that exact envelope sessionId. Missing,
   invalid, unknown-session and incorrect secrets close the connection.
4. Authentication binds the socket to that session for its entire lifetime. A
   second live connection for the same session is rejected. The server returns
   registration_result, emits sessionAuthenticated, and forwards the registration
   via messageReceived with authToken removed.
5. Later frames must retain the bound sessionId. Heartbeats are handled internally;
   event/error messages reach messageReceived. A disconnect closes only that client.
   Registration repeats and server-only commands from clients are rejected.

This is bearer-token authentication within a local user boundary. It does not
protect against a compromised account/process that has obtained the token. A
secret stays valid across reconnects until revoke, rotation, or server shutdown.
`authorize` rotates an existing credential and disconnects its old connection;
`revoke` invalidates the credential and disconnects that session. `stop` closes
all connections, clears credentials and stops the listener/timer.

Transport registration_result currently acknowledges authenticated transport
admission, not SessionRegistry mutation. The future composition layer must
coordinate domain admission before exposing successful activation to users. This
component does not invent verified Codex events or update pets. It exposes
messageReceived/sessionDisconnected for the later event-dispatch integration.

## Limits and liveness

- At most 10 accepted connections, including unauthenticated handshakes. An
  unauthenticated connection reserves a slot only until its handshake deadline.
- Read chunks and the socket read buffer are bounded to 65,540 bytes. Each event
  loop callback processes one chunk, scheduling remaining data for another turn.
- Pending output is capped at four maximum frames. Overflow or failed writes close
  the client. Successful `send` means queued, not acknowledged by the peer.
- Handshake timeout: 5 seconds from acceptance; partial input does not extend it.
- Partial-frame deadline: 5 seconds from the start of an incomplete frame. Dripping
  bytes does not reset it; finishing a frame permits a new frame deadline.
- Server ping interval: 5 seconds. The client must pong with the exact ping ID
  within 15 seconds. Other traffic does not satisfy an outstanding ping.
- Client pings get pongs without triggering reply loops. Unexpected or mismatched
  pongs close the connection.
- Tick interval: 100 ms. Deadlines use elapsed monotonic time, independent of wall
  clock changes. Incoming data is also checked against expired deadlines.
- EOF, socket errors, malformed frames, incorrect direction and session-ID changes
  remove only the offending connection. sessionDisconnected emits once for a
  previously authenticated session, with a generic reason and no payload secrets.

All timing values can be configured with positive `ServerOptions` fields. `send`
allows application-originated approval_response and error messages only, routed
to the currently authenticated session. Registry/state-machine rejection, pending
approval ownership, expiry and decision auditing remain application responsibilities.

## Tests

`dimepenny.ipc.server` uses real local sockets with unique temporary endpoint names.
It covers ten clients, capacity, session spoofing, duplicate logins, wrong/missing
credentials, token rotation/revocation, reconnects, event delivery, server replies,
abrupt disconnect, handshakes, heartbeat correlation/timeouts, malformed frames,
stalled frames and graceful disconnect. Tests require permission to create local
Windows named pipes; they do not contact a remote service.
