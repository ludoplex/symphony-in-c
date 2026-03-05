# Symphony in C

A portable implementation of Symphony using Cosmopolitan libc, producing a single
Actually Portable Executable (APE) that runs natively on Linux, Windows, macOS,
FreeBSD, OpenBSD, and NetBSD.

## Features

- **Single Binary**: One executable runs on all major operating systems
- **Built-in Web Server**: HTTP server for dashboard and REST API
- **Embedded SQLite**: Persistent state management (via Cosmopolitan)
- **Embedded Lua**: Template rendering and scripting (via Cosmopolitan)
- **HTMX Dashboard**: Modern, reactive web UI
- **Zero Dependencies**: All required libraries statically linked

## Building

### Quick Start (Native Build)

For development and testing on Linux:

```bash
cd c
make native    # Build with system GCC
./symphony --help
```

This requires `libcurl-dev` and `pthreads` to be installed:

```bash
# Debian/Ubuntu
sudo apt-get install libcurl4-openssl-dev

# macOS
brew install curl
```

### Cosmopolitan Build (APE)

To build a truly portable binary that runs on Linux, Windows, macOS, and BSDs:

1. **Download the Cosmopolitan toolchain** (if not auto-downloaded):
   ```bash
   mkdir -p build/cosmopolitan
   cd build
   curl -LO https://cosmo.zip/pub/cosmocc/cosmocc-4.0.2.zip
   unzip cosmocc-4.0.2.zip -d cosmopolitan
   cd ..
   ```

2. **Build**:
   ```bash
   make cosmo
   ```

3. **Run anywhere**:
   ```bash
   ./symphony.com   # Works on Linux, Windows, macOS, FreeBSD, etc.
   ```

The resulting `symphony.com` file is an Actually Portable Executable that contains
everything needed to run the application on any supported operating system.

## Usage

```bash
# Basic usage
./symphony [WORKFLOW_PATH] [OPTIONS]

# With HTTP dashboard
./symphony --port 8080

# Use specific workflow file
./symphony /path/to/WORKFLOW.md

# Show version
./symphony --version
```

### Environment Variables

- `LINEAR_API_KEY`: API key for the Linear issue tracker

### WORKFLOW.md Configuration

Symphony reads configuration from a `WORKFLOW.md` file with YAML front matter:

```markdown
---
tracker:
  kind: linear
  project_slug: my-project
  api_key: $LINEAR_API_KEY

polling:
  interval_ms: 30000

workspace:
  root: ~/symphony_workspaces

agent:
  max_concurrent_agents: 5

codex:
  command: codex app-server
  
server:
  port: 8080
---

You are working on {{ issue.identifier }}: {{ issue.title }}

{{ issue.description }}

Please implement this feature according to the specification.
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
