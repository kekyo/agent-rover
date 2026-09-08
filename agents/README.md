# agent-rover Agent Protocol

This document describes the current agent-rover native agent protocol. It is
written from the point of view of a custom agent implementer that wants to be
compatible with the TypeScript driver in `agent-rover/src/driver`.

The current production agent is the Windows native TCP agent under
`agents/windows`. The TypeScript driver currently validates `platform` as
`"windows"`, so non-Windows agents must still report the current Windows-shaped
schema unless the driver is extended.

## Current Versions

| Name | Value | Source |
| --- | --- | --- |
| JSON protocol version | `2026-09-09` | `protocolVersion` / `kProtocolVersion` |
| TCP frame version | `2` | `tcpFrameVersion` / `kTcpFrameVersion` |
| TCP transport capability | `transport.tcp-frame-v1` | `tcpFrameCapabilityId` / `kTcpFrameCapabilityId` |
| Default frame payload limit | `16 * 1024 * 1024` bytes | driver and native agent |
| Default request timeout | `30000` ms | TypeScript driver |
| Binary transfer chunk size used by driver and native agent | `64 * 1024` bytes | implementation detail, peers must accept other positive chunk sizes |

The driver rejects the connection during the ready handshake when the reported
JSON protocol version is not exactly `2026-09-09`.

## Connection Lifecycle

The agent is a TCP server. The TypeScript driver opens one TCP connection and
then speaks the agent-rover TCP frame protocol on that stream.

1. The driver connects to the configured host and port.
2. If the agent requires authentication, the agent immediately sends an
   `AuthChallenge` frame and waits for an `AuthResponse` frame. If
   authentication fails, close the socket without sending `agent.ready`.
3. If the agent does not require authentication, skip the challenge.
4. After authentication succeeds, the agent sends the `agent.ready` JSON event.
5. The driver waits for `agent.ready`, validates `capabilities`, and then starts
   sending request messages.
6. The connection remains open until either side sends a `Close` frame or the
   socket is closed.

The reference Windows server handles one accepted client at a time, but the wire
protocol does not require that limitation. A custom agent may support multiple
simultaneous connections if every connection has its own pending requests and
binary transfer state.

## TCP Frame Layer

Every item on the TCP stream is a frame. Multi-byte integer fields are
little-endian.

### Header Layout

Each frame starts with a fixed 20-byte header:

| Offset | Size | Type | Field | Required value |
| --- | --- | --- | --- | --- |
| 0 | 4 | ASCII | magic | `TRVR` |
| 4 | 2 | uint16 | version | `2` |
| 6 | 2 | uint16 | kind | one of the frame kinds below |
| 8 | 4 | uint32 | flags | `0` |
| 12 | 4 | uint32 | payloadLength | number of payload bytes following the header |
| 16 | 4 | uint32 | reserved | `0` |

The payload immediately follows the header and is exactly `payloadLength` bytes.
Peers should treat invalid magic, unsupported version, unknown kind, non-zero
flags, non-zero reserved fields, or an excessive payload length as protocol
errors and close the connection.

### Frame Kinds

| Kind | Name | Payload |
| --- | --- | --- |
| `1` | `Json` | UTF-8 JSON protocol message |
| `2` | `Binary` | Binary transfer chunk payload |
| `3` | `Ping` | Empty payload |
| `4` | `Pong` | Empty payload |
| `5` | `Close` | Empty payload |
| `6` | `AuthChallenge` | 32 random bytes from agent to driver |
| `7` | `AuthResponse` | 32-byte HMAC response from driver to agent |

Agents must respond to `Ping` with `Pong`. A received `Pong` requires no action.
When an agent receives `Close`, it should close the socket. During normal
shutdown the driver sends a `Close` frame and then ends the socket.

`AuthChallenge` is only valid from the agent before authentication completes.
`AuthResponse` is only valid from the driver after receiving a challenge. Any
other use should close the connection.

## Authentication

Authentication is optional. The native agent enables it by default and prints a
token at startup. The driver sends a token through
`connectRemoteAgent({ authToken })` or through the `AGENT_ROVER_AUTH_TOKEN`
environment variable.

The native Windows agent defaults to `--host 0.0.0.0`, `--port 39397`, and
authentication enabled. `--no-auth` or `-n` disables authentication.
`--unsafe-token <token>` sets a fixed ASCII token for repeatable testing and
cannot be combined with `--no-auth`. Generated tokens use 32 random bytes encoded
as unpadded base64url text, producing a 43-character token. Explicit
`--unsafe-token` values must be non-empty ASCII strings of at most 64 bytes.

When authentication is required:

1. Agent generates 32 cryptographically random bytes.
2. Agent sends them as an `AuthChallenge` frame payload.
3. Driver computes:

```text
HMAC-SHA-256(
  key = UTF-8 bytes of token,
  message = ASCII bytes "agent-rover-auth-v1\0" followed by the 32 challenge bytes
)
```

4. Driver sends the raw 32-byte digest as an `AuthResponse` frame payload.
5. Agent compares the received digest with the expected digest. Use a
   constant-time comparison when possible.
6. On success, agent sends `agent.ready`. On failure, agent closes the socket.

The terminal NUL byte after `agent-rover-auth-v1` is part of the HMAC message.

## JSON Protocol Layer

JSON frame payloads are UTF-8 encoded JSON objects. The shared TypeScript
validator accepts only JSON-compatible values: `null`, booleans, finite numbers,
strings, arrays, and objects. Request and response ids are non-empty strings.

### Request

Requests are sent by the driver:

```json
{
  "kind": "request",
  "id": "req-1",
  "method": "agent.bounds",
  "params": {
    "example": true
  }
}
```

`params` is omitted when a method has no parameters. Agents should treat missing
`params` as an empty object. Request ids are opaque strings; the current driver
generates `req-1`, `req-2`, and so on, but agents must echo whatever id was
received.

The driver may have multiple requests in flight. Responses may be returned in
any order as long as each response uses the matching request id.

### Success Response

```json
{
  "kind": "response",
  "id": "req-1",
  "ok": true,
  "result": {}
}
```

`result` may be omitted. Methods that conceptually return `void` normally return
`null` in the current native agent, and the driver accepts that.

### Failure Response

```json
{
  "kind": "response",
  "id": "req-1",
  "ok": false,
  "error": {
    "code": "PROTOCOL_ERROR",
    "message": "Human-readable diagnostic message."
  }
}
```

Current agent failures use `PROTOCOL_ERROR`. The protocol error code type also
contains `AUTHENTICATION_FAILED`, `CHECKSUM_MISMATCH`, `DISCONNECTED`,
`TIMEOUT`, and `TRANSFER_CANCELLED`; most of those are raised locally by the
driver rather than returned by the agent.

Unknown methods and invalid method parameters should normally produce a failure
response instead of closing the socket. Malformed frames, invalid frame kinds,
invalid binary transfer chunks, and authentication errors should close the
socket.

### Event

Events are sent without a request id:

```json
{
  "kind": "event",
  "name": "agent.ready",
  "data": {}
}
```

The only event currently required by the driver is `agent.ready`.

### Reserved JSON Transfer Messages

The shared protocol helper can parse these JSON message kinds:

```json
{
  "kind": "transfer.chunk",
  "transferId": "transfer-1",
  "sequence": 0,
  "dataBase64": "AAECAw==",
  "final": true,
  "totalBytes": 4,
  "sha256": "..."
}
```

```json
{
  "kind": "transfer.cancel",
  "transferId": "transfer-1"
}
```

The current TCP driver does not route JSON `transfer.chunk` messages to the
remote agent APIs. Custom TCP agents should use `Binary` frames for
screenshots and file transfers.

## Ready Handshake

Immediately after authentication, the agent must send:

```json
{
  "kind": "event",
  "name": "agent.ready",
  "data": {
    "capabilities": {
      "protocolVersion": "2026-09-09",
      "platform": "windows",
      "features": [
        "capabilities",
        "clipboard.clear",
        "clipboard.readText",
        "clipboard.writeText",
        "agent.bounds",
        "agent.cursor",
        "agent.monitors",
        "agent.screenshot",
        "agent.recordVideo",
        "agent.windows",
        "window.children",
        "applications.launch",
        "window.activate",
        "window.close",
        "window.focus",
        "window.setBounds",
        "window.show",
        "window.screenshot",
        "window.recordVideo",
        "window.snapshot",
        "input.perform",
        "file.read",
        "file.write",
        "file.exists",
        "file.stat",
        "file.mkdir",
        "file.readdir",
        "file.manifest",
        "file.remove",
        "file.rename",
        "file.mkdtemp",
        "process.kill",
        "process.killManaged",
        "process.list",
        "process.launchManaged",
        "process.managedSnapshot",
        "process.releaseManaged",
        "process.snapshot",
        "eventLogs.read",
        "transport.tcp-frame-v1",
        "agent.native-windows"
      ]
    },
    "protocolVersion": "2026-09-09"
  }
}
```

The driver validates `data.capabilities`:

- `protocolVersion` must be a string and must equal `2026-09-09`.
- `platform` must be the string `windows`.
- `features` must be an array of strings.

The feature list is informational in the current driver. Missing features do not
disable public APIs; unsupported methods fail only when called.

## Binary Transfers

Large binary values are sent through `Binary` frames. This is used for:

- Agent to driver: `agent.screenshot`
- Agent to driver: `agent.recordVideo` / `video.result`
- Agent to driver: `window.screenshot`
- Agent to driver: `window.recordVideo` / `video.result`
- Agent to driver: `file.read`
- Driver to agent: `file.write`

Each `Binary` frame payload contains:

| Offset | Size | Type | Field |
| --- | --- | --- | --- |
| 0 | 4 | uint32 little-endian | metadataLength |
| 4 | metadataLength | UTF-8 JSON object | chunk metadata |
| 4 + metadataLength | remaining bytes | raw binary data for this chunk |

The chunk metadata object is:

```json
{
  "transferId": "req-1-screenshot",
  "sequence": 0,
  "final": true,
  "contentType": "image/png",
  "totalBytes": 1234,
  "sha256": "lowercase-hex-sha-256"
}
```

Required fields on every chunk:

- `transferId`: non-empty string shared by all chunks in one transfer.
- `sequence`: zero-based non-negative integer.
- `final`: boolean.
- `contentType`: non-empty string.

Required fields on the final chunk:

- `totalBytes`: total byte count after concatenating all chunks.
- `sha256`: SHA-256 hex digest of the concatenated bytes. The driver produces
  and compares lowercase hex.

Receiver rules:

- The first chunk for a transfer must have `sequence: 0`.
- Chunks must be contiguous: `0`, `1`, `2`, and so on.
- `contentType` should remain the same for every chunk in a transfer.
- A zero-length payload is represented as one final chunk with empty raw data,
  `totalBytes: 0`, and the SHA-256 of an empty byte string.
- When the final chunk arrives, logically reassemble the ordered raw data and
  verify both `totalBytes` and `sha256`. Receivers may write chunks directly to
  disk instead of retaining the complete payload in memory; the TypeScript
  driver does this for `video/mp4` transfers.

For downloads, the agent sends binary chunks and a JSON response whose result
contains a transfer reference. The current driver accepts either order. The
native agent sends screenshot and file chunks before the response, but sends
the video response before streaming its chunks from disk. The transfer must
finish before the request timeout expires.

For uploads, the current driver sends all binary chunks first, then sends the
JSON request referencing that transfer. An agent should store completed
transfers by `transferId` until the corresponding method consumes them.

## Common Data Shapes

### Rect

Coordinates are physical screen pixels in the virtual screen coordinate
system. Multi-monitor systems may use negative `x` or `y` values.

```json
{
  "x": 0,
  "y": 0,
  "width": 1024,
  "height": 768
}
```

### Point

```json
{
  "x": 12,
  "y": 34
}
```

### Capabilities

```json
{
  "protocolVersion": "2026-09-09",
  "platform": "windows",
  "features": ["transport.tcp-frame-v1"]
}
```

### Window Snapshot

```json
{
  "id": "0x1001",
  "title": "Untitled - Notepad",
  "className": "Notepad",
  "bounds": { "x": 10, "y": 20, "width": 640, "height": 480 },
  "visible": true,
  "active": false,
  "focused": false,
  "enabled": true,
  "minimized": false,
  "maximized": false,
  "controlId": 0,
  "process": {
    "id": 1001,
    "name": "notepad.exe",
    "path": "C:\\Windows\\System32\\notepad.exe"
  }
}
```

All fields above are required by the driver. Extra fields such as `parentId` are
ignored.

`id` is an agent-defined stable identifier for the current screen session. The
Windows agent uses handle-derived strings, but custom agents only need to make
the id usable by later `window.*` requests.

### Monitor

```json
{
  "id": "monitor-1",
  "name": "DISPLAY1",
  "bounds": { "x": 0, "y": 0, "width": 1024, "height": 768 },
  "workArea": { "x": 0, "y": 0, "width": 1024, "height": 728 },
  "primary": true,
  "scaleFactor": 1
}
```

### Cursor

```json
{
  "point": { "x": 12, "y": 34 },
  "visible": true
}
```

### Screenshot Result

Screenshot methods return metadata and send an `image/png` binary transfer:

```json
{
  "transferId": "req-4-screenshot",
  "contentType": "image/png",
  "totalBytes": 1234,
  "sha256": "lowercase-hex-sha-256",
  "bounds": { "x": 10, "y": 20, "width": 640, "height": 480 },
  "visibleBounds": { "x": 10, "y": 20, "width": 640, "height": 480 },
  "clipped": false
}
```

The driver also accepts `imageBase64` in screenshot results, but the current
native TCP agent uses binary transfers. Prefer binary transfers for custom TCP
agents.

### Video Result

`video.result` returns capture metadata and a `video/mp4` binary transfer
reference:

```json
{
  "transferId": "req-5-video-mp4",
  "contentType": "video/mp4",
  "codec": "h264",
  "totalBytes": 245760,
  "sha256": "lowercase-hex-sha-256",
  "durationMs": 1500,
  "fps": 60,
  "frameCount": 90,
  "droppedFrames": 0,
  "initialBounds": { "x": 10, "y": 20, "width": 640, "height": 480 },
  "finalBounds": { "x": 90, "y": 80, "width": 640, "height": 480 },
  "clipped": false
}
```

`frameCount` is the number of samples written to the encoder.
`droppedFrames` counts capture deadlines skipped because capture or encoding
could not keep up. `initialBounds` and `finalBounds` describe the target at the
start and end of recording; encoded dimensions remain fixed from the initial
bounds.

### File Stat

```json
{
  "type": "file",
  "size": 42,
  "createdAt": "2026-06-25T00:00:00.000Z",
  "modifiedAt": "2026-06-25T00:00:00.000Z"
}
```

`type` must be one of `file`, `directory`, or `other`. Timestamps are strings;
ISO 8601 UTC strings are recommended.

### Directory Entry

Directory entries include the file stat fields plus `name`:

```json
{
  "name": "example.txt",
  "type": "file",
  "size": 42,
  "createdAt": "2026-06-25T00:00:00.000Z",
  "modifiedAt": "2026-06-25T00:00:00.000Z"
}
```

### Application Process

```json
{
  "id": 4321,
  "name": "notepad.exe"
}
```

### Process Snapshot

```json
{
  "id": 4321,
  "name": "notepad.exe",
  "path": "C:\\Windows\\System32\\notepad.exe",
  "parentProcessId": 1234,
  "createdAt": "2026-07-02T00:00:00.000Z",
  "running": true,
  "exitCode": null
}
```

`parentProcessId` and `createdAt` may be `null` when the platform cannot resolve
them. The TypeScript driver uses these fields to correlate managed process trees
with top-level windows.

`exitCode` must be `null` while unknown or while the process is still running;
otherwise it must be a finite number.

### Event Log Entry

```json
{
  "id": 1,
  "provider": "Application",
  "level": "Information",
  "timestamp": "2026-06-25T00:00:00.000Z",
  "message": "Agent started."
}
```

`level` must be one of `Critical`, `Error`, `Information`, `Verbose`, or
`Warning`.

## RPC Methods

This section lists the request `method`, the expected `params` object, and the
success `result` shape. Required fields are shown in examples.

### `agent.capabilities`

Params: omitted.

Result: `Capabilities`.

This is the request form of the same capability object sent in `agent.ready`.

### `agent.bounds`

Params: omitted.

Result: `Rect`.

Return the virtual screen bounds in physical pixels.

### `agent.monitors`

Params: omitted.

Result: `Monitor[]`.

### `agent.cursor`

Params: omitted.

Result: `Cursor`.

### `agent.screenshot`

Params:

```json
{
  "rect": { "x": 0, "y": 0, "width": 1024, "height": 768 }
}
```

`rect` is optional. When omitted, capture the full virtual screen.

Result: `Screenshot Result`.

Send the PNG image as an `image/png` binary transfer.

### `agent.recordVideo`

Params:

```json
{
  "durationMs": 1500,
  "fps": 60,
  "quality": 90,
  "rect": { "x": 0, "y": 0, "width": 1024, "height": 768 }
}
```

`rect` is optional. When omitted, record the full virtual screen. `durationMs`
must be a positive uint32. `fps` must be from 1 through 240 and `quality` must
be from 1 through 100.

Start recording asynchronously and return:

```json
{ "recordingId": "req-5-video" }
```

Only one recording is retained per connection until `video.result` consumes
it. The native Windows agent advertises this method only when the Media
Foundation entry points it requires are available.

### `agent.windows`

Params: omitted.

Result: `Window Snapshot[]`.

Return top-level application windows. Child windows are returned through
`window.children`.

### `window.children`

Params:

```json
{
  "windowId": "0x1001"
}
```

Result: `Window Snapshot[]`.

Return direct child windows for the supplied window id.

### `window.snapshot`

Params:

```json
{
  "windowId": "0x1001"
}
```

Result: `Window Snapshot`.

### `window.activate`

Params:

```json
{
  "windowId": "0x1001"
}
```

Result: `Window Snapshot`.

Bring the window to the foreground if the platform allows it. Return the updated
snapshot. If the platform cannot actually activate the window, return a failure
response instead of a stale success.

### `window.focus`

Params:

```json
{
  "windowId": "0x1001"
}
```

Result: `Window Snapshot`.

Set keyboard focus to the window or control when possible. Return the updated
snapshot.

### `window.show`

Params:

```json
{
  "windowId": "0x1001",
  "state": "restored"
}
```

`state` must be one of `minimized`, `maximized`, or `restored`.

Result: `Window Snapshot`.

### `window.setBounds`

Params:

```json
{
  "windowId": "0x1001",
  "bounds": { "x": 20, "y": 30, "width": 800, "height": 600 }
}
```

Result: `Window Snapshot`.

Move and resize the window. Return the updated snapshot.

### `window.close`

Params:

```json
{
  "windowId": "0x1001"
}
```

Result: `null`.

Request that the window closes.

### `window.screenshot`

Params:

```json
{
  "windowId": "0x1001"
}
```

Result: `Screenshot Result`.

Send the PNG image as an `image/png` binary transfer.

### `window.recordVideo`

Params:

```json
{
  "windowId": "0x1001",
  "durationMs": 1500,
  "fps": 60,
  "quality": 90,
  "tracking": "followWindow"
}
```

`tracking` must be `followWindow` or `initialBounds`. In `followWindow` mode,
resolve the current window rectangle for every frame. In `initialBounds` mode,
keep capturing the rectangle resolved when recording began. Return the same
asynchronous `recordingId` result as `agent.recordVideo`.

### `video.result`

Params:

```json
{ "recordingId": "req-5-video" }
```

Result: `Video Result`.

Wait for the requested recording if it has not finished, finalize the H.264
MP4, and return its metadata. Send the MP4 through `video/mp4` binary chunks
without requiring the whole file to be held in memory. The native Windows
agent sends the JSON response before these chunks; the driver accepts download
chunks either before or after the response.

### `input.perform`

Params is the input operation object itself, not wrapped in another property.

Keyboard down:

```json
{
  "kind": "keyboard.down",
  "key": "Shift"
}
```

Keyboard up:

```json
{
  "kind": "keyboard.up",
  "key": "Shift"
}
```

Keyboard press:

```json
{
  "kind": "keyboard.press",
  "key": "End",
  "modifiers": ["Control"]
}
```

Keyboard type:

```json
{
  "kind": "keyboard.type",
  "text": "Hello"
}
```

Mouse move:

```json
{
  "kind": "mouse.move",
  "point": { "x": 100, "y": 200 }
}
```

Mouse down:

```json
{
  "kind": "mouse.down",
  "button": "left",
  "point": null
}
```

Mouse up:

```json
{
  "kind": "mouse.up",
  "button": "left",
  "point": { "x": 100, "y": 200 }
}
```

Mouse click:

```json
{
  "kind": "mouse.click",
  "point": { "x": 100, "y": 200 },
  "button": "left",
  "modifiers": []
}
```

Mouse drag:

```json
{
  "kind": "mouse.drag",
  "from": { "x": 100, "y": 200 },
  "to": { "x": 300, "y": 400 },
  "button": "left",
  "modifiers": []
}
```

Mouse wheel:

```json
{
  "kind": "mouse.wheel",
  "deltaX": 0,
  "deltaY": -120,
  "point": null
}
```

Result: `null`.

Valid mouse buttons are `left`, `middle`, and `right`. Valid modifiers are
`Alt`, `Control`, `Meta`, and `Shift`.

The reference Windows agent supports common key names such as `Alt`, `Control`,
`Ctrl`, `Meta`, `Win`, `Windows`, `Shift`, `Enter`, `Escape`, `Esc`, `Tab`,
`Space`, `Backspace`, `Delete`, `End`, `Home`, arrow keys, `F1` through `F24`,
and single characters resolvable by `VkKeyScanW`.

### `clipboard.readText`

Params: omitted.

Result:

```json
{
  "text": "clipboard text"
}
```

Return an empty string when no text is available.

### `clipboard.writeText`

Params:

```json
{
  "text": "clipboard text"
}
```

Result: `null`.

### `clipboard.clear`

Params: omitted.

Result: `null`.

### `applications.launch`

Params:

```json
{
  "path": "notepad.exe",
  "arguments": ["example.txt"],
  "workingDirectory": "C:\\Temp",
  "environment": {
    "NAME": "value"
  },
  "stdoutPath": "C:\\Temp\\stdout.log",
  "stderrPath": "C:\\Temp\\stderr.log",
  "createNoWindow": false
}
```

Only `path` is required. All other fields are optional.

Result: `Application Process`.

### `process.launchManaged`

Params are the same as `applications.launch`, with optional
`killTreeOnRelease`. The TypeScript driver sends `killTreeOnRelease: true`
unless the caller explicitly disables release cleanup.

Result:

```json
{
  "managedProcessId": 1,
  "id": 4321,
  "name": "notepad.exe",
  "stdoutPath": "C:\\Temp\\stdout.log",
  "stderrPath": "C:\\Temp\\stderr.log"
}
```

`stdoutPath` and `stderrPath` may be `null`.

### `process.managedSnapshot`

Params:

```json
{
  "managedProcessId": 1
}
```

Result: `Process Snapshot`.

### `process.killManaged`

Params:

```json
{
  "managedProcessId": 1
}
```

Result: `null`.

### `process.releaseManaged`

Params:

```json
{
  "managedProcessId": 1
}
```

Result: `null`.

### `process.snapshot`

Params:

```json
{
  "processId": 4321
}
```

Result: `Process Snapshot`.

When the process does not exist, the current fake agent returns a non-running
snapshot. The native Windows agent returns the snapshot behavior implemented by
`SnapshotProcess`. Custom agents should prefer returning a non-running snapshot
instead of failing when the id is syntactically valid but no process is running.

### `process.list`

Params:

```json
{
  "name": "notepad.exe"
}
```

`name` is optional.

Result: `Process Snapshot[]`.

### `process.kill`

Params:

```json
{
  "processId": 4321
}
```

Result: `null`.

### `file.exists`

Params:

```json
{
  "path": "C:\\Temp\\example.txt"
}
```

Result:

```json
{
  "exists": true
}
```

### `file.stat`

Params:

```json
{
  "path": "C:\\Temp\\example.txt"
}
```

Result: `File Stat`.

Return a failure response when the path does not exist.

### `file.mkdir`

Params:

```json
{
  "path": "C:\\Temp\\example",
  "recursive": true
}
```

`recursive` and `ignoreMissing` are sent as booleans. An absent target fails unless
`ignoreMissing` is true. Recursive removal does not follow directory reparse points.
Each RPC performs one attempt; the driver owns the shared removal deadline
(default 10000 ms) and bounded backoff. `onLockedFile: "fail"` disables retries.
Failures carry structured OS details and driver retry results; diagnostic text is
not used to decide whether to retry. Removal never terminates a process.

Result: `null`.

### `file.readdir`

Params:

```json
{
  "path": "C:\\Temp"
}
```

Result: `Directory Entry[]`.

### `file.remove`

Params:

```json
{
  "path": "C:\\Temp\\example.txt",
  "recursive": false
}
```

`recursive` is always sent by the current driver as a boolean.

Result: `null`.

Agents should reject dangerous paths such as an empty string or drive roots.

### `file.rename`

Params:

```json
{
  "from": "C:\\Temp\\old.txt",
  "to": "C:\\Temp\\new.txt"
}
```

Result: `null`.

### `file.mkdtemp`

Params:

```json
{
  "prefix": "C:\\Temp\\agent-rover-"
}
```

Result:

```json
{
  "path": "C:\\Temp\\agent-rover-abc123"
}
```

### `file.read`

Params:

```json
{
  "path": "C:\\Temp\\example.bin"
}
```

Result:

```json
{
  "transferId": "req-10-file-read",
  "contentType": "application/octet-stream",
  "totalBytes": 1234,
  "sha256": "lowercase-hex-sha-256"
}
```

Send the file bytes as an `application/octet-stream` binary transfer.

The driver also accepts a legacy base64 result object with `dataBase64` and
`sha256`, but the current native TCP agent uses binary transfers. Prefer binary
transfers for custom TCP agents.

### `file.write`

The current driver uploads file bytes as an `application/octet-stream` binary
transfer before sending the request.

Params:

```json
{
  "path": "C:\\Temp\\example.bin",
  "transferId": "client-transfer-1",
  "contentType": "application/octet-stream",
  "totalBytes": 1234,
  "sha256": "lowercase-hex-sha-256"
}
```

Result: `null`.

The agent must consume the completed transfer, verify content type, size, and
checksum, then write the bytes to `path`.

The native Windows agent also accepts a legacy base64 upload:

```json
{
  "path": "C:\\Temp\\example.bin",
  "dataBase64": "AAECAw==",
  "sha256": "lowercase-hex-sha-256"
}
```

Custom agents only need the binary-transfer form for compatibility with the
current TypeScript driver.

### `eventLogs.read`

Params:

```json
{
  "source": "Application",
  "maxEntries": 50,
  "since": "2026-06-25T00:00:00.000Z"
}
```

All fields are optional.

Result: `Event Log Entry[]`.

The current diagnostics helper calls this method with `{ "maxEntries": 50 }`
when no event log query is supplied.

## Implementer Notes

- Parse the JSON `params` object. The current Windows agent's lightweight parser
  searches the whole request string for fields, but interoperable agents should
  implement normal JSON request parsing.
- Extra fields in results are allowed. Missing required fields will cause the
  TypeScript driver to raise `PROTOCOL_ERROR`.
- Use UTF-8 for all JSON strings. Use standard base64 only for legacy base64
  fields.
- Keep request ids opaque. Do not assume the `req-N` format.
- Keep `transferId` unique for every in-flight binary transfer.
- A failed method should return a failure response with the same request id. A
  broken transport should close the socket.
- Binary transfer chunks may arrive before the request that references them.
  Store completed uploads until consumed.
- Downloads may send binary chunks before or after the JSON response. The driver
  stores early completed transfers and waits for late transfers until the request
  timeout.
- The driver redacts fields named like token, payload, data, or dataBase64 in
  diagnostics, but agents should still avoid putting secrets in error messages.
- The current driver does not negotiate capabilities. If a method is listed in
  `features` but fails at runtime, the caller sees that method failure.

## Minimal Custom Agent Checklist

1. Listen on a TCP port and read/write 20-byte `TRVR` frames.
2. Implement optional auth challenge/response, including the NUL byte in the
   HMAC message prefix.
3. Send `agent.ready` with protocol version `2026-09-09`, platform `windows`,
   and a string feature array.
4. Decode JSON request frames and return matching JSON response frames.
5. Implement binary transfer chunk encode/decode, contiguous sequence
   validation, `totalBytes`, and `sha256` verification.
6. Store driver uploads by `transferId` until `file.write` consumes them.
7. For screenshot, video, and file download methods, send binary chunks and
    return a transfer reference in the JSON result.
8. Implement every RPC method that your tests will call. The driver does not
   call unsupported methods automatically after handshake.
9. Respond to `Ping` with `Pong` and handle `Close` by closing the socket.
10. Test with `connectRemoteAgent()` and a small script that calls
    `capabilities()`, `bounds()`, `windows()`, a screenshot method, and a file
    round trip.

### Native operation failures

File operation failures use `OPERATION_FAILED` with a `details` object containing
`operation`, `nativeOperation`, `path`, `osCode` (or null), and `reason`.
The path identifies the actual failing entry during recursive operations.
`reason` distinguishes `readOnly`, `sharingViolation`, `lockViolation`,
`accessDenied`, `notFound`, `directoryNotEmpty`, `busy`, and `unknown`.
An access-denied code alone does not identify a transient lock or a permanent
permission problem. Clients must use these fields instead of parsing `message`.
Process cleanup may additionally report an incomplete `stage`.

### Managed release completion

`process.releaseManaged` retains unfinished native cleanup state. It returns
`OPERATION_FAILED` with reason `busy` and stage `processExit` while a bounded
exit check finds live Job members. The driver retries under one deadline.
Termination, capture closure and handle closure errors retain the managed ID
for a later attempt; repeating an already completed release succeeds.
Managed launch requires successful Job assignment before resuming the process.
With `killTreeOnRelease: false`, capture-free release closes monitoring handles
without terminating the process. Captured processes must finish writing before
release can complete. The driver marks release complete only after native
release and removal of its temporary directory both succeed.

### Managed capture and tree state

`process.managedRunning` takes `managedProcessId` and returns whether the Job
contains active processes, including descendants whose parents have exited.
`process.readCaptured` takes `managedProcessId` and `stream` (`stdout` or
`stderr`) and returns a binary file-transfer reference. While the root runs it
reads a snapshot using write sharing. After root exit it reports `busy` until
all Job members exit, then reads without write sharing to reject remaining
writers. General `file.read` retains its original sharing behavior.
The driver waits for in-flight capture reads before releasing their resources.
