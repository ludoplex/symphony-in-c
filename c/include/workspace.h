/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * workspace.h - Workspace management
 */

#ifndef SYMPHONY_WORKSPACE_H
#define SYMPHONY_WORKSPACE_H

#include "symphony.h"
#include "config.h"

/* Workspace functions */
symphony_workspace_t *workspace_create(void);
void workspace_destroy(symphony_workspace_t *ws);

/* Create or reuse workspace for an issue */
symphony_error_t workspace_ensure(symphony_workspace_t *ws, 
                                   symphony_config_t *cfg,
                                   const char *issue_identifier);

/* Path utilities */
void workspace_sanitize_key(const char *identifier, char *out, size_t out_size);
bool workspace_validate_path(const char *workspace_root, const char *workspace_path);
char *workspace_get_path(symphony_config_t *cfg, const char *issue_identifier);

/* Lifecycle hooks */
symphony_error_t workspace_run_hook(symphony_config_t *cfg, 
                                     const char *hook_script,
                                     const char *workspace_path);
symphony_error_t workspace_after_create(symphony_config_t *cfg, symphony_workspace_t *ws);
symphony_error_t workspace_before_run(symphony_config_t *cfg, symphony_workspace_t *ws);
void workspace_after_run(symphony_config_t *cfg, symphony_workspace_t *ws);
void workspace_before_remove(symphony_config_t *cfg, symphony_workspace_t *ws);

/* Cleanup */
symphony_error_t workspace_remove(symphony_config_t *cfg, const char *issue_identifier);
void workspace_cleanup_terminal(symphony_config_t *cfg, symphony_issue_t *issues, int count);

#endif /* SYMPHONY_WORKSPACE_H */
