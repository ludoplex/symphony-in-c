/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * config.c - Configuration management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "workflow.h"
#include "util.h"

/* Default values */
#define DEFAULT_LINEAR_ENDPOINT "https://api.linear.app/graphql"
#define DEFAULT_POLL_INTERVAL_MS 30000
#define DEFAULT_HOOK_TIMEOUT_MS 60000
#define DEFAULT_MAX_CONCURRENT_AGENTS 10
#define DEFAULT_MAX_TURNS 20
#define DEFAULT_MAX_RETRY_BACKOFF_MS 300000
#define DEFAULT_CODEX_COMMAND "codex app-server"
#define DEFAULT_TURN_TIMEOUT_MS 3600000
#define DEFAULT_READ_TIMEOUT_MS 5000
#define DEFAULT_STALL_TIMEOUT_MS 300000

static const char *DEFAULT_ACTIVE_STATES[] = {"Todo", "In Progress"};
static const int DEFAULT_ACTIVE_STATE_COUNT = 2;

static const char *DEFAULT_TERMINAL_STATES[] = {"Closed", "Cancelled", "Canceled", "Duplicate", "Done"};
static const int DEFAULT_TERMINAL_STATE_COUNT = 5;

/*
 * Create configuration with defaults
 */
symphony_config_t *config_create(void) {
    symphony_config_t *cfg = calloc(1, sizeof(symphony_config_t));
    if (!cfg) return NULL;
    
    /* Set defaults */
    strcpy(cfg->tracker.kind, "linear");
    strcpy(cfg->tracker.endpoint, DEFAULT_LINEAR_ENDPOINT);
    
    /* Default active states */
    for (int i = 0; i < DEFAULT_ACTIVE_STATE_COUNT; i++) {
        strcpy(cfg->tracker.active_states[i], DEFAULT_ACTIVE_STATES[i]);
    }
    cfg->tracker.active_state_count = DEFAULT_ACTIVE_STATE_COUNT;
    
    /* Default terminal states */
    for (int i = 0; i < DEFAULT_TERMINAL_STATE_COUNT; i++) {
        strcpy(cfg->tracker.terminal_states[i], DEFAULT_TERMINAL_STATES[i]);
    }
    cfg->tracker.terminal_state_count = DEFAULT_TERMINAL_STATE_COUNT;
    
    cfg->polling.interval_ms = DEFAULT_POLL_INTERVAL_MS;
    
    /* Default workspace root */
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir) tmpdir = "/tmp";
    snprintf(cfg->workspace.root, sizeof(cfg->workspace.root),
             "%s/symphony_workspaces", tmpdir);
    
    cfg->hooks.timeout_ms = DEFAULT_HOOK_TIMEOUT_MS;
    
    cfg->agent.max_concurrent_agents = DEFAULT_MAX_CONCURRENT_AGENTS;
    cfg->agent.max_turns = DEFAULT_MAX_TURNS;
    cfg->agent.max_retry_backoff_ms = DEFAULT_MAX_RETRY_BACKOFF_MS;
    
    strcpy(cfg->codex.command, DEFAULT_CODEX_COMMAND);
    cfg->codex.turn_timeout_ms = DEFAULT_TURN_TIMEOUT_MS;
    cfg->codex.read_timeout_ms = DEFAULT_READ_TIMEOUT_MS;
    cfg->codex.stall_timeout_ms = DEFAULT_STALL_TIMEOUT_MS;
    
    cfg->server.port = -1;  /* Disabled by default */
    
    return cfg;
}

/*
 * Destroy configuration
 */
void config_destroy(symphony_config_t *cfg) {
    if (!cfg) return;
    
    free(cfg->hooks.after_create);
    free(cfg->hooks.before_run);
    free(cfg->hooks.after_run);
    free(cfg->hooks.before_remove);
    free(cfg->prompt_template);
    free(cfg);
}

/*
 * Expand environment variables in a string
 * Supports $VAR_NAME syntax
 */
char *config_expand_env(const char *value) {
    if (!value) return NULL;
    
    /* Check for $VAR_NAME pattern */
    if (value[0] == '$') {
        const char *env_val = getenv(value + 1);
        if (env_val && env_val[0]) {
            return util_strdup(env_val);
        }
        return util_strdup("");  /* Empty if not found */
    }
    
    return util_strdup(value);
}

/*
 * Expand path with ~ and environment variables
 */
char *config_expand_path(const char *path) {
    if (!path) return NULL;
    
    /* First expand environment variables */
    char *expanded = config_expand_env(path);
    if (!expanded) return NULL;
    
    /* Then expand ~ */
    char *result = util_path_expand_home(expanded);
    free(expanded);
    
    return result;
}

/*
 * Load configuration from WORKFLOW.md
 */
symphony_error_t config_load(symphony_config_t *cfg, const char *workflow_path) {
    if (!cfg || !workflow_path) return SYMPHONY_ERR_INTERNAL;
    
    /* Store workflow path */
    strncpy(cfg->workflow_path, workflow_path, sizeof(cfg->workflow_path) - 1);
    cfg->workflow_mtime = util_file_mtime(workflow_path);
    
    /* Parse workflow file */
    workflow_def_t *wf = workflow_create();
    if (!wf) return SYMPHONY_ERR_INTERNAL;
    
    symphony_error_t err = workflow_parse_file(wf, workflow_path);
    if (err != SYMPHONY_OK) {
        workflow_destroy(wf);
        return err;
    }
    
    /* Load tracker config */
    char buf[512];
    
    workflow_get_nested_string(wf, "tracker", "kind", buf, sizeof(buf));
    if (buf[0]) strcpy(cfg->tracker.kind, buf);
    
    workflow_get_nested_string(wf, "tracker", "endpoint", buf, sizeof(buf));
    if (buf[0]) strcpy(cfg->tracker.endpoint, buf);
    
    workflow_get_nested_string(wf, "tracker", "api_key", buf, sizeof(buf));
    if (buf[0]) {
        char *expanded = config_expand_env(buf);
        if (expanded) {
            strncpy(cfg->tracker.api_key, expanded, sizeof(cfg->tracker.api_key) - 1);
            free(expanded);
        }
    } else {
        /* Try LINEAR_API_KEY environment variable */
        const char *api_key = getenv("LINEAR_API_KEY");
        if (api_key) {
            strncpy(cfg->tracker.api_key, api_key, sizeof(cfg->tracker.api_key) - 1);
        }
    }
    
    workflow_get_nested_string(wf, "tracker", "project_slug", buf, sizeof(buf));
    if (buf[0]) strncpy(cfg->tracker.project_slug, buf, sizeof(cfg->tracker.project_slug) - 1);
    
    /* Load active and terminal states */
    int count;
    (void)count;  /* Used in parsing loops */
    
    workflow_get_nested_string(wf, "tracker", "active_states", buf, sizeof(buf));
    if (buf[0]) {
        /* Parse as comma-separated or handle through list */
        char *ptr = buf;
        count = 0;
        while (*ptr && count < 16) {
            while (*ptr && (*ptr == ' ' || *ptr == ',' || *ptr == '\t')) ptr++;
            if (!*ptr) break;
            
            char *end = ptr;
            while (*end && *end != ',' && *end != '\n') end++;
            
            char *trim_end = end;
            while (trim_end > ptr && (*(trim_end - 1) == ' ' || *(trim_end - 1) == '\t')) {
                trim_end--;
            }
            
            if (trim_end > ptr) {
                size_t len = (size_t)(trim_end - ptr);
                if (len >= MAX_STATE_LEN) len = MAX_STATE_LEN - 1;
                memcpy(cfg->tracker.active_states[count], ptr, len);
                cfg->tracker.active_states[count][len] = '\0';
                count++;
            }
            
            ptr = end;
        }
        if (count > 0) cfg->tracker.active_state_count = count;
    }
    
    workflow_get_nested_string(wf, "tracker", "terminal_states", buf, sizeof(buf));
    if (buf[0]) {
        char *ptr = buf;
        count = 0;
        while (*ptr && count < 16) {
            while (*ptr && (*ptr == ' ' || *ptr == ',' || *ptr == '\t')) ptr++;
            if (!*ptr) break;
            
            char *end = ptr;
            while (*end && *end != ',' && *end != '\n') end++;
            
            char *trim_end = end;
            while (trim_end > ptr && (*(trim_end - 1) == ' ' || *(trim_end - 1) == '\t')) {
                trim_end--;
            }
            
            if (trim_end > ptr) {
                size_t len = (size_t)(trim_end - ptr);
                if (len >= MAX_STATE_LEN) len = MAX_STATE_LEN - 1;
                memcpy(cfg->tracker.terminal_states[count], ptr, len);
                cfg->tracker.terminal_states[count][len] = '\0';
                count++;
            }
            
            ptr = end;
        }
        if (count > 0) cfg->tracker.terminal_state_count = count;
    }
    
    /* Load polling config */
    int int_val;
    workflow_get_nested_int(wf, "polling", "interval_ms", &int_val);
    if (int_val > 0) cfg->polling.interval_ms = int_val;
    
    /* Load workspace config */
    workflow_get_nested_string(wf, "workspace", "root", buf, sizeof(buf));
    if (buf[0]) {
        char *expanded = config_expand_path(buf);
        if (expanded) {
            strncpy(cfg->workspace.root, expanded, sizeof(cfg->workspace.root) - 1);
            free(expanded);
        }
    }
    
    /* Load hooks config */
    workflow_get_nested_string(wf, "hooks", "after_create", buf, sizeof(buf));
    if (buf[0]) {
        free(cfg->hooks.after_create);
        cfg->hooks.after_create = util_strdup(buf);
    }
    
    workflow_get_nested_string(wf, "hooks", "before_run", buf, sizeof(buf));
    if (buf[0]) {
        free(cfg->hooks.before_run);
        cfg->hooks.before_run = util_strdup(buf);
    }
    
    workflow_get_nested_string(wf, "hooks", "after_run", buf, sizeof(buf));
    if (buf[0]) {
        free(cfg->hooks.after_run);
        cfg->hooks.after_run = util_strdup(buf);
    }
    
    workflow_get_nested_string(wf, "hooks", "before_remove", buf, sizeof(buf));
    if (buf[0]) {
        free(cfg->hooks.before_remove);
        cfg->hooks.before_remove = util_strdup(buf);
    }
    
    workflow_get_nested_int(wf, "hooks", "timeout_ms", &int_val);
    if (int_val > 0) cfg->hooks.timeout_ms = int_val;
    
    /* Load agent config */
    workflow_get_nested_int(wf, "agent", "max_concurrent_agents", &int_val);
    if (int_val > 0) cfg->agent.max_concurrent_agents = int_val;
    
    workflow_get_nested_int(wf, "agent", "max_turns", &int_val);
    if (int_val > 0) cfg->agent.max_turns = int_val;
    
    workflow_get_nested_int(wf, "agent", "max_retry_backoff_ms", &int_val);
    if (int_val > 0) cfg->agent.max_retry_backoff_ms = int_val;
    
    /* Load codex config */
    workflow_get_nested_string(wf, "codex", "command", buf, sizeof(buf));
    if (buf[0]) strncpy(cfg->codex.command, buf, sizeof(cfg->codex.command) - 1);
    
    workflow_get_nested_string(wf, "codex", "approval_policy", buf, sizeof(buf));
    if (buf[0]) strncpy(cfg->codex.approval_policy, buf, sizeof(cfg->codex.approval_policy) - 1);
    
    workflow_get_nested_string(wf, "codex", "thread_sandbox", buf, sizeof(buf));
    if (buf[0]) strncpy(cfg->codex.thread_sandbox, buf, sizeof(cfg->codex.thread_sandbox) - 1);
    
    workflow_get_nested_string(wf, "codex", "turn_sandbox_policy", buf, sizeof(buf));
    if (buf[0]) strncpy(cfg->codex.turn_sandbox_policy, buf, sizeof(cfg->codex.turn_sandbox_policy) - 1);
    
    workflow_get_nested_int(wf, "codex", "turn_timeout_ms", &int_val);
    if (int_val > 0) cfg->codex.turn_timeout_ms = int_val;
    
    workflow_get_nested_int(wf, "codex", "read_timeout_ms", &int_val);
    if (int_val > 0) cfg->codex.read_timeout_ms = int_val;
    
    workflow_get_nested_int(wf, "codex", "stall_timeout_ms", &int_val);
    if (int_val >= 0) cfg->codex.stall_timeout_ms = int_val;
    
    /* Load server config */
    workflow_get_nested_int(wf, "server", "port", &int_val);
    if (int_val >= 0) cfg->server.port = int_val;
    
    /* Load prompt template */
    if (wf->prompt_body && wf->prompt_body[0]) {
        free(cfg->prompt_template);
        cfg->prompt_template = util_strdup(wf->prompt_body);
    } else {
        /* Default minimal prompt */
        free(cfg->prompt_template);
        cfg->prompt_template = util_strdup("You are working on an issue from Linear.");
    }
    
    workflow_destroy(wf);
    return SYMPHONY_OK;
}

/*
 * Reload configuration if file changed
 */
symphony_error_t config_reload(symphony_config_t *cfg) {
    if (!cfg || !cfg->workflow_path[0]) return SYMPHONY_ERR_INTERNAL;
    
    time_t current_mtime = util_file_mtime(cfg->workflow_path);
    if (current_mtime == cfg->workflow_mtime) {
        return SYMPHONY_OK;  /* No change */
    }
    
    LOG_INFO("Workflow file changed, reloading configuration");
    return config_load(cfg, cfg->workflow_path);
}

/*
 * Validate configuration
 */
bool config_validate(symphony_config_t *cfg, char *error_buf, size_t error_buf_size) {
    if (!cfg) {
        snprintf(error_buf, error_buf_size, "Configuration is NULL");
        return false;
    }
    
    /* Check tracker kind */
    if (!cfg->tracker.kind[0]) {
        snprintf(error_buf, error_buf_size, "tracker.kind is required");
        return false;
    }
    
    if (strcmp(cfg->tracker.kind, "linear") != 0) {
        snprintf(error_buf, error_buf_size, "Unsupported tracker kind: %s", cfg->tracker.kind);
        return false;
    }
    
    /* Check API key for Linear */
    if (strcmp(cfg->tracker.kind, "linear") == 0) {
        if (!cfg->tracker.api_key[0]) {
            snprintf(error_buf, error_buf_size, 
                     "tracker.api_key is required (set LINEAR_API_KEY environment variable)");
            return false;
        }
        
        if (!cfg->tracker.project_slug[0]) {
            snprintf(error_buf, error_buf_size, "tracker.project_slug is required for Linear");
            return false;
        }
    }
    
    /* Check codex command */
    if (!cfg->codex.command[0]) {
        snprintf(error_buf, error_buf_size, "codex.command is required");
        return false;
    }
    
    return true;
}

/*
 * Getters with defaults
 */
int config_get_poll_interval(symphony_config_t *cfg) {
    return cfg ? cfg->polling.interval_ms : DEFAULT_POLL_INTERVAL_MS;
}

int config_get_max_concurrent_agents(symphony_config_t *cfg) {
    return cfg ? cfg->agent.max_concurrent_agents : DEFAULT_MAX_CONCURRENT_AGENTS;
}

int config_get_max_concurrent_for_state(symphony_config_t *cfg, const char *state) {
    if (!cfg || !state) return DEFAULT_MAX_CONCURRENT_AGENTS;
    
    /* Normalize state for lookup */
    char normalized[MAX_STATE_LEN];
    strncpy(normalized, state, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    util_str_tolower(normalized);
    util_str_trim(normalized);
    
    /* Check per-state limits */
    for (int i = 0; i < cfg->agent.state_limits_count; i++) {
        char key_norm[MAX_STATE_LEN];
        strncpy(key_norm, cfg->agent.state_limits_keys[i], sizeof(key_norm) - 1);
        key_norm[sizeof(key_norm) - 1] = '\0';
        util_str_tolower(key_norm);
        util_str_trim(key_norm);
        
        if (strcmp(key_norm, normalized) == 0) {
            return cfg->agent.state_limits_values[i];
        }
    }
    
    /* Fall back to global limit */
    return cfg->agent.max_concurrent_agents;
}

const char *config_get_workspace_root(symphony_config_t *cfg) {
    if (!cfg || !cfg->workspace.root[0]) {
        return "/tmp/symphony_workspaces";
    }
    return cfg->workspace.root;
}

const char *config_get_codex_command(symphony_config_t *cfg) {
    if (!cfg || !cfg->codex.command[0]) {
        return DEFAULT_CODEX_COMMAND;
    }
    return cfg->codex.command;
}

/*
 * State checking
 */
bool config_is_active_state(symphony_config_t *cfg, const char *state) {
    if (!cfg || !state) return false;
    
    char normalized[MAX_STATE_LEN];
    strncpy(normalized, state, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    util_str_tolower(normalized);
    util_str_trim(normalized);
    
    for (int i = 0; i < cfg->tracker.active_state_count; i++) {
        char active_norm[MAX_STATE_LEN];
        strncpy(active_norm, cfg->tracker.active_states[i], sizeof(active_norm) - 1);
        active_norm[sizeof(active_norm) - 1] = '\0';
        util_str_tolower(active_norm);
        util_str_trim(active_norm);
        
        if (strcmp(active_norm, normalized) == 0) {
            return true;
        }
    }
    
    return false;
}

bool config_is_terminal_state(symphony_config_t *cfg, const char *state) {
    if (!cfg || !state) return false;
    
    char normalized[MAX_STATE_LEN];
    strncpy(normalized, state, sizeof(normalized) - 1);
    normalized[sizeof(normalized) - 1] = '\0';
    util_str_tolower(normalized);
    util_str_trim(normalized);
    
    for (int i = 0; i < cfg->tracker.terminal_state_count; i++) {
        char terminal_norm[MAX_STATE_LEN];
        strncpy(terminal_norm, cfg->tracker.terminal_states[i], sizeof(terminal_norm) - 1);
        terminal_norm[sizeof(terminal_norm) - 1] = '\0';
        util_str_tolower(terminal_norm);
        util_str_trim(terminal_norm);
        
        if (strcmp(terminal_norm, normalized) == 0) {
            return true;
        }
    }
    
    return false;
}
