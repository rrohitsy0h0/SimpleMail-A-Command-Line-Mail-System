# SimpleMail - Mini Project 2

Department of Computer Science and Engineering  
Computer Network Lab (CS39006)

Name: Rohit Ranjeet Satpute  
Roll No: 23CS10060

## Overview

This project implements a command-line email system consisting of two programs:
- **`smserver`**: The SimpleMail server — stores and manages mailboxes for registered users, handles both SMTP2 (sending) and SMP (receiving) protocols concurrently.
- **`smclient`**: The SimpleMail client — connects to the server to send and retrieve mail through an interactive menu-driven interface.

Both protocols are text-based, operate over TCP, and use `\r\n` (CRLF) as the line terminator.

## Build and Run

Build:

```bash
make
```

Run server:

```bash
./smserver <port> <userfile>
```

Run client:

```bash
./smclient <server_ip> <port>
```

Example:

```bash
./smserver 9000 users.txt
./smclient 127.0.0.1 9000
```

Clean:

```bash
make clean
```

## User File Format

Each line contains a username and password separated by a single space:

```text
alice secretpass1
bob hunter2
charlie x9Kp2mW
```

Validation rules enforced on startup:
- **Username**: lowercase alphabetic only (`a-z`), length 1–20
- **Password**: alphanumeric only (`a-z`, `A-Z`, `0-9`), length 1–30
- Duplicate usernames are rejected
- Extra fields on a line cause rejection
- Any malformed entry causes the server to print an error and exit

## Protocol Implementation

### Connection Handshake

1. Server sends: `WELCOME SimpleMail v1.0`
2. Client sends `MODE SEND` or `MODE RECV`
3. Server responds with `OK` or `ERR Unknown mode`
4. If no mode is received within 30 seconds, the server closes the connection

### SMTP2 (Sending Protocol)

Used when the client selects `MODE SEND`. No authentication required.

**Command sequence** (strictly enforced):

| Step | Client sends | Server responds |
|------|-------------|-----------------|
| 1 | `FROM <display_name>` | `OK Sender accepted` |
| 2 | `TO <username>` (one or more) | `OK Recipient accepted` or `ERR No such user` |
| 3 | `SUB <subject>` or `SUB` (empty) | `OK Subject accepted` |
| 4 | `BODY` | `OK Send body, end with CRLF.CRLF` |
| 5 | Body lines, terminated by `.` on its own line | `OK Delivered to N mailboxes` |
| 6 | `QUIT` | `BYE` |

**Edge cases handled:**
- Out-of-sequence commands → `ERR Bad sequence`
- Nonexistent recipient → `ERR No such user`
- Recipient list exceeds server capacity (`MAX_USERS`) → `ERR Too many recipients`
- No valid recipients when `BODY` is sent → `ERR No valid recipients`
- Empty subject (`SUB` with no argument) → stored as `(no subject)`
- Dot-stuffing: body lines starting with `.` are prepended with an extra `.` by the client; the server strips the leading extra dot on receipt
- Body size limit: 65536 bytes after de-stuffing; exceeded → `ERR Body too large`, server drains remaining body until dot terminator
- Duplicate recipients: accepted but stored only once
- Line length: max 512 bytes including CRLF; exceeded → `ERR Line too long`
- `QUIT` is accepted at any state in the SMTP2 session

**Stored mail format:**

```text
From: <display_name>
To: <comma-separated accepted recipients>
Subject: <subject_text>
Date: <YYYY-MM-DD HH:MM:SS>
---
<body text>
```

### SMP (Retrieval Protocol)

Used when the client selects `MODE RECV`. Requires authentication.

**Authentication flow:**
1. Server sends `AUTH REQUIRED <nonce>` (8-character random alphanumeric token)
2. Client computes DJB2 hash of `password || nonce` (password concatenated with nonce, no separator)
3. Client sends `AUTH <username> <hash>` (hash as unsigned decimal integer)
4. Server verifies by computing the same hash with stored password
5. Success → `OK Welcome <username>`; Failure → `ERR Authentication failed`
6. After 3 failed attempts → `ERR Too many failures` and connection is closed
7. Raw password is never sent over the network

**Mailbox commands** (available after authentication, any order):

| Command | Response |
|---------|----------|
| `LIST` | `OK <count> messages` + tab-separated summaries + `.` terminator |
| `READ <id>` | `OK` + mail content (dot-stuffed) + `.` terminator |
| `DELETE <id>` | `OK Deleted` (deleted IDs are never reused) |
| `COUNT` | `OK <count>` (implemented on server; current `smclient` menu does not expose this option) |
| `QUIT` | `BYE` |

- Nonexistent message → `ERR No such message`
- Unknown command → `ERR Unknown command`

## Design Choices

### Server Architecture

- **Concurrency model**: Single-process event loop using `select()`. No forking or threading.
- **Socket handling**: One persistent listening socket + one accepted socket per connected client. All sockets are set to non-blocking using `fcntl()`.
- **Per-client state**: Each client slot tracks its protocol mode (`mode_received`), SMTP2 state machine (`smtp2_state`), SMP state machine (`smp_state`), line parser buffer (`linebuf`), and connection timestamp for timeout enforcement.
- **Partial read handling**: The server accumulates data in per-client line buffers and only processes complete CRLF-terminated lines. Commands can arrive in fragments across multiple `read()` calls and are reassembled correctly.
- **Oversize line handling**: If a line buffer fills up without a CRLF, the server sends `ERR Line too long` and enters a drain mode to discard bytes until the next CRLF, preventing buffer overflow and protocol desynchronization.
- **Body overflow handling**: When the body exceeds 65536 bytes, the server sends `ERR Body too large` and sets a `body_overflow` flag. It continues to silently discard incoming body lines until the dot terminator is received, then resets the SMTP2 state cleanly instead of misinterpreting body lines as protocol commands.
- **Timeout**: Clients that do not send a `MODE` command within 30 seconds of connecting are disconnected by `check_timeout()`.
- **Graceful shutdown**: On `SIGINT`/`SIGQUIT`, active connections are closed but mailboxes are preserved on disk.

### Client Architecture

- **Event-driven design**: The client uses `select()` to multiplex between stdin (user input) and the server socket, avoiding blocking on either.
- **State machines**: Three enum-based state machines drive the protocol logic:
  - `mode_state`: `MODE_CHK` (main menu) → `MODE_SEND` / `MODE_RECV` → `QUIT`
  - `send_state`: `STP` → `FROM` → `TO` → `SUBJECT` → `BODY_INIT` → `BODY` → `STP`
  - `recv_state`: `RECV_USER` → `RECV_PASS` → `RECV_MENU` → `RECV_READ_ID` / `RECV_DEL_ID`
- **Two handler functions**:
  - `cli_read()`: Handles user input from stdin — reads a line, constructs the appropriate protocol command based on current state, and sends it to the server.
  - `server_read()`: Handles server responses — reads a line from the server, parses the response based on current state, prints user-friendly output, and transitions state.
- **Connection management**: The client connects at startup (blocking), reads the server greeting, then switches the socket to non-blocking for the event loop. After each complete action (`QUIT`/`BYE`), `reconn()` closes the old socket, creates a new connection, reads the greeting, switches to non-blocking, and updates the `fd_set`.
- **Clean separation**: Protocol logic (in `cli_read` and `server_read`) is fully separated from the UI (in `menu()` and `mail_menu()`). The user never sees raw protocol commands or responses.
- **Mailbox UI scope**: The retrieval menu currently exposes LIST/READ/DELETE/LOGOUT; `COUNT` exists in the wire protocol and server implementation but is not mapped to a menu entry.
- **Partial write handling**: `send_t()` wraps `send_all()` which retries on `EAGAIN`/`EWOULDBLOCK` and handles partial writes. The CRLF delimiter is appended automatically by `send_t()`.
- **CRLF line reading**: `read_line()` reads one byte at a time, detects `\r\n`, strips it, and null-terminates the result. On non-blocking sockets, it retries on `EAGAIN`/`EWOULDBLOCK`.

### Mailbox ID Strategy

- Mail files are stored under `mailboxes/<user>/` as `<id>.txt`.
- On startup, the server scans each user's mailbox directory and sets `next_id` to `max_existing_id + 1`.
- During runtime, IDs increase monotonically in memory.
- Deleted message IDs are never reused, even after deletion.

### Dot-Stuffing

- **Client side (sending)**: If a body line starts with `.`, the client prepends an additional `.` before sending.
- **Server side (receiving body)**: The server strips the leading extra `.` from received body lines that start with `..`.
- **Server side (sending via READ)**: When sending mail content back, lines starting with `.` are prepended with an extra `.`.
- **Client side (receiving via READ)**: Lines starting with `..` have the leading dot stripped before display.

## Logging

Server prints timestamped logs (`[YYYY-MM-DD HH:MM:SS]`) for:
- Server startup with port and user count (including filename)
- New connections (client IP and port)
- Mode selection
- Authentication success/failure
- Mail delivery (sender name, recipient list, delivered count)
- Mailbox access (LIST, READ, DELETE with username and message ID)
- Client disconnection (normal, QUIT, or timeout)
- Server shutdown

## Assumptions and Additional Design Notes

- Line endings on the wire are strictly CRLF (`\r\n`).
- The sender name in SMTP2 (`FROM`) is a free-form display name, not a registered user, and does not require authentication as specified.
- Usernames are matched case-insensitively (`strcasecmp`) but stored in lowercase.
- Mail storage uses flat files on disk, not a database.
- The client reuses a single persistent TCP connection per action. After `QUIT`/`BYE`, it reconnects fresh for the next menu action. The initial connection on startup verifies server reachability.
- Server shutdown (`SIGINT`/`SIGQUIT`) preserves all mailbox data on disk so mail survives across restarts.
- The DJB2 hash function uses `unsigned long` as the hash type (matching the reference implementation in the spec). The sample hash value `7572886318025498` for `hunter2a3bK9xQ2` has been verified.
- Protocol message formats are aligned with the wire-level trace in the assignment spec to ensure cross-student compatibility.
- Body-too-large handling: after sending `ERR Body too large`, the server drains (discards) remaining body lines until the dot terminator before resetting state, preventing protocol desynchronization.
- Duplicate recipients in `TO` commands are accepted (respond `OK`) but deduplicated before delivery.
