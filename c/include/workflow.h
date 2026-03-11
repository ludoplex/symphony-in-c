/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * workflow.h - WORKFLOW.md parsing
 */

#ifndef SYMPHONY_WORKFLOW_H
#define SYMPHONY_WORKFLOW_H

#include "symphony.h"

/*
 * Workflow definition - parsed WORKFLOW.md
 */
typedef struct {
    char *raw_content;          /* Full file content */
    char *yaml_front_matter;    /* YAML between --- markers */
    char *prompt_body;          /* Markdown after front matter */
} workflow_def_t;

/* Workflow functions */
workflow_def_t *workflow_create(void);
void workflow_destroy(workflow_def_t *wf);
symphony_error_t workflow_parse_file(workflow_def_t *wf, const char *path);
symphony_error_t workflow_parse_string(workflow_def_t *wf, const char *content);

/* YAML parsing helpers */
symphony_error_t workflow_get_string(workflow_def_t *wf, const char *key, char *out, size_t out_size);
symphony_error_t workflow_get_int(workflow_def_t *wf, const char *key, int *out);
symphony_error_t workflow_get_string_list(workflow_def_t *wf, const char *key, 
                                          char out[][MAX_STATE_LEN], int *count, int max_count);
symphony_error_t workflow_get_nested_string(workflow_def_t *wf, const char *parent, 
                                            const char *key, char *out, size_t out_size);
symphony_error_t workflow_get_nested_int(workflow_def_t *wf, const char *parent, 
                                         const char *key, int *out);

#endif /* SYMPHONY_WORKFLOW_H */
