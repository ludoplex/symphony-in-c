/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * tracker.h - Issue tracker client interface
 */

#ifndef SYMPHONY_TRACKER_H
#define SYMPHONY_TRACKER_H

#include "symphony.h"
#include "config.h"

/*
 * Tracker client interface
 */
typedef struct tracker_client tracker_client_t;

/* Create/destroy tracker client */
tracker_client_t *tracker_create(symphony_config_t *cfg);
void tracker_destroy(tracker_client_t *client);

/* Fetch candidate issues (active states) */
symphony_error_t tracker_fetch_candidates(tracker_client_t *client,
                                          symphony_issue_t **issues,
                                          int *count);

/* Fetch issues by specific states (for terminal cleanup) */
symphony_error_t tracker_fetch_by_states(tracker_client_t *client,
                                         const char *states[],
                                         int state_count,
                                         symphony_issue_t **issues,
                                         int *count);

/* Fetch current state for specific issue IDs (reconciliation) */
symphony_error_t tracker_fetch_states_by_ids(tracker_client_t *client,
                                             const char *ids[],
                                             int id_count,
                                             symphony_issue_t **issues,
                                             int *count);

/* Issue utilities */
symphony_issue_t *issue_create(void);
void issue_destroy(symphony_issue_t *issue);
void issue_list_destroy(symphony_issue_t *issues, int count);
symphony_issue_t *issue_clone(symphony_issue_t *issue);

/* Issue sorting (priority, created_at, identifier) */
int issue_compare(const void *a, const void *b);
void issue_sort(symphony_issue_t *issues, int count);

/* Blocker checking */
bool issue_has_nonterminal_blockers(symphony_issue_t *issue, symphony_config_t *cfg);

#endif /* SYMPHONY_TRACKER_H */
