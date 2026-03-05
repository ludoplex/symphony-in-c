# Symphony in C

A portable implementation of Symphony using Cosmopolitan libc, producing a single
Actually Portable Executable (APE) that runs natively on Linux, Windows, macOS,
FreeBSD, OpenBSD, and NetBSD.

## Features

- **Single Binary**: One executable runs on all major operating systems
- **Built-in Web Server**: Redbean-style web server for dashboard and API
- **Embedded SQLite**: Persistent state management
- **Embedded Lua**: Template rendering and scripting
- **HTMX Dashboard**: Modern, reactive web UI

## Building

### Prerequisites

The Makefile will automatically download the Cosmopolitan toolchain on first build.

### Build

```bash
cd c
make
```

This produces `symphony.com`, an Actually Portable Executable.

### Run

```bash
./symphony.com [path-to-WORKFLOW.md] [--port PORT]
```

## Architecture

```
c/
├── src/                    # C source files
│   ├── main.c             # Entry point and CLI
│   ├── config.c           # Configuration management
│   ├── workflow.c         # WORKFLOW.md parser
│   ├── workspace.c        # Workspace management
│   ├── orchestrator.c     # Main orchestration loop
│   ├── tracker_linear.c   # Linear API client
│   ├── agent_runner.c     # Codex app-server client
│   ├── http_server.c      # Dashboard HTTP server
│   ├── template.c         # Lua template rendering
│   └── util.c             # Utility functions
├── include/               # Header files
├── vendors/               # Git submodules (cosmo-bde, cosmo-sokol)
├── web/                   # Web assets
│   ├── static/           # Static files (htmx.js, CSS)
│   └── templates/        # HTML templates
└── Makefile              # Build system
```

## Components

### Workflow Loader
Parses `WORKFLOW.md` with YAML front matter and Markdown prompt template.

### Config Layer
Typed configuration with defaults, `$VAR` environment expansion, and `~` home expansion.

### Orchestrator
Single-authority polling loop that manages issue dispatch, concurrency, retries,
and reconciliation.

### Workspace Manager
Creates isolated per-issue workspaces with lifecycle hooks.

### Linear Tracker Client
GraphQL client for fetching and normalizing issues from Linear.

### Agent Runner
JSON-RPC client for the Codex app-server subprocess.

### HTTP Server
Dashboard UI and REST API for observability.

## License

Apache License 2.0
