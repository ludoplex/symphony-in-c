/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * orchestrator.h - Main orchestration loop
 */

#ifndef SYMPHONY_ORCHESTRATOR_H
#define SYMPHONY_ORCHESTRATOR_H

#include "symphony.h"
#include "config.h"
#include "tracker.h"

/* Orchestrator lifecycle */
symphony_orchestrator_t *orchestrator_create(symphony_config_t *cfg);
void orchestrator_destroy(symphony_orchestrator_t *orch);

/* Main run loop */
symphony_error_t orchestrator_start(symphony_orchestrator_t *orch);
void orchestrator_stop(symphony_orchestrator_t *orch);

/* Poll tick handling */
symphony_error_t orchestrator_tick(symphony_orchestrator_t *orch);

/* Issue dispatch */
bool orchestrator_can_dispatch(symphony_orchestrator_t *orch, symphony_issue_t *issue);
symphony_error_t orchestrator_dispatch(symphony_orchestrator_t *orch, symphony_issue_t *issue, int attempt);

/* Reconciliation */
symphony_error_t orchestrator_reconcile(symphony_orchestrator_t *orch);

/* Retry management */
void orchestrator_schedule_retry(symphony_orchestrator_t *orch, 
                                  const char *issue_id,
                                  const char *identifier,
                                  int attempt,
                                  const char *error);
void orchestrator_process_retries(symphony_orchestrator_t *orch);

/* Worker callbacks */
void orchestrator_on_worker_exit(symphony_orchestrator_t *orch, 
                                  const char *issue_id,
                                  bool success,
                                  const char *error);
void orchestrator_on_codex_event(symphony_orchestrator_t *orch,
                                  const char *issue_id,
                                  const char *event_type,
                                  uint64_t input_tokens,
                                  uint64_t output_tokens);

/* State accessors */
int orchestrator_available_slots(symphony_orchestrator_t *orch);
int orchestrator_running_count(symphony_orchestrator_t *orch);
bool orchestrator_is_running(symphony_orchestrator_t *orch, const char *issue_id);
bool orchestrator_is_claimed(symphony_orchestrator_t *orch, const char *issue_id);

/* Snapshot for API/dashboard */
typedef struct {
    int running_count;
    int retry_count;
    uint64_t total_input_tokens;
    uint64_t total_output_tokens;
    uint64_t total_tokens;
    double seconds_running;
    running_entry_t *running;
    retry_entry_t *retrying;
} orchestrator_snapshot_t;

orchestrator_snapshot_t *orchestrator_snapshot(symphony_orchestrator_t *orch);
void orchestrator_snapshot_destroy(orchestrator_snapshot_t *snap);

#endif /* SYMPHONY_ORCHESTRATOR_H */
