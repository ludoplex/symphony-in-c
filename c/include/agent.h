/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * agent.h - Codex app-server integration
 */

#ifndef SYMPHONY_AGENT_H
#define SYMPHONY_AGENT_H

#include "symphony.h"
#include "config.h"

/*
 * Agent runner - manages codex subprocess
 */
typedef struct agent_runner agent_runner_t;

/*
 * Codex event types
 */
typedef enum {
    AGENT_EVENT_SESSION_STARTED,
    AGENT_EVENT_STARTUP_FAILED,
    AGENT_EVENT_TURN_COMPLETED,
    AGENT_EVENT_TURN_FAILED,
    AGENT_EVENT_TURN_CANCELLED,
    AGENT_EVENT_TURN_ERROR,
    AGENT_EVENT_INPUT_REQUIRED,
    AGENT_EVENT_AUTO_APPROVED,
    AGENT_EVENT_UNSUPPORTED_TOOL,
    AGENT_EVENT_NOTIFICATION,
    AGENT_EVENT_OTHER,
    AGENT_EVENT_MALFORMED
} agent_event_type_t;

/*
 * Codex event callback
 */
typedef void (*agent_event_callback_t)(
    void *ctx,
    agent_event_type_t type,
    const char *message,
    uint64_t input_tokens,
    uint64_t output_tokens
);

/* Agent runner lifecycle */
agent_runner_t *agent_create(symphony_config_t *cfg);
void agent_destroy(agent_runner_t *agent);

/* Start a session */
symphony_error_t agent_start_session(agent_runner_t *agent,
                                      const char *workspace_path,
                                      symphony_session_t *session);

/* Run a turn with the given prompt */
symphony_error_t agent_run_turn(agent_runner_t *agent,
                                 symphony_session_t *session,
                                 const char *prompt,
                                 const char *title,
                                 agent_event_callback_t callback,
                                 void *callback_ctx);

/* Send continuation guidance (not full prompt) */
symphony_error_t agent_continue_turn(agent_runner_t *agent,
                                      symphony_session_t *session,
                                      const char *guidance,
                                      agent_event_callback_t callback,
                                      void *callback_ctx);

/* Stop the session */
void agent_stop_session(agent_runner_t *agent, symphony_session_t *session);

/* Check if session is still alive */
bool agent_session_alive(agent_runner_t *agent, symphony_session_t *session);

#endif /* SYMPHONY_AGENT_H */
