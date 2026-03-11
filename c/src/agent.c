/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * agent.c - Codex app-server integration
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "agent.h"
#include "util.h"

/*
 * Agent runner structure
 */
struct agent_runner {
    symphony_config_t *config;
    pid_t pid;
    int stdin_fd;
    int stdout_fd;
    int stderr_fd;
    int next_id;
    char line_buffer[MAX_LINE_LEN];
    size_t line_len;
};

/*
 * Create agent runner
 */
agent_runner_t *agent_create(symphony_config_t *cfg) {
    if (!cfg) return NULL;
    
    agent_runner_t *agent = calloc(1, sizeof(agent_runner_t));
    if (!agent) return NULL;
    
    agent->config = cfg;
    agent->stdin_fd = -1;
    agent->stdout_fd = -1;
    agent->stderr_fd = -1;
    agent->next_id = 1;
    
    return agent;
}

/*
 * Destroy agent runner
 */
void agent_destroy(agent_runner_t *agent) {
    if (!agent) return;
    
    /* Clean up pipes */
    if (agent->stdin_fd >= 0) close(agent->stdin_fd);
    if (agent->stdout_fd >= 0) close(agent->stdout_fd);
    if (agent->stderr_fd >= 0) close(agent->stderr_fd);
    
    /* Kill process if still running */
    if (agent->pid > 0) {
        kill(agent->pid, SIGTERM);
        int status;
        waitpid(agent->pid, &status, WNOHANG);
    }
    
    free(agent);
}

/*
 * Send JSON-RPC message
 */
static bool agent_send(agent_runner_t *agent, const char *json) {
    if (!agent || agent->stdin_fd < 0 || !json) return false;
    
    size_t len = strlen(json);
    ssize_t written = write(agent->stdin_fd, json, len);
    if (written != (ssize_t)len) return false;
    
    /* Write newline */
    written = write(agent->stdin_fd, "\n", 1);
    return written == 1;
}

/*
 * Read a line from stdout
 */
static char *agent_read_line(agent_runner_t *agent, int timeout_ms) {
    if (!agent || agent->stdout_fd < 0) return NULL;
    
    int64_t deadline = util_monotonic_ms() + timeout_ms;
    
    while (1) {
        /* Check for complete line in buffer */
        char *newline = memchr(agent->line_buffer, '\n', agent->line_len);
        if (newline) {
            size_t line_len = (size_t)(newline - agent->line_buffer);
            char *line = util_strndup(agent->line_buffer, line_len);
            
            /* Shift buffer */
            size_t remaining = agent->line_len - line_len - 1;
            memmove(agent->line_buffer, newline + 1, remaining);
            agent->line_len = remaining;
            
            return line;
        }
        
        /* Check timeout */
        int64_t remaining_ms = deadline - util_monotonic_ms();
        if (remaining_ms <= 0) return NULL;
        
        /* Set up select */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(agent->stdout_fd, &read_fds);
        
        struct timeval tv;
        tv.tv_sec = remaining_ms / 1000;
        tv.tv_usec = (remaining_ms % 1000) * 1000;
        
        int result = select(agent->stdout_fd + 1, &read_fds, NULL, NULL, &tv);
        if (result <= 0) return NULL;
        
        /* Read more data */
        size_t space = sizeof(agent->line_buffer) - agent->line_len - 1;
        if (space == 0) {
            LOG_ERROR("Agent line buffer overflow");
            return NULL;
        }
        
        ssize_t bytes = read(agent->stdout_fd, 
                             agent->line_buffer + agent->line_len, 
                             space);
        if (bytes <= 0) return NULL;
        
        agent->line_len += (size_t)bytes;
        agent->line_buffer[agent->line_len] = '\0';
    }
}

/*
 * Send request and wait for response
 */
static char *agent_request(agent_runner_t *agent, int id, 
                           const char *method, const char *params,
                           int timeout_ms) {
    if (!agent || !method) return NULL;
    
    /* Build request */
    char request[65536];
    if (params) {
        snprintf(request, sizeof(request),
                 "{\"id\":%d,\"method\":\"%s\",\"params\":%s}",
                 id, method, params);
    } else {
        snprintf(request, sizeof(request),
                 "{\"id\":%d,\"method\":\"%s\",\"params\":{}}",
                 id, method);
    }
    
    if (!agent_send(agent, request)) {
        LOG_ERROR("Failed to send request: %s", method);
        return NULL;
    }
    
    /* Wait for response with matching ID */
    char *line;
    while ((line = agent_read_line(agent, timeout_ms)) != NULL) {
        /* Check if response has our ID */
        int resp_id = util_json_get_int(line, "id", -1);
        if (resp_id == id) {
            return line;
        }
        
        /* Handle notification or other message */
        free(line);
    }
    
    LOG_ERROR("Timeout waiting for response to %s", method);
    return NULL;
}

/*
 * Send notification (no response expected)
 */
static bool agent_notify(agent_runner_t *agent, const char *method, const char *params) {
    if (!agent || !method) return false;
    
    char notification[65536];
    if (params) {
        snprintf(notification, sizeof(notification),
                 "{\"method\":\"%s\",\"params\":%s}",
                 method, params);
    } else {
        snprintf(notification, sizeof(notification),
                 "{\"method\":\"%s\",\"params\":{}}",
                 method);
    }
    
    return agent_send(agent, notification);
}

/*
 * Start agent session
 */
symphony_error_t agent_start_session(agent_runner_t *agent,
                                      const char *workspace_path,
                                      symphony_session_t *session) {
    if (!agent || !workspace_path || !session) return SYMPHONY_ERR_INTERNAL;
    
    symphony_config_t *cfg = agent->config;
    
    /* Create pipes */
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        LOG_ERROR("Failed to create pipes: %s", strerror(errno));
        return SYMPHONY_ERR_AGENT_ERROR;
    }
    
    /* Build command */
    char command[MAX_PATH_LEN + 256];
    snprintf(command, sizeof(command), "bash -lc '%s'", cfg->codex.command);
    
    /* Fork */
    pid_t pid = fork();
    if (pid < 0) {
        LOG_ERROR("Fork failed: %s", strerror(errno));
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return SYMPHONY_ERR_AGENT_ERROR;
    }
    
    if (pid == 0) {
        /* Child process */
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        
        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        if (chdir(workspace_path) != 0) {
            fprintf(stderr, "Failed to chdir to %s: %s\n", workspace_path, strerror(errno));
            _exit(127);
        }
        
        execl("/bin/sh", "sh", "-c", command, NULL);
        _exit(127);
    }
    
    /* Parent process */
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    agent->pid = pid;
    agent->stdin_fd = stdin_pipe[1];
    agent->stdout_fd = stdout_pipe[0];
    agent->stderr_fd = stderr_pipe[0];
    
    /* Set non-blocking for stderr */
    fcntl(agent->stderr_fd, F_SETFL, O_NONBLOCK);
    
    LOG_INFO("Started agent process (PID %d) in %s", pid, workspace_path);
    
    /* Protocol handshake */
    int timeout_ms = cfg->codex.read_timeout_ms;
    
    /* 1. Initialize */
    char init_params[512];
    snprintf(init_params, sizeof(init_params),
             "{\"clientInfo\":{\"name\":\"symphony\",\"version\":\"%s\"},\"capabilities\":{}}",
             SYMPHONY_VERSION);
    
    int init_id = agent->next_id++;
    char *response = agent_request(agent, init_id, "initialize", init_params, timeout_ms);
    if (!response) {
        LOG_ERROR("Initialize request failed");
        agent_stop_session(agent, session);
        return SYMPHONY_ERR_AGENT_ERROR;
    }
    free(response);
    
    /* 2. Initialized notification */
    if (!agent_notify(agent, "initialized", NULL)) {
        LOG_ERROR("Initialized notification failed");
        agent_stop_session(agent, session);
        return SYMPHONY_ERR_AGENT_ERROR;
    }
    
    /* 3. Thread start */
    char thread_params[4096];
    snprintf(thread_params, sizeof(thread_params),
             "{\"approvalPolicy\":\"%s\",\"sandbox\":\"%s\",\"cwd\":\"%s\"}",
             cfg->codex.approval_policy[0] ? cfg->codex.approval_policy : "auto-edit",
             cfg->codex.thread_sandbox[0] ? cfg->codex.thread_sandbox : "none",
             workspace_path);
    
    int thread_id = agent->next_id++;
    response = agent_request(agent, thread_id, "thread/start", thread_params, timeout_ms);
    if (!response) {
        LOG_ERROR("Thread start request failed");
        agent_stop_session(agent, session);
        return SYMPHONY_ERR_AGENT_ERROR;
    }
    
    /* Extract thread ID */
    char *tid = util_json_get_nested(response, "result", "thread");
    if (!tid) tid = util_json_get_nested(response, "result", "id");
    if (tid) {
        strncpy(session->thread_id, tid, sizeof(session->thread_id) - 1);
        free(tid);
    }
    free(response);
    
    if (!session->thread_id[0]) {
        LOG_ERROR("Failed to get thread ID");
        agent_stop_session(agent, session);
        return SYMPHONY_ERR_AGENT_ERROR;
    }
    
    session->codex_pid = pid;
    session->turn_count = 0;
    
    LOG_INFO("Agent session started (thread=%s)", session->thread_id);
    return SYMPHONY_OK;
}

/*
 * Run a turn with the given prompt
 */
symphony_error_t agent_run_turn(agent_runner_t *agent,
                                 symphony_session_t *session,
                                 const char *prompt,
                                 const char *title,
                                 agent_event_callback_t callback,
                                 void *callback_ctx) {
    if (!agent || !session || !prompt) return SYMPHONY_ERR_INTERNAL;
    
    symphony_config_t *cfg = agent->config;
    
    /* Build turn start params */
    char *escaped_prompt = util_json_escape(prompt);
    char *escaped_title = util_json_escape(title ? title : "");
    
    size_t params_size = strlen(escaped_prompt) + strlen(escaped_title) + 2048;
    char *turn_params = malloc(params_size);
    if (!turn_params) {
        free(escaped_prompt);
        free(escaped_title);
        return SYMPHONY_ERR_INTERNAL;
    }
    
    snprintf(turn_params, params_size,
             "{\"threadId\":\"%s\",\"input\":[{\"type\":\"text\",\"text\":\"%s\"}],"
             "\"cwd\":\"%s\",\"title\":\"%s\","
             "\"approvalPolicy\":\"%s\",\"sandboxPolicy\":{\"type\":\"%s\"}}",
             session->thread_id, escaped_prompt,
             "",  /* cwd already set in thread start */
             escaped_title,
             cfg->codex.approval_policy[0] ? cfg->codex.approval_policy : "auto-edit",
             cfg->codex.turn_sandbox_policy[0] ? cfg->codex.turn_sandbox_policy : "none");
    
    free(escaped_prompt);
    free(escaped_title);
    
    /* Send turn start */
    int turn_request_id = agent->next_id++;
    char request[65536];
    snprintf(request, sizeof(request),
             "{\"id\":%d,\"method\":\"turn/start\",\"params\":%s}",
             turn_request_id, turn_params);
    free(turn_params);
    
    if (!agent_send(agent, request)) {
        LOG_ERROR("Failed to send turn/start");
        return SYMPHONY_ERR_AGENT_ERROR;
    }
    
    session->turn_count++;
    
    /* Stream turn events until completion */
    int64_t turn_deadline = util_monotonic_ms() + cfg->codex.turn_timeout_ms;
    bool turn_completed = false;
    symphony_error_t result = SYMPHONY_OK;
    
    while (!turn_completed) {
        int64_t remaining_ms = turn_deadline - util_monotonic_ms();
        if (remaining_ms <= 0) {
            LOG_ERROR("Turn timeout");
            result = SYMPHONY_ERR_AGENT_ERROR;
            break;
        }
        
        char *line = agent_read_line(agent, (int)remaining_ms);
        if (!line) {
            LOG_ERROR("Failed to read agent output");
            result = SYMPHONY_ERR_AGENT_ERROR;
            break;
        }
        
        /* Parse message */
        char *method = util_json_get_string(line, "method");
        int msg_id = util_json_get_int(line, "id", -1);
        
        /* Check for turn completion */
        if (method) {
            if (strcmp(method, "turn/completed") == 0) {
                turn_completed = true;
                if (callback) {
                    callback(callback_ctx, AGENT_EVENT_TURN_COMPLETED, NULL, 0, 0);
                }
            } else if (strcmp(method, "turn/failed") == 0) {
                turn_completed = true;
                result = SYMPHONY_ERR_AGENT_ERROR;
                if (callback) {
                    callback(callback_ctx, AGENT_EVENT_TURN_FAILED, NULL, 0, 0);
                }
            } else if (strcmp(method, "turn/cancelled") == 0) {
                turn_completed = true;
                result = SYMPHONY_ERR_AGENT_ERROR;
                if (callback) {
                    callback(callback_ctx, AGENT_EVENT_TURN_CANCELLED, NULL, 0, 0);
                }
            } else if (strstr(method, "input") || strstr(method, "Input")) {
                /* User input required - fail immediately */
                turn_completed = true;
                result = SYMPHONY_ERR_AGENT_ERROR;
                if (callback) {
                    callback(callback_ctx, AGENT_EVENT_INPUT_REQUIRED, NULL, 0, 0);
                }
            } else if (strstr(method, "approval")) {
                /* Auto-approve */
                char *approval_id = util_json_get_string(line, "id");
                if (approval_id) {
                    char approve_response[256];
                    snprintf(approve_response, sizeof(approve_response),
                             "{\"id\":\"%s\",\"result\":{\"approved\":true}}",
                             approval_id);
                    agent_send(agent, approve_response);
                    free(approval_id);
                }
                if (callback) {
                    callback(callback_ctx, AGENT_EVENT_AUTO_APPROVED, method, 0, 0);
                }
            }
            free(method);
        }
        
        /* Check for response to our turn start */
        if (msg_id == turn_request_id) {
            /* Got turn start response */
            char *tid = util_json_get_nested(line, "result", "turn");
            if (!tid) tid = util_json_get_nested(line, "result", "id");
            if (tid) {
                strncpy(session->turn_id, tid, sizeof(session->turn_id) - 1);
                snprintf(session->session_id, sizeof(session->session_id),
                         "%s-%s", session->thread_id, session->turn_id);
                free(tid);
            }
        }
        
        /* Extract token usage */
        uint64_t input_tokens = (uint64_t)util_json_get_int(line, "input_tokens", 0);
        uint64_t output_tokens = (uint64_t)util_json_get_int(line, "output_tokens", 0);
        if (input_tokens > 0 || output_tokens > 0) {
            session->input_tokens = input_tokens;
            session->output_tokens = output_tokens;
            session->total_tokens = input_tokens + output_tokens;
        }
        
        session->last_timestamp = time(NULL);
        free(line);
    }
    
    return result;
}

/*
 * Send continuation guidance
 */
symphony_error_t agent_continue_turn(agent_runner_t *agent,
                                      symphony_session_t *session,
                                      const char *guidance,
                                      agent_event_callback_t callback,
                                      void *callback_ctx) {
    /* For continuation, use a simpler prompt */
    return agent_run_turn(agent, session, guidance, NULL, callback, callback_ctx);
}

/*
 * Stop agent session
 */
void agent_stop_session(agent_runner_t *agent, symphony_session_t *session) {
    if (!agent) return;
    
    (void)session;  /* Not used for now */
    
    /* Close pipes */
    if (agent->stdin_fd >= 0) {
        close(agent->stdin_fd);
        agent->stdin_fd = -1;
    }
    if (agent->stdout_fd >= 0) {
        close(agent->stdout_fd);
        agent->stdout_fd = -1;
    }
    if (agent->stderr_fd >= 0) {
        close(agent->stderr_fd);
        agent->stderr_fd = -1;
    }
    
    /* Kill process */
    if (agent->pid > 0) {
        kill(agent->pid, SIGTERM);
        
        /* Wait briefly for graceful exit */
        int status;
        for (int i = 0; i < 10; i++) {
            int result = waitpid(agent->pid, &status, WNOHANG);
            if (result > 0) break;
            usleep(100000);  /* 100ms */
        }
        
        /* Force kill if needed */
        kill(agent->pid, SIGKILL);
        waitpid(agent->pid, &status, 0);
        
        LOG_INFO("Agent process stopped (PID %d)", agent->pid);
        agent->pid = 0;
    }
}

/*
 * Check if session is still alive
 */
bool agent_session_alive(agent_runner_t *agent, symphony_session_t *session) {
    if (!agent || agent->pid <= 0) return false;
    (void)session;
    
    int status;
    int result = waitpid(agent->pid, &status, WNOHANG);
    return result == 0;  /* Still running */
}
