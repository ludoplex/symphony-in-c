/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * workspace.c - Workspace management
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "workspace.h"
#include "util.h"

/*
 * Create workspace structure
 */
symphony_workspace_t *workspace_create(void) {
    symphony_workspace_t *ws = calloc(1, sizeof(symphony_workspace_t));
    return ws;
}

/*
 * Destroy workspace structure
 */
void workspace_destroy(symphony_workspace_t *ws) {
    free(ws);
}

/*
 * Sanitize issue identifier for use as directory name
 * Only allow [A-Za-z0-9._-], replace others with _
 */
void workspace_sanitize_key(const char *identifier, char *out, size_t out_size) {
    if (!identifier || !out || out_size == 0) return;
    
    size_t j = 0;
    for (size_t i = 0; identifier[i] && j < out_size - 1; i++) {
        char c = identifier[i];
        if ((c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '.' || c == '_' || c == '-') {
            out[j++] = c;
        } else {
            out[j++] = '_';
        }
    }
    out[j] = '\0';
}

/*
 * Validate that workspace path is under workspace root
 */
bool workspace_validate_path(const char *workspace_root, const char *workspace_path) {
    if (!workspace_root || !workspace_path) return false;
    
    /* Normalize both paths */
    char *root_norm = util_path_normalize(workspace_root);
    char *path_norm = util_path_normalize(workspace_path);
    
    if (!root_norm || !path_norm) {
        free(root_norm);
        free(path_norm);
        return false;
    }
    
    /* Check that path is under root */
    size_t root_len = strlen(root_norm);
    bool valid = strncmp(path_norm, root_norm, root_len) == 0 &&
                 (path_norm[root_len] == '/' || path_norm[root_len] == '\0');
    
    free(root_norm);
    free(path_norm);
    return valid;
}

/*
 * Get workspace path for an issue identifier
 */
char *workspace_get_path(symphony_config_t *cfg, const char *issue_identifier) {
    if (!cfg || !issue_identifier) return NULL;
    
    char key[MAX_IDENTIFIER_LEN];
    workspace_sanitize_key(issue_identifier, key, sizeof(key));
    
    return util_path_join(config_get_workspace_root(cfg), key);
}

/*
 * Remove temporary/cache files from workspace
 */
static void workspace_clean_temp_files(const char *workspace_path) {
    const char *temp_dirs[] = {"tmp", ".elixir_ls", "__pycache__", ".cache", "node_modules/.cache"};
    
    for (size_t i = 0; i < sizeof(temp_dirs) / sizeof(temp_dirs[0]); i++) {
        char *temp_path = util_path_join(workspace_path, temp_dirs[i]);
        if (temp_path) {
            if (util_path_exists(temp_path)) {
                util_rm_rf(temp_path);
            }
            free(temp_path);
        }
    }
}

/*
 * Ensure workspace exists for an issue
 */
symphony_error_t workspace_ensure(symphony_workspace_t *ws,
                                   symphony_config_t *cfg,
                                   const char *issue_identifier) {
    if (!ws || !cfg || !issue_identifier) return SYMPHONY_ERR_INTERNAL;
    
    /* Sanitize identifier */
    workspace_sanitize_key(issue_identifier, ws->workspace_key, sizeof(ws->workspace_key));
    
    /* Build workspace path */
    const char *root = config_get_workspace_root(cfg);
    char *path = util_path_join(root, ws->workspace_key);
    if (!path) return SYMPHONY_ERR_WORKSPACE_ERROR;
    
    strncpy(ws->path, path, sizeof(ws->path) - 1);
    free(path);
    
    /* Validate path is under root */
    if (!workspace_validate_path(root, ws->path)) {
        LOG_ERROR("Workspace path validation failed: %s is not under %s", 
                  ws->path, root);
        return SYMPHONY_ERR_WORKSPACE_ERROR;
    }
    
    /* Check if workspace exists */
    struct stat st;
    if (stat(ws->path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            /* Path exists but is not a directory */
            LOG_WARN("Workspace path exists but is not a directory: %s", ws->path);
            if (unlink(ws->path) != 0) {
                LOG_ERROR("Failed to remove non-directory at workspace path: %s", ws->path);
                return SYMPHONY_ERR_WORKSPACE_ERROR;
            }
        } else {
            /* Directory exists, reuse it */
            ws->created_now = false;
            workspace_clean_temp_files(ws->path);
            return SYMPHONY_OK;
        }
    }
    
    /* Ensure workspace root exists */
    if (!util_mkdir_p(root)) {
        LOG_ERROR("Failed to create workspace root: %s", root);
        return SYMPHONY_ERR_WORKSPACE_ERROR;
    }
    
    /* Create workspace directory */
    if (mkdir(ws->path, 0755) != 0) {
        LOG_ERROR("Failed to create workspace directory: %s", ws->path);
        return SYMPHONY_ERR_WORKSPACE_ERROR;
    }
    
    ws->created_now = true;
    LOG_INFO("Created new workspace: %s", ws->path);
    
    return SYMPHONY_OK;
}

/*
 * Run a hook script in the workspace directory
 */
symphony_error_t workspace_run_hook(symphony_config_t *cfg,
                                     const char *hook_script,
                                     const char *workspace_path) {
    if (!cfg || !hook_script || !workspace_path) return SYMPHONY_ERR_INTERNAL;
    
    int timeout_ms = cfg->hooks.timeout_ms;
    if (timeout_ms <= 0) timeout_ms = 60000;
    
    LOG_INFO("Running hook in %s", workspace_path);
    
    process_result_t *result = util_run_command(hook_script, workspace_path, timeout_ms);
    if (!result) {
        LOG_ERROR("Failed to run hook");
        return SYMPHONY_ERR_WORKSPACE_ERROR;
    }
    
    if (result->exit_code != 0) {
        LOG_ERROR("Hook failed with exit code %d: %s", 
                  result->exit_code, 
                  result->stderr_data ? result->stderr_data : "(no output)");
        util_process_result_destroy(result);
        return SYMPHONY_ERR_WORKSPACE_ERROR;
    }
    
    util_process_result_destroy(result);
    return SYMPHONY_OK;
}

/*
 * Run after_create hook
 */
symphony_error_t workspace_after_create(symphony_config_t *cfg, symphony_workspace_t *ws) {
    if (!cfg || !ws) return SYMPHONY_ERR_INTERNAL;
    if (!ws->created_now) return SYMPHONY_OK;  /* Only run on new workspace */
    if (!cfg->hooks.after_create) return SYMPHONY_OK;
    
    LOG_INFO("Running after_create hook for %s", ws->workspace_key);
    symphony_error_t err = workspace_run_hook(cfg, cfg->hooks.after_create, ws->path);
    
    if (err != SYMPHONY_OK) {
        /* after_create failure is fatal - remove the workspace */
        LOG_ERROR("after_create hook failed, removing workspace");
        util_rm_rf(ws->path);
    }
    
    return err;
}

/*
 * Run before_run hook
 */
symphony_error_t workspace_before_run(symphony_config_t *cfg, symphony_workspace_t *ws) {
    if (!cfg || !ws) return SYMPHONY_ERR_INTERNAL;
    if (!cfg->hooks.before_run) return SYMPHONY_OK;
    
    LOG_INFO("Running before_run hook for %s", ws->workspace_key);
    return workspace_run_hook(cfg, cfg->hooks.before_run, ws->path);
}

/*
 * Run after_run hook (best effort)
 */
void workspace_after_run(symphony_config_t *cfg, symphony_workspace_t *ws) {
    if (!cfg || !ws) return;
    if (!cfg->hooks.after_run) return;
    
    LOG_INFO("Running after_run hook for %s", ws->workspace_key);
    symphony_error_t err = workspace_run_hook(cfg, cfg->hooks.after_run, ws->path);
    if (err != SYMPHONY_OK) {
        LOG_WARN("after_run hook failed (ignored)");
    }
}

/*
 * Run before_remove hook (best effort)
 */
void workspace_before_remove(symphony_config_t *cfg, symphony_workspace_t *ws) {
    if (!cfg || !ws) return;
    if (!cfg->hooks.before_remove) return;
    if (!util_path_exists(ws->path)) return;
    
    LOG_INFO("Running before_remove hook for %s", ws->workspace_key);
    symphony_error_t err = workspace_run_hook(cfg, cfg->hooks.before_remove, ws->path);
    if (err != SYMPHONY_OK) {
        LOG_WARN("before_remove hook failed (ignored)");
    }
}

/*
 * Remove a workspace for an issue
 */
symphony_error_t workspace_remove(symphony_config_t *cfg, const char *issue_identifier) {
    if (!cfg || !issue_identifier) return SYMPHONY_ERR_INTERNAL;
    
    char *path = workspace_get_path(cfg, issue_identifier);
    if (!path) return SYMPHONY_ERR_WORKSPACE_ERROR;
    
    /* Create temporary workspace for hooks */
    symphony_workspace_t ws = {0};
    strncpy(ws.path, path, sizeof(ws.path) - 1);
    workspace_sanitize_key(issue_identifier, ws.workspace_key, sizeof(ws.workspace_key));
    
    /* Run before_remove hook */
    workspace_before_remove(cfg, &ws);
    
    /* Remove directory */
    if (util_path_exists(path)) {
        LOG_INFO("Removing workspace: %s", path);
        if (!util_rm_rf(path)) {
            LOG_WARN("Failed to remove workspace: %s", path);
        }
    }
    
    free(path);
    return SYMPHONY_OK;
}

/*
 * Cleanup workspaces for terminal issues
 */
void workspace_cleanup_terminal(symphony_config_t *cfg, symphony_issue_t *issues, int count) {
    if (!cfg || !issues || count <= 0) return;
    
    for (int i = 0; i < count; i++) {
        if (issues[i].identifier[0]) {
            workspace_remove(cfg, issues[i].identifier);
        }
    }
}
