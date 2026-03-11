/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * template.h - Template rendering using Lua
 */

#ifndef SYMPHONY_TEMPLATE_H
#define SYMPHONY_TEMPLATE_H

#include "symphony.h"
#include "config.h"

/*
 * Template engine (backed by Lua)
 */
typedef struct template_engine template_engine_t;

/* Template engine lifecycle */
template_engine_t *template_create(void);
void template_destroy(template_engine_t *eng);

/*
 * Render a prompt template with issue data
 * 
 * Variables available in template:
 *   - issue.id, issue.identifier, issue.title, issue.description
 *   - issue.priority, issue.state, issue.url
 *   - issue.labels (array)
 *   - issue.blocked_by (array of {id, identifier, state})
 *   - attempt (integer or nil for first run)
 */
symphony_error_t template_render(template_engine_t *eng,
                                  const char *template_str,
                                  symphony_issue_t *issue,
                                  int attempt,  /* 0 for first run */
                                  char **output);

/* Validate template syntax without rendering */
symphony_error_t template_validate(template_engine_t *eng,
                                    const char *template_str,
                                    char *error_buf,
                                    size_t error_buf_size);

#endif /* SYMPHONY_TEMPLATE_H */
