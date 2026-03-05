/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * symphony.h - Main header file
 */

#ifndef SYMPHONY_H
#define SYMPHONY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* Version info */
#define SYMPHONY_VERSION "1.0.0"
#define SYMPHONY_NAME "symphony"

/* Maximum sizes */
#define MAX_PATH_LEN 4096
#define MAX_IDENTIFIER_LEN 256
#define MAX_TITLE_LEN 1024
#define MAX_URL_LEN 2048
#define MAX_STATE_LEN 128
#define MAX_LABELS 32
#define MAX_BLOCKERS 16
#define MAX_HOOK_SCRIPT_LEN 8192
#define MAX_PROMPT_LEN 65536
#define MAX_LINE_LEN 10485760  /* 10 MB for JSON lines */

/* Forward declarations */
typedef struct symphony_config symphony_config_t;
typedef struct symphony_issue symphony_issue_t;
typedef struct symphony_workspace symphony_workspace_t;
typedef struct symphony_session symphony_session_t;
typedef struct symphony_orchestrator symphony_orchestrator_t;

/*
 * Issue structure - Normalized issue record
 */
typedef struct {
    char id[MAX_IDENTIFIER_LEN];
    char identifier[MAX_IDENTIFIER_LEN];
} blocker_ref_t;

struct symphony_issue {
    char id[MAX_IDENTIFIER_LEN];
    char identifier[MAX_IDENTIFIER_LEN];
    char title[MAX_TITLE_LEN];
    char *description;  /* Dynamically allocated */
    int priority;       /* 1-4, 0 for null */
    char state[MAX_STATE_LEN];
    char branch_name[MAX_IDENTIFIER_LEN];
    char url[MAX_URL_LEN];
    char labels[MAX_LABELS][MAX_STATE_LEN];
    int label_count;
    blocker_ref_t blocked_by[MAX_BLOCKERS];
    int blocker_count;
    time_t created_at;
    time_t updated_at;
};

/*
 * Workspace structure
 */
struct symphony_workspace {
    char path[MAX_PATH_LEN];
    char workspace_key[MAX_IDENTIFIER_LEN];
    bool created_now;
};

/*
 * Session structure - Live coding agent session
 */
struct symphony_session {
    char session_id[MAX_IDENTIFIER_LEN];
    char thread_id[MAX_IDENTIFIER_LEN];
    char turn_id[MAX_IDENTIFIER_LEN];
    pid_t codex_pid;
    char last_event[64];
    time_t last_timestamp;
    uint64_t input_tokens;
    uint64_t output_tokens;
    uint64_t total_tokens;
    int turn_count;
};

/*
 * Run attempt status
 */
typedef enum {
    RUN_STATUS_PREPARING_WORKSPACE,
    RUN_STATUS_BUILDING_PROMPT,
    RUN_STATUS_LAUNCHING_AGENT,
    RUN_STATUS_INITIALIZING_SESSION,
    RUN_STATUS_STREAMING_TURN,
    RUN_STATUS_FINISHING,
    RUN_STATUS_SUCCEEDED,
    RUN_STATUS_FAILED,
    RUN_STATUS_TIMED_OUT,
    RUN_STATUS_STALLED,
    RUN_STATUS_CANCELED
} run_status_t;

/*
 * Running entry - Tracks a running issue
 */
typedef struct {
    char issue_id[MAX_IDENTIFIER_LEN];
    char identifier[MAX_IDENTIFIER_LEN];
    int attempt;
    char workspace_path[MAX_PATH_LEN];
    time_t started_at;
    run_status_t status;
    symphony_session_t session;
} running_entry_t;

/*
 * Retry entry
 */
typedef struct {
    char issue_id[MAX_IDENTIFIER_LEN];
    char identifier[MAX_IDENTIFIER_LEN];
    int attempt;
    int64_t due_at_ms;
    char error[256];
} retry_entry_t;

/*
 * Orchestrator state
 */
struct symphony_orchestrator {
    symphony_config_t *config;
    int poll_interval_ms;
    int max_concurrent_agents;
    
    /* Running issues map (implemented as array for simplicity) */
    running_entry_t *running;
    int running_count;
    int running_capacity;
    
    /* Retry queue */
    retry_entry_t *retry_queue;
    int retry_count;
    int retry_capacity;
    
    /* Token totals */
    uint64_t total_input_tokens;
    uint64_t total_output_tokens;
    uint64_t total_tokens;
    double total_seconds_running;
    
    /* State flags */
    bool should_stop;
    time_t last_poll;
};

/* 
 * Error codes 
 */
typedef enum {
    SYMPHONY_OK = 0,
    SYMPHONY_ERR_MISSING_WORKFLOW_FILE,
    SYMPHONY_ERR_WORKFLOW_PARSE_ERROR,
    SYMPHONY_ERR_WORKFLOW_NOT_A_MAP,
    SYMPHONY_ERR_TEMPLATE_PARSE_ERROR,
    SYMPHONY_ERR_TEMPLATE_RENDER_ERROR,
    SYMPHONY_ERR_MISSING_API_KEY,
    SYMPHONY_ERR_MISSING_PROJECT_SLUG,
    SYMPHONY_ERR_UNSUPPORTED_TRACKER,
    SYMPHONY_ERR_NETWORK_ERROR,
    SYMPHONY_ERR_WORKSPACE_ERROR,
    SYMPHONY_ERR_AGENT_ERROR,
    SYMPHONY_ERR_INTERNAL
} symphony_error_t;

/* Error handling */
const char *symphony_error_string(symphony_error_t err);

#endif /* SYMPHONY_H */
