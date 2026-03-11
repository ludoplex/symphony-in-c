/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * orchestrator.c - Main orchestration loop
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "orchestrator.h"
#include "workspace.h"
#include "agent.h"
#include "template.h"
#include "util.h"

/* Continuation retry delay */
#define CONTINUATION_RETRY_MS 1000

/*
 * Create orchestrator
 */
symphony_orchestrator_t *orchestrator_create(symphony_config_t *cfg) {
    if (!cfg) return NULL;
    
    symphony_orchestrator_t *orch = calloc(1, sizeof(symphony_orchestrator_t));
    if (!orch) return NULL;
    
    orch->config = cfg;
    orch->poll_interval_ms = cfg->polling.interval_ms;
    orch->max_concurrent_agents = cfg->agent.max_concurrent_agents;
    
    /* Initialize running array */
    orch->running_capacity = 32;
    orch->running = calloc((size_t)orch->running_capacity, sizeof(running_entry_t));
    
    /* Initialize retry queue */
    orch->retry_capacity = 32;
    orch->retry_queue = calloc((size_t)orch->retry_capacity, sizeof(retry_entry_t));
    
    if (!orch->running || !orch->retry_queue) {
        free(orch->running);
        free(orch->retry_queue);
        free(orch);
        return NULL;
    }
    
    return orch;
}

/*
 * Destroy orchestrator
 */
void orchestrator_destroy(symphony_orchestrator_t *orch) {
    if (!orch) return;
    free(orch->running);
    free(orch->retry_queue);
    free(orch);
}

/*
 * Check available slots
 */
int orchestrator_available_slots(symphony_orchestrator_t *orch) {
    if (!orch) return 0;
    int available = orch->max_concurrent_agents - orch->running_count;
    return available > 0 ? available : 0;
}

int orchestrator_running_count(symphony_orchestrator_t *orch) {
    return orch ? orch->running_count : 0;
}

/*
 * Check if issue is running
 */
bool orchestrator_is_running(symphony_orchestrator_t *orch, const char *issue_id) {
    if (!orch || !issue_id) return false;
    
    for (int i = 0; i < orch->running_count; i++) {
        if (strcmp(orch->running[i].issue_id, issue_id) == 0) {
            return true;
        }
    }
    return false;
}

/*
 * Check if issue is claimed (running or retrying)
 */
bool orchestrator_is_claimed(symphony_orchestrator_t *orch, const char *issue_id) {
    if (!orch || !issue_id) return false;
    
    /* Check running */
    if (orchestrator_is_running(orch, issue_id)) return true;
    
    /* Check retry queue */
    for (int i = 0; i < orch->retry_count; i++) {
        if (strcmp(orch->retry_queue[i].issue_id, issue_id) == 0) {
            return true;
        }
    }
    
    return false;
}

/*
 * Count running issues in a specific state
 */
static int count_running_in_state(symphony_orchestrator_t *orch, const char *state) {
    if (!orch || !state) return 0;
    
    int count = 0;
    /* Note: In a full implementation, we'd track state in running entries */
    (void)state;  /* Not implemented yet */
    return count;
}

/*
 * Check if an issue can be dispatched
 */
bool orchestrator_can_dispatch(symphony_orchestrator_t *orch, symphony_issue_t *issue) {
    if (!orch || !issue) return false;
    
    /* Basic required fields */
    if (!issue->id[0] || !issue->identifier[0] || !issue->title[0] || !issue->state[0]) {
        return false;
    }
    
    /* Check active state */
    if (!config_is_active_state(orch->config, issue->state)) {
        return false;
    }
    
    /* Check terminal state */
    if (config_is_terminal_state(orch->config, issue->state)) {
        return false;
    }
    
    /* Check if already claimed */
    if (orchestrator_is_claimed(orch, issue->id)) {
        return false;
    }
    
    /* Check global slots */
    if (orchestrator_available_slots(orch) <= 0) {
        return false;
    }
    
    /* Check per-state limit */
    int state_limit = config_get_max_concurrent_for_state(orch->config, issue->state);
    int state_count = count_running_in_state(orch, issue->state);
    if (state_count >= state_limit) {
        return false;
    }
    
    /* Check Todo blocker rule */
    if (util_str_eq_nocase(issue->state, "todo")) {
        if (issue_has_nonterminal_blockers(issue, orch->config)) {
            return false;
        }
    }
    
    return true;
}

/*
 * Find running entry index
 */
static int find_running_index(symphony_orchestrator_t *orch, const char *issue_id) {
    for (int i = 0; i < orch->running_count; i++) {
        if (strcmp(orch->running[i].issue_id, issue_id) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Find retry entry index
 */
static int find_retry_index(symphony_orchestrator_t *orch, const char *issue_id) {
    for (int i = 0; i < orch->retry_count; i++) {
        if (strcmp(orch->retry_queue[i].issue_id, issue_id) == 0) {
            return i;
        }
    }
    return -1;
}

/*
 * Remove running entry
 */
static void remove_running(symphony_orchestrator_t *orch, int index) {
    if (index < 0 || index >= orch->running_count) return;
    
    /* Shift remaining entries */
    for (int i = index; i < orch->running_count - 1; i++) {
        orch->running[i] = orch->running[i + 1];
    }
    orch->running_count--;
}

/*
 * Remove retry entry
 */
static void remove_retry(symphony_orchestrator_t *orch, int index) {
    if (index < 0 || index >= orch->retry_count) return;
    
    for (int i = index; i < orch->retry_count - 1; i++) {
        orch->retry_queue[i] = orch->retry_queue[i + 1];
    }
    orch->retry_count--;
}

/*
 * Worker thread function (simplified - runs synchronously for now)
 */
static void run_worker(symphony_orchestrator_t *orch, symphony_issue_t *issue, int attempt) {
    LOG_INFO_ISSUE(issue->id, "Starting worker for %s (attempt %d)", 
                   issue->identifier, attempt);
    
    symphony_config_t *cfg = orch->config;
    symphony_workspace_t *ws = workspace_create();
    if (!ws) {
        LOG_ERROR_ISSUE(issue->id, "Failed to create workspace structure");
        orchestrator_on_worker_exit(orch, issue->id, false, "workspace allocation failed");
        return;
    }
    
    /* Create/reuse workspace */
    symphony_error_t err = workspace_ensure(ws, cfg, issue->identifier);
    if (err != SYMPHONY_OK) {
        LOG_ERROR_ISSUE(issue->id, "Failed to ensure workspace: %s", symphony_error_string(err));
        workspace_destroy(ws);
        orchestrator_on_worker_exit(orch, issue->id, false, "workspace error");
        return;
    }
    
    /* Run after_create hook if new */
    if (ws->created_now) {
        err = workspace_after_create(cfg, ws);
        if (err != SYMPHONY_OK) {
            workspace_destroy(ws);
            orchestrator_on_worker_exit(orch, issue->id, false, "after_create hook failed");
            return;
        }
    }
    
    /* Run before_run hook */
    err = workspace_before_run(cfg, ws);
    if (err != SYMPHONY_OK) {
        workspace_after_run(cfg, ws);
        workspace_destroy(ws);
        orchestrator_on_worker_exit(orch, issue->id, false, "before_run hook failed");
        return;
    }
    
    /* Build prompt */
    template_engine_t *eng = template_create();
    if (!eng) {
        workspace_after_run(cfg, ws);
        workspace_destroy(ws);
        orchestrator_on_worker_exit(orch, issue->id, false, "template engine creation failed");
        return;
    }
    
    char *prompt = NULL;
    err = template_render(eng, cfg->prompt_template, issue, attempt, &prompt);
    template_destroy(eng);
    
    if (err != SYMPHONY_OK || !prompt) {
        workspace_after_run(cfg, ws);
        workspace_destroy(ws);
        orchestrator_on_worker_exit(orch, issue->id, false, "prompt rendering failed");
        return;
    }
    
    /* Start agent session */
    agent_runner_t *agent = agent_create(cfg);
    if (!agent) {
        free(prompt);
        workspace_after_run(cfg, ws);
        workspace_destroy(ws);
        orchestrator_on_worker_exit(orch, issue->id, false, "agent creation failed");
        return;
    }
    
    symphony_session_t session = {0};
    err = agent_start_session(agent, ws->path, &session);
    if (err != SYMPHONY_OK) {
        agent_destroy(agent);
        free(prompt);
        workspace_after_run(cfg, ws);
        workspace_destroy(ws);
        orchestrator_on_worker_exit(orch, issue->id, false, "agent session start failed");
        return;
    }
    
    /* Update running entry with session info */
    int idx = find_running_index(orch, issue->id);
    if (idx >= 0) {
        orch->running[idx].session = session;
        orch->running[idx].status = RUN_STATUS_STREAMING_TURN;
    }
    
    /* Build title */
    char title[1536];
    snprintf(title, sizeof(title), "%s: %s", issue->identifier, issue->title);
    
    /* Run turn loop */
    int max_turns = cfg->agent.max_turns;
    int turn_count = 0;
    bool success = true;
    
    while (turn_count < max_turns && !orch->should_stop) {
        /* Run turn */
        err = agent_run_turn(agent, &session, prompt, title, NULL, NULL);
        turn_count++;
        
        if (err != SYMPHONY_OK) {
            success = false;
            break;
        }
        
        /* Check if we should continue - would fetch issue state here */
        /* For now, just do one turn */
        break;
    }
    
    /* Cleanup */
    agent_stop_session(agent, &session);
    agent_destroy(agent);
    free(prompt);
    workspace_after_run(cfg, ws);
    workspace_destroy(ws);
    
    orchestrator_on_worker_exit(orch, issue->id, success, success ? NULL : "turn failed");
}

/*
 * Dispatch an issue
 */
symphony_error_t orchestrator_dispatch(symphony_orchestrator_t *orch, 
                                        symphony_issue_t *issue, 
                                        int attempt) {
    if (!orch || !issue) return SYMPHONY_ERR_INTERNAL;
    
    /* Grow running array if needed */
    if (orch->running_count >= orch->running_capacity) {
        int new_capacity = orch->running_capacity * 2;
        running_entry_t *new_running = realloc(orch->running,
            (size_t)new_capacity * sizeof(running_entry_t));
        if (!new_running) return SYMPHONY_ERR_INTERNAL;
        orch->running = new_running;
        orch->running_capacity = new_capacity;
    }
    
    /* Create running entry */
    running_entry_t *entry = &orch->running[orch->running_count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->issue_id, issue->id, sizeof(entry->issue_id) - 1);
    strncpy(entry->identifier, issue->identifier, sizeof(entry->identifier) - 1);
    entry->attempt = attempt;
    entry->started_at = time(NULL);
    entry->status = RUN_STATUS_PREPARING_WORKSPACE;
    
    char *ws_path = workspace_get_path(orch->config, issue->identifier);
    if (ws_path) {
        strncpy(entry->workspace_path, ws_path, sizeof(entry->workspace_path) - 1);
        free(ws_path);
    }
    
    orch->running_count++;
    
    LOG_INFO_ISSUE(issue->id, "Dispatching %s", issue->identifier);
    
    /* Run worker (synchronously for now - a real impl would use threads) */
    run_worker(orch, issue, attempt);
    
    return SYMPHONY_OK;
}

/*
 * Schedule a retry
 */
void orchestrator_schedule_retry(symphony_orchestrator_t *orch,
                                  const char *issue_id,
                                  const char *identifier,
                                  int attempt,
                                  const char *error) {
    if (!orch || !issue_id || !identifier) return;
    
    /* Cancel existing retry for this issue */
    int existing = find_retry_index(orch, issue_id);
    if (existing >= 0) {
        remove_retry(orch, existing);
    }
    
    /* Grow retry queue if needed */
    if (orch->retry_count >= orch->retry_capacity) {
        int new_capacity = orch->retry_capacity * 2;
        retry_entry_t *new_queue = realloc(orch->retry_queue,
            (size_t)new_capacity * sizeof(retry_entry_t));
        if (!new_queue) return;
        orch->retry_queue = new_queue;
        orch->retry_capacity = new_capacity;
    }
    
    /* Calculate backoff */
    int64_t delay_ms;
    if (attempt == 1 && !error) {
        /* Continuation retry - short delay */
        delay_ms = CONTINUATION_RETRY_MS;
    } else {
        /* Exponential backoff: 10s * 2^(attempt-1), capped */
        delay_ms = 10000;
        for (int i = 1; i < attempt && delay_ms < orch->config->agent.max_retry_backoff_ms; i++) {
            delay_ms *= 2;
        }
        if (delay_ms > orch->config->agent.max_retry_backoff_ms) {
            delay_ms = orch->config->agent.max_retry_backoff_ms;
        }
    }
    
    /* Create retry entry */
    retry_entry_t *entry = &orch->retry_queue[orch->retry_count];
    memset(entry, 0, sizeof(*entry));
    strncpy(entry->issue_id, issue_id, sizeof(entry->issue_id) - 1);
    strncpy(entry->identifier, identifier, sizeof(entry->identifier) - 1);
    entry->attempt = attempt;
    entry->due_at_ms = util_monotonic_ms() + delay_ms;
    if (error) {
        strncpy(entry->error, error, sizeof(entry->error) - 1);
    }
    
    orch->retry_count++;
    
    LOG_INFO_ISSUE(issue_id, "Scheduled retry %d for %s in %ldms%s%s",
                   attempt, identifier, (long)delay_ms,
                   error ? ": " : "", error ? error : "");
}

/*
 * Process due retries
 */
void orchestrator_process_retries(symphony_orchestrator_t *orch) {
    if (!orch) return;
    
    int64_t now_ms = util_monotonic_ms();
    tracker_client_t *tracker = tracker_create(orch->config);
    
    /* Process each due retry */
    int i = 0;
    while (i < orch->retry_count) {
        retry_entry_t *entry = &orch->retry_queue[i];
        
        if (entry->due_at_ms > now_ms) {
            i++;
            continue;
        }
        
        LOG_INFO_ISSUE(entry->issue_id, "Processing retry %d for %s",
                       entry->attempt, entry->identifier);
        
        /* Fetch candidates to check if issue is still eligible */
        symphony_issue_t *issues = NULL;
        int count = 0;
        
        symphony_error_t err = tracker_fetch_candidates(tracker, &issues, &count);
        if (err != SYMPHONY_OK) {
            /* Requeue with incremented attempt */
            entry->attempt++;
            entry->due_at_ms = now_ms + 10000;  /* 10s retry */
            strncpy(entry->error, "retry poll failed", sizeof(entry->error) - 1);
            i++;
            continue;
        }
        
        /* Find the issue */
        symphony_issue_t *found = NULL;
        for (int j = 0; j < count; j++) {
            if (strcmp(issues[j].id, entry->issue_id) == 0) {
                found = &issues[j];
                break;
            }
        }
        
        if (!found) {
            /* Issue no longer eligible, release */
            LOG_INFO_ISSUE(entry->issue_id, "Issue no longer eligible, releasing claim");
            remove_retry(orch, i);
            issue_list_destroy(issues, count);
            continue;
        }
        
        /* Check slots */
        if (orchestrator_available_slots(orch) <= 0) {
            entry->attempt++;
            entry->due_at_ms = now_ms + 5000;
            strncpy(entry->error, "no available orchestrator slots", sizeof(entry->error) - 1);
            i++;
            issue_list_destroy(issues, count);
            continue;
        }
        
        /* Dispatch */
        int attempt = entry->attempt;
        char issue_id[MAX_IDENTIFIER_LEN];
        strncpy(issue_id, entry->issue_id, sizeof(issue_id) - 1);
        
        remove_retry(orch, i);
        
        /* Clone issue for dispatch */
        symphony_issue_t *issue_copy = issue_clone(found);
        issue_list_destroy(issues, count);
        
        if (issue_copy) {
            orchestrator_dispatch(orch, issue_copy, attempt);
            issue_destroy(issue_copy);
        }
    }
    
    tracker_destroy(tracker);
}

/*
 * Handle worker exit
 */
void orchestrator_on_worker_exit(symphony_orchestrator_t *orch,
                                  const char *issue_id,
                                  bool success,
                                  const char *error) {
    if (!orch || !issue_id) return;
    
    int idx = find_running_index(orch, issue_id);
    if (idx < 0) return;
    
    running_entry_t *entry = &orch->running[idx];
    
    /* Update runtime totals */
    time_t duration = time(NULL) - entry->started_at;
    orch->total_seconds_running += (double)duration;
    
    /* Copy needed fields before removing */
    char identifier[MAX_IDENTIFIER_LEN];
    strncpy(identifier, entry->identifier, sizeof(identifier) - 1);
    identifier[sizeof(identifier) - 1] = '\0';
    int attempt = entry->attempt;
    
    /* Remove from running */
    remove_running(orch, idx);
    
    if (success) {
        LOG_INFO_ISSUE(issue_id, "Worker completed successfully for %s", identifier);
        /* Schedule continuation retry */
        orchestrator_schedule_retry(orch, issue_id, identifier, 1, NULL);
    } else {
        LOG_WARN_ISSUE(issue_id, "Worker failed for %s: %s", identifier, error ? error : "unknown");
        /* Schedule exponential backoff retry */
        orchestrator_schedule_retry(orch, issue_id, identifier, attempt + 1, error);
    }
}

/*
 * Handle codex event
 */
void orchestrator_on_codex_event(symphony_orchestrator_t *orch,
                                  const char *issue_id,
                                  const char *event_type,
                                  uint64_t input_tokens,
                                  uint64_t output_tokens) {
    if (!orch || !issue_id) return;
    
    int idx = find_running_index(orch, issue_id);
    if (idx < 0) return;
    
    running_entry_t *entry = &orch->running[idx];
    
    /* Update session info */
    if (event_type) {
        strncpy(entry->session.last_event, event_type, sizeof(entry->session.last_event) - 1);
    }
    entry->session.last_timestamp = time(NULL);
    
    /* Update token counts (delta) */
    uint64_t input_delta = input_tokens > entry->session.input_tokens ? 
        input_tokens - entry->session.input_tokens : 0;
    uint64_t output_delta = output_tokens > entry->session.output_tokens ?
        output_tokens - entry->session.output_tokens : 0;
    
    entry->session.input_tokens = input_tokens;
    entry->session.output_tokens = output_tokens;
    entry->session.total_tokens = input_tokens + output_tokens;
    
    /* Update orchestrator totals */
    orch->total_input_tokens += input_delta;
    orch->total_output_tokens += output_delta;
    orch->total_tokens = orch->total_input_tokens + orch->total_output_tokens;
}

/*
 * Reconcile running issues
 */
symphony_error_t orchestrator_reconcile(symphony_orchestrator_t *orch) {
    if (!orch || orch->running_count == 0) return SYMPHONY_OK;
    
    symphony_config_t *cfg = orch->config;
    
    /* Stall detection */
    int64_t now_ms = util_monotonic_ms();
    int stall_timeout_ms = cfg->codex.stall_timeout_ms;
    
    if (stall_timeout_ms > 0) {
        for (int i = 0; i < orch->running_count; i++) {
            running_entry_t *entry = &orch->running[i];
            
            time_t last_activity = entry->session.last_timestamp > 0 ?
                entry->session.last_timestamp : entry->started_at;
            int64_t elapsed_ms = (now_ms / 1000 - last_activity) * 1000;
            
            if (elapsed_ms > stall_timeout_ms) {
                LOG_WARN_ISSUE(entry->issue_id, "Stall detected for %s (no activity for %ldms)",
                               entry->identifier, (long)elapsed_ms);
                
                /* Would kill worker here in a threaded implementation */
                /* For now, just mark for retry */
                orchestrator_on_worker_exit(orch, entry->issue_id, false, "stalled");
                i--;  /* Array was modified */
            }
        }
    }
    
    /* State refresh */
    if (orch->running_count == 0) return SYMPHONY_OK;
    
    /* Build list of running issue IDs */
    const char **ids = malloc((size_t)orch->running_count * sizeof(char *));
    if (!ids) return SYMPHONY_ERR_INTERNAL;
    
    for (int i = 0; i < orch->running_count; i++) {
        ids[i] = orch->running[i].issue_id;
    }
    
    /* Fetch current states */
    tracker_client_t *tracker = tracker_create(cfg);
    if (!tracker) {
        free(ids);
        return SYMPHONY_ERR_INTERNAL;
    }
    
    symphony_issue_t *issues = NULL;
    int count = 0;
    symphony_error_t err = tracker_fetch_states_by_ids(tracker, ids, orch->running_count, 
                                                        &issues, &count);
    tracker_destroy(tracker);
    free(ids);
    
    if (err != SYMPHONY_OK) {
        LOG_WARN("Failed to fetch issue states for reconciliation");
        return SYMPHONY_OK;  /* Keep running, try again next tick */
    }
    
    /* Check each running issue */
    for (int i = 0; i < orch->running_count; i++) {
        running_entry_t *entry = &orch->running[i];
        
        /* Find in fetched issues */
        symphony_issue_t *found = NULL;
        for (int j = 0; j < count; j++) {
            if (strcmp(issues[j].id, entry->issue_id) == 0) {
                found = &issues[j];
                break;
            }
        }
        
        if (!found) {
            LOG_WARN_ISSUE(entry->issue_id, "Issue not found in tracker, stopping");
            orchestrator_on_worker_exit(orch, entry->issue_id, false, "issue not found");
            i--;
            continue;
        }
        
        if (config_is_terminal_state(cfg, found->state)) {
            LOG_INFO_ISSUE(entry->issue_id, "Issue in terminal state '%s', cleaning up",
                           found->state);
            
            /* Clean workspace */
            workspace_remove(cfg, entry->identifier);
            
            /* Remove from running (don't retry) */
            remove_running(orch, i);
            i--;
            continue;
        }
        
        if (!config_is_active_state(cfg, found->state)) {
            LOG_INFO_ISSUE(entry->issue_id, "Issue no longer in active state '%s', stopping",
                           found->state);
            orchestrator_on_worker_exit(orch, entry->issue_id, false, "state changed");
            i--;
            continue;
        }
    }
    
    issue_list_destroy(issues, count);
    return SYMPHONY_OK;
}

/*
 * Single orchestrator tick
 */
symphony_error_t orchestrator_tick(symphony_orchestrator_t *orch) {
    if (!orch || orch->should_stop) return SYMPHONY_OK;
    
    symphony_config_t *cfg = orch->config;
    
    /* Check for config reload */
    config_reload(cfg);
    
    /* Update dynamic settings */
    orch->poll_interval_ms = cfg->polling.interval_ms;
    orch->max_concurrent_agents = cfg->agent.max_concurrent_agents;
    
    /* Reconcile running issues */
    orchestrator_reconcile(orch);
    
    /* Process retries */
    orchestrator_process_retries(orch);
    
    /* Validate config for dispatch */
    char error_buf[256];
    if (!config_validate(cfg, error_buf, sizeof(error_buf))) {
        LOG_WARN("Config validation failed, skipping dispatch: %s", error_buf);
        return SYMPHONY_OK;
    }
    
    /* Fetch candidate issues */
    tracker_client_t *tracker = tracker_create(cfg);
    if (!tracker) {
        LOG_WARN("Failed to create tracker client");
        return SYMPHONY_OK;
    }
    
    symphony_issue_t *candidates = NULL;
    int count = 0;
    symphony_error_t err = tracker_fetch_candidates(tracker, &candidates, &count);
    tracker_destroy(tracker);
    
    if (err != SYMPHONY_OK) {
        LOG_WARN("Failed to fetch candidates: %s", symphony_error_string(err));
        return SYMPHONY_OK;
    }
    
    LOG_INFO("Fetched %d candidate issues", count);
    
    /* Dispatch eligible issues */
    for (int i = 0; i < count && orchestrator_available_slots(orch) > 0; i++) {
        symphony_issue_t *issue = &candidates[i];
        
        if (orchestrator_can_dispatch(orch, issue)) {
            err = orchestrator_dispatch(orch, issue, 0);
            if (err != SYMPHONY_OK) {
                LOG_WARN_ISSUE(issue->id, "Failed to dispatch: %s", symphony_error_string(err));
            }
        }
    }
    
    issue_list_destroy(candidates, count);
    orch->last_poll = time(NULL);
    
    return SYMPHONY_OK;
}

/*
 * Start orchestrator main loop
 */
symphony_error_t orchestrator_start(symphony_orchestrator_t *orch) {
    if (!orch) return SYMPHONY_ERR_INTERNAL;
    
    LOG_INFO("Orchestrator starting");
    
    /* Startup terminal cleanup */
    tracker_client_t *tracker = tracker_create(orch->config);
    if (tracker) {
        const char *terminal_states[16];
        int state_count = 0;
        for (int i = 0; i < orch->config->tracker.terminal_state_count && state_count < 16; i++) {
            terminal_states[state_count++] = orch->config->tracker.terminal_states[i];
        }
        
        symphony_issue_t *terminal_issues = NULL;
        int count = 0;
        symphony_error_t err = tracker_fetch_by_states(tracker, terminal_states, state_count,
                                                        &terminal_issues, &count);
        tracker_destroy(tracker);
        
        if (err == SYMPHONY_OK && count > 0) {
            LOG_INFO("Cleaning up %d terminal workspaces", count);
            workspace_cleanup_terminal(orch->config, terminal_issues, count);
            issue_list_destroy(terminal_issues, count);
        } else {
            issue_list_destroy(terminal_issues, count);
            if (err != SYMPHONY_OK) {
                LOG_WARN("Failed to fetch terminal issues for cleanup");
            }
        }
    }
    
    /* Initial tick */
    orchestrator_tick(orch);
    
    /* Main loop */
    while (!orch->should_stop) {
        /* Sleep for poll interval */
        usleep((useconds_t)orch->poll_interval_ms * 1000);
        
        if (orch->should_stop) break;
        
        orchestrator_tick(orch);
    }
    
    LOG_INFO("Orchestrator stopped");
    return SYMPHONY_OK;
}

/*
 * Stop orchestrator
 */
void orchestrator_stop(symphony_orchestrator_t *orch) {
    if (orch) {
        orch->should_stop = true;
    }
}

/*
 * Create snapshot for API/dashboard
 */
orchestrator_snapshot_t *orchestrator_snapshot(symphony_orchestrator_t *orch) {
    if (!orch) return NULL;
    
    orchestrator_snapshot_t *snap = calloc(1, sizeof(orchestrator_snapshot_t));
    if (!snap) return NULL;
    
    snap->running_count = orch->running_count;
    snap->retry_count = orch->retry_count;
    snap->total_input_tokens = orch->total_input_tokens;
    snap->total_output_tokens = orch->total_output_tokens;
    snap->total_tokens = orch->total_tokens;
    snap->seconds_running = orch->total_seconds_running;
    
    /* Add current running time */
    time_t now = time(NULL);
    for (int i = 0; i < orch->running_count; i++) {
        snap->seconds_running += difftime(now, orch->running[i].started_at);
    }
    
    /* Copy running entries */
    if (orch->running_count > 0) {
        snap->running = calloc((size_t)orch->running_count, sizeof(running_entry_t));
        if (snap->running) {
            memcpy(snap->running, orch->running, 
                   (size_t)orch->running_count * sizeof(running_entry_t));
        }
    }
    
    /* Copy retry entries */
    if (orch->retry_count > 0) {
        snap->retrying = calloc((size_t)orch->retry_count, sizeof(retry_entry_t));
        if (snap->retrying) {
            memcpy(snap->retrying, orch->retry_queue,
                   (size_t)orch->retry_count * sizeof(retry_entry_t));
        }
    }
    
    return snap;
}

void orchestrator_snapshot_destroy(orchestrator_snapshot_t *snap) {
    if (!snap) return;
    free(snap->running);
    free(snap->retrying);
    free(snap);
}
