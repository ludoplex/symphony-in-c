/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * http_server.h - Web server for dashboard and API
 */

#ifndef SYMPHONY_HTTP_SERVER_H
#define SYMPHONY_HTTP_SERVER_H

#include "symphony.h"
#include "orchestrator.h"

/*
 * HTTP server instance
 */
typedef struct http_server http_server_t;

/* Server lifecycle */
http_server_t *http_server_create(symphony_orchestrator_t *orch, int port);
void http_server_destroy(http_server_t *srv);

/* Start serving (non-blocking) */
symphony_error_t http_server_start(http_server_t *srv);

/* Stop serving */
void http_server_stop(http_server_t *srv);

/* Check if server is running */
bool http_server_is_running(http_server_t *srv);

/* Get bound port (useful when port=0 for ephemeral) */
int http_server_get_port(http_server_t *srv);

/*
 * API endpoints implemented:
 *   GET /              - Dashboard HTML
 *   GET /api/v1/state  - System state JSON
 *   GET /api/v1/issues - Running issues JSON
 */

#endif /* SYMPHONY_HTTP_SERVER_H */
