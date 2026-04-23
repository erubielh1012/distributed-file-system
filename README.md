# Programming Assignment 4 - Distributed File System

## Introduction
In this assignment, you will build, in C, a distributed file system for reliable and secure file storage.

A Distributed File System (DFS) is a client/server-based application that allows a client to store and retrieve files on multiple servers. One of the features of a DFS is that each file is divided into chunks and stored on different servers and can be reconstructed even if one of the servers is not responding.

In this assignment, a DFS client (DFC) uploads and downloads to and from multiple distributed file servers (DFS1, DFS2, DFS3, DFS4). These servers can run locally on a single machine using different ports (e.g., 10001–10004).

When uploading a file:
- Split the file into 4 chunks: P1, P2, P3, P4
- Group into pairs:
  - (P1, P2)
  - (P2, P3)
  - (P3, P4)
  - (P4, P1)
- Upload each pair to a different DFS server

This creates redundancy so one failed server does not prevent reconstruction.

---

## Chunk Distribution Logic

Compute:
x = HASH(filename) % y

Where:
- y = number of DFS servers (4)
- Use MD5 hash

### Chunk Placement Table

| x | DFS1  | DFS2  | DFS3  | DFS4  |
|---|-------|-------|-------|-------|
| 0 | (1,2) | (2,3) | (3,4) | (4,1) |
| 1 | (4,1) | (1,2) | (2,3) | (3,4) |
| 2 | (3,4) | (4,1) | (1,2) | (2,3) |
| 3 | (2,3) | (3,4) | (4,1) | (1,2) |

---

## Requirements for the DFC (Client)

### Execution
./dfc \<command> [filename]

- Non-interactive program
- Executes command and exits

### Configuration File (~/dfc.conf)
server dfs1 127.0.0.1:10001\
server dfs2 127.0.0.1:10002\
server dfs3 127.0.0.1:10003\
server dfs4 127.0.0.1:10004\
...\
server dfsn 127.0.0.1:1000n

### Supported Commands

#### list
- Lists files stored across DFS servers
- Indicates incomplete files (if file chunks available in DFS servers are not enough to reconstruct original file then it indicates some servers are not available):
  - "filename [incomplete]"

#### get
- Downloads all chunks
- Reconstructs file if possible
- If not:
  print error: 
    - "\<filename> is incomplete"

#### put
- Uploads file chunks to servers
- If insufficient servers:
  respond with: 
    - "\<filename> put failed"

---

## Requirements for the DFS (Server)

### Execution
./dfs ./dfs1 10001 &
./dfs ./dfs2 10002 &
./dfs ./dfs3 10003 &
./dfs ./dfs4 10004 &

### Server Setup
- Each server has its own directory:
  dfs1/, dfs2/, dfs3/, dfs4/

### Behavior
- Client timeout: 1 second
- If no response → server considered unavailable
- Should support multiple clients concurrently (extra credit)

---

## Grading Script

https://github.com/TuanTRAN-CUBoulder/NetworkSystems_GradingScript.git

---

## Submission Requirements

Submit the following files:

- dfc.c
- dfs.c
- grading_script_result.png

### Notes
- Code must be your own work
- Must compile and run on Ubuntu VM
- Grading script must be executed on Ubuntu for valid results
