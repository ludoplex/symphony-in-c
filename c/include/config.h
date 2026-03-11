/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * config.h - Configuration management
 */

#ifndef SYMPHONY_CONFIG_H
#define SYMPHONY_CONFIG_H

#include "symphony.h"

/* 
 * Tracker configuration 
 */
typedef struct {
    char kind[32];                      /* "linear" */
    char endpoint[MAX_URL_LEN];         /* GraphQL endpoint */
    char api_key[512];                  /* Resolved API key */
    char project_slug[256];             /* Project identifier */
    char active_states[16][MAX_STATE_LEN];
    int active_state_count;
    char terminal_states[16][MAX_STATE_LEN];
    int terminal_state_count;
} tracker_config_t;

/*
 * Polling configuration
 */
typedef struct {
    int interval_ms;                    /* Default: 30000 */
} polling_config_t;

/*
 * Workspace configuration
 */
typedef struct {
    char root[MAX_PATH_LEN];            /* Workspace root path */
} workspace_config_t;

/*
 * Hooks configuration
 */
typedef struct {
    char *after_create;                 /* Shell script, dynamically allocated */
    char *before_run;
    char *after_run;
    char *before_remove;
    int timeout_ms;                     /* Default: 60000 */
} hooks_config_t;

/*
 * Agent configuration
 */
typedef struct {
    int max_concurrent_agents;          /* Default: 10 */
    int max_turns;                      /* Default: 20 */
    int max_retry_backoff_ms;           /* Default: 300000 (5m) */
    /* Per-state concurrency map - simplified as parallel arrays */
    char state_limits_keys[16][MAX_STATE_LEN];
    int state_limits_values[16];
    int state_limits_count;
} agent_config_t;

/*
 * Codex configuration
 */
typedef struct {
    char command[MAX_PATH_LEN];         /* Default: "codex app-server" */
    char approval_policy[64];
    char thread_sandbox[64];
    char turn_sandbox_policy[64];
    int turn_timeout_ms;                /* Default: 3600000 (1h) */
    int read_timeout_ms;                /* Default: 5000 */
    int stall_timeout_ms;               /* Default: 300000 (5m) */
} codex_config_t;

/*
 * Server configuration (optional extension)
 */
typedef struct {
    int port;                           /* 0 = disabled, >0 = enabled */
} server_config_t;

/*
 * Full configuration structure
 */
struct symphony_config {
    tracker_config_t tracker;
    polling_config_t polling;
    workspace_config_t workspace;
    hooks_config_t hooks;
    agent_config_t agent;
    codex_config_t codex;
    server_config_t server;
    
    /* Prompt template */
    char *prompt_template;              /* Dynamically allocated */
    
    /* Source file info */
    char workflow_path[MAX_PATH_LEN];
    time_t workflow_mtime;              /* For change detection */
};

/* Configuration functions */
symphony_config_t *config_create(void);
void config_destroy(symphony_config_t *cfg);
symphony_error_t config_load(symphony_config_t *cfg, const char *workflow_path);
symphony_error_t config_reload(symphony_config_t *cfg);
bool config_validate(symphony_config_t *cfg, char *error_buf, size_t error_buf_size);

/* Environment variable expansion */
char *config_expand_env(const char *value);
char *config_expand_path(const char *path);

/* Getters with defaults */
int config_get_poll_interval(symphony_config_t *cfg);
int config_get_max_concurrent_agents(symphony_config_t *cfg);
int config_get_max_concurrent_for_state(symphony_config_t *cfg, const char *state);
const char *config_get_workspace_root(symphony_config_t *cfg);
const char *config_get_codex_command(symphony_config_t *cfg);

/* State checking */
bool config_is_active_state(symphony_config_t *cfg, const char *state);
bool config_is_terminal_state(symphony_config_t *cfg, const char *state);

#endif /* SYMPHONY_CONFIG_H */
