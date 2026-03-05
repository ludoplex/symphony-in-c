/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * http_server.c - Web server for dashboard and API
 *
 * This implementation uses a simple single-threaded HTTP server.
 * For production use with Cosmopolitan, this would be replaced with
 * Redbean's built-in web server capabilities.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "http_server.h"
#include "util.h"

#define HTTP_BUFFER_SIZE 65536
#define MAX_CONNECTIONS 16

/*
 * HTTP server structure
 */
struct http_server {
    symphony_orchestrator_t *orchestrator;
    int port;
    int server_fd;
    bool running;
    pthread_t thread;
};

/*
 * HTTP response helper
 */
static void http_send_response(int client_fd, int status, const char *content_type, 
                                const char *body, size_t body_len) {
    char header[1024];
    const char *status_text;
    
    switch (status) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 500: status_text = "Internal Server Error"; break;
        default: status_text = "OK"; break;
    }
    
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, status_text, content_type, body_len);
    
    write(client_fd, header, (size_t)header_len);
    if (body && body_len > 0) {
        write(client_fd, body, body_len);
    }
}

/*
 * Generate JSON state response
 */
static char *generate_state_json(symphony_orchestrator_t *orch) {
    orchestrator_snapshot_t *snap = orchestrator_snapshot(orch);
    if (!snap) return util_strdup("{\"error\":\"failed to get snapshot\"}");
    
    /* Build JSON */
    size_t size = 4096 + (size_t)snap->running_count * 1024 + (size_t)snap->retry_count * 512;
    char *json = malloc(size);
    if (!json) {
        orchestrator_snapshot_destroy(snap);
        return util_strdup("{\"error\":\"out of memory\"}");
    }
    
    char *timestamp = util_format_iso8601(time(NULL));
    
    int pos = snprintf(json, size,
        "{\n"
        "  \"generated_at\": \"%s\",\n"
        "  \"counts\": {\n"
        "    \"running\": %d,\n"
        "    \"retry_queued\": %d\n"
        "  },\n"
        "  \"codex_totals\": {\n"
        "    \"input_tokens\": %llu,\n"
        "    \"output_tokens\": %llu,\n"
        "    \"total_tokens\": %llu,\n"
        "    \"seconds_running\": %.1f\n"
        "  },\n",
        timestamp ? timestamp : "",
        snap->running_count,
        snap->retry_count,
        (unsigned long long)snap->total_input_tokens,
        (unsigned long long)snap->total_output_tokens,
        (unsigned long long)snap->total_tokens,
        snap->seconds_running);
    
    free(timestamp);
    
    /* Running sessions */
    pos += snprintf(json + pos, size - (size_t)pos, "  \"running\": [\n");
    for (int i = 0; i < snap->running_count; i++) {
        running_entry_t *entry = &snap->running[i];
        char *started = util_format_iso8601(entry->started_at);
        
        pos += snprintf(json + pos, size - (size_t)pos,
            "    {\n"
            "      \"issue_id\": \"%s\",\n"
            "      \"identifier\": \"%s\",\n"
            "      \"attempt\": %d,\n"
            "      \"started_at\": \"%s\",\n"
            "      \"session_id\": \"%s\",\n"
            "      \"turn_count\": %d\n"
            "    }%s\n",
            entry->issue_id,
            entry->identifier,
            entry->attempt,
            started ? started : "",
            entry->session.session_id,
            entry->session.turn_count,
            i < snap->running_count - 1 ? "," : "");
        
        free(started);
    }
    pos += snprintf(json + pos, size - (size_t)pos, "  ],\n");
    
    /* Retry queue */
    pos += snprintf(json + pos, size - (size_t)pos, "  \"retry_queue\": [\n");
    for (int i = 0; i < snap->retry_count; i++) {
        retry_entry_t *entry = &snap->retrying[i];
        
        pos += snprintf(json + pos, size - (size_t)pos,
            "    {\n"
            "      \"issue_id\": \"%s\",\n"
            "      \"identifier\": \"%s\",\n"
            "      \"attempt\": %d,\n"
            "      \"error\": \"%s\"\n"
            "    }%s\n",
            entry->issue_id,
            entry->identifier,
            entry->attempt,
            entry->error,
            i < snap->retry_count - 1 ? "," : "");
    }
    pos += snprintf(json + pos, size - (size_t)pos, "  ]\n");
    
    snprintf(json + pos, size - (size_t)pos, "}\n");
    
    orchestrator_snapshot_destroy(snap);
    return json;
}

/*
 * Generate HTML dashboard
 */
static const char *DASHBOARD_HTML = 
"<!DOCTYPE html>\n"
"<html lang=\"en\">\n"
"<head>\n"
"  <meta charset=\"UTF-8\">\n"
"  <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
"  <title>Symphony Dashboard</title>\n"
"  <script src=\"https://unpkg.com/htmx.org@1.9.12\"></script>\n"
"  <style>\n"
"    :root { --bg: #0d1117; --card: #161b22; --border: #30363d; --text: #c9d1d9; --accent: #58a6ff; }\n"
"    * { box-sizing: border-box; margin: 0; padding: 0; }\n"
"    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;\n"
"           background: var(--bg); color: var(--text); padding: 20px; min-height: 100vh; }\n"
"    header { display: flex; align-items: center; gap: 16px; margin-bottom: 24px; }\n"
"    header h1 { font-size: 24px; font-weight: 600; }\n"
"    .stats { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 16px; margin-bottom: 24px; }\n"
"    .stat-card { background: var(--card); border: 1px solid var(--border); border-radius: 8px; padding: 16px; }\n"
"    .stat-card h3 { font-size: 12px; text-transform: uppercase; color: #8b949e; margin-bottom: 8px; }\n"
"    .stat-card .value { font-size: 32px; font-weight: 600; color: var(--accent); }\n"
"    .section { background: var(--card); border: 1px solid var(--border); border-radius: 8px; margin-bottom: 24px; }\n"
"    .section-header { padding: 16px; border-bottom: 1px solid var(--border); font-weight: 600; }\n"
"    .section-body { padding: 16px; }\n"
"    table { width: 100%%; border-collapse: collapse; }\n"
"    th, td { text-align: left; padding: 12px 16px; border-bottom: 1px solid var(--border); }\n"
"    th { font-size: 12px; text-transform: uppercase; color: #8b949e; }\n"
"    .status-running { color: #3fb950; }\n"
"    .status-retry { color: #d29922; }\n"
"    .empty { color: #8b949e; font-style: italic; }\n"
"    #state-container { min-height: 200px; }\n"
"  </style>\n"
"</head>\n"
"<body>\n"
"  <header>\n"
"    <h1>🎼 Symphony</h1>\n"
"    <span>Orchestrator Dashboard</span>\n"
"  </header>\n"
"\n"
"  <div id=\"state-container\" hx-get=\"/api/v1/state/html\" hx-trigger=\"load, every 5s\" hx-swap=\"innerHTML\">\n"
"    <p class=\"empty\">Loading...</p>\n"
"  </div>\n"
"\n"
"  <footer style=\"margin-top: 40px; color: #8b949e; font-size: 12px;\">\n"
"    Symphony v1.0.0 &mdash; Built with Cosmopolitan libc\n"
"  </footer>\n"
"</body>\n"
"</html>\n";

/*
 * Generate HTML fragment for HTMX updates
 */
static char *generate_state_html(symphony_orchestrator_t *orch) {
    orchestrator_snapshot_t *snap = orchestrator_snapshot(orch);
    if (!snap) return util_strdup("<p class=\"empty\">Failed to get state</p>");
    
    size_t size = 8192 + (size_t)snap->running_count * 512 + (size_t)snap->retry_count * 512;
    char *html = malloc(size);
    if (!html) {
        orchestrator_snapshot_destroy(snap);
        return util_strdup("<p class=\"empty\">Out of memory</p>");
    }
    
    int pos = 0;
    
    /* Stats cards */
    pos += snprintf(html + pos, size - (size_t)pos,
        "<div class=\"stats\">\n"
        "  <div class=\"stat-card\">\n"
        "    <h3>Running</h3>\n"
        "    <div class=\"value\">%d</div>\n"
        "  </div>\n"
        "  <div class=\"stat-card\">\n"
        "    <h3>Retry Queue</h3>\n"
        "    <div class=\"value\">%d</div>\n"
        "  </div>\n"
        "  <div class=\"stat-card\">\n"
        "    <h3>Total Tokens</h3>\n"
        "    <div class=\"value\">%llu</div>\n"
        "  </div>\n"
        "  <div class=\"stat-card\">\n"
        "    <h3>Runtime</h3>\n"
        "    <div class=\"value\">%.0fs</div>\n"
        "  </div>\n"
        "</div>\n",
        snap->running_count,
        snap->retry_count,
        (unsigned long long)snap->total_tokens,
        snap->seconds_running);
    
    /* Running sessions */
    pos += snprintf(html + pos, size - (size_t)pos,
        "<div class=\"section\">\n"
        "  <div class=\"section-header\">Running Sessions</div>\n"
        "  <div class=\"section-body\">\n");
    
    if (snap->running_count > 0) {
        pos += snprintf(html + pos, size - (size_t)pos,
            "    <table>\n"
            "      <thead>\n"
            "        <tr><th>Issue</th><th>Status</th><th>Attempt</th><th>Turns</th></tr>\n"
            "      </thead>\n"
            "      <tbody>\n");
        
        for (int i = 0; i < snap->running_count; i++) {
            running_entry_t *entry = &snap->running[i];
            pos += snprintf(html + pos, size - (size_t)pos,
                "        <tr>\n"
                "          <td><strong>%s</strong></td>\n"
                "          <td><span class=\"status-running\">●</span> Running</td>\n"
                "          <td>%d</td>\n"
                "          <td>%d</td>\n"
                "        </tr>\n",
                entry->identifier,
                entry->attempt,
                entry->session.turn_count);
        }
        
        pos += snprintf(html + pos, size - (size_t)pos,
            "      </tbody>\n"
            "    </table>\n");
    } else {
        pos += snprintf(html + pos, size - (size_t)pos,
            "    <p class=\"empty\">No running sessions</p>\n");
    }
    
    pos += snprintf(html + pos, size - (size_t)pos,
        "  </div>\n"
        "</div>\n");
    
    /* Retry queue */
    pos += snprintf(html + pos, size - (size_t)pos,
        "<div class=\"section\">\n"
        "  <div class=\"section-header\">Retry Queue</div>\n"
        "  <div class=\"section-body\">\n");
    
    if (snap->retry_count > 0) {
        pos += snprintf(html + pos, size - (size_t)pos,
            "    <table>\n"
            "      <thead>\n"
            "        <tr><th>Issue</th><th>Attempt</th><th>Error</th></tr>\n"
            "      </thead>\n"
            "      <tbody>\n");
        
        for (int i = 0; i < snap->retry_count; i++) {
            retry_entry_t *entry = &snap->retrying[i];
            pos += snprintf(html + pos, size - (size_t)pos,
                "        <tr>\n"
                "          <td><strong>%s</strong></td>\n"
                "          <td>%d</td>\n"
                "          <td>%s</td>\n"
                "        </tr>\n",
                entry->identifier,
                entry->attempt,
                entry->error[0] ? entry->error : "-");
        }
        
        pos += snprintf(html + pos, size - (size_t)pos,
            "      </tbody>\n"
            "    </table>\n");
    } else {
        pos += snprintf(html + pos, size - (size_t)pos,
            "    <p class=\"empty\">No pending retries</p>\n");
    }
    
    snprintf(html + pos, size - (size_t)pos,
        "  </div>\n"
        "</div>\n");
    
    orchestrator_snapshot_destroy(snap);
    return html;
}

/*
 * Handle HTTP request
 */
static void handle_request(http_server_t *srv, int client_fd) {
    char buffer[HTTP_BUFFER_SIZE];
    ssize_t bytes = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[bytes] = '\0';
    
    /* Parse request line */
    char method[16] = {0};
    char path[256] = {0};
    sscanf(buffer, "%15s %255s", method, path);
    
    LOG_DEBUG("HTTP %s %s", method, path);
    
    /* Route request */
    if (strcmp(method, "GET") == 0) {
        if (strcmp(path, "/") == 0) {
            /* Dashboard */
            http_send_response(client_fd, 200, "text/html; charset=utf-8",
                               DASHBOARD_HTML, strlen(DASHBOARD_HTML));
        } else if (strcmp(path, "/api/v1/state") == 0) {
            /* JSON API */
            char *json = generate_state_json(srv->orchestrator);
            http_send_response(client_fd, 200, "application/json",
                               json, strlen(json));
            free(json);
        } else if (strcmp(path, "/api/v1/state/html") == 0) {
            /* HTML fragment for HTMX */
            char *html = generate_state_html(srv->orchestrator);
            http_send_response(client_fd, 200, "text/html; charset=utf-8",
                               html, strlen(html));
            free(html);
        } else {
            /* 404 */
            const char *body = "Not Found";
            http_send_response(client_fd, 404, "text/plain", body, strlen(body));
        }
    } else {
        /* Method not allowed */
        const char *body = "Method Not Allowed";
        http_send_response(client_fd, 405, "text/plain", body, strlen(body));
    }
    
    close(client_fd);
}

/*
 * Server thread
 */
static void *server_thread(void *arg) {
    http_server_t *srv = (http_server_t *)arg;
    
    while (srv->running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(srv->server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            if (srv->running) {
                LOG_WARN("Accept failed: %s", strerror(errno));
            }
            continue;
        }
        
        handle_request(srv, client_fd);
    }
    
    return NULL;
}

/*
 * Create HTTP server
 */
http_server_t *http_server_create(symphony_orchestrator_t *orch, int port) {
    if (!orch) return NULL;
    
    http_server_t *srv = calloc(1, sizeof(http_server_t));
    if (!srv) return NULL;
    
    srv->orchestrator = orch;
    srv->port = port;
    srv->server_fd = -1;
    
    return srv;
}

/*
 * Destroy HTTP server
 */
void http_server_destroy(http_server_t *srv) {
    if (!srv) return;
    
    http_server_stop(srv);
    free(srv);
}

/*
 * Start HTTP server
 */
symphony_error_t http_server_start(http_server_t *srv) {
    if (!srv) return SYMPHONY_ERR_INTERNAL;
    
    /* Create socket */
    srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->server_fd < 0) {
        LOG_ERROR("Failed to create socket: %s", strerror(errno));
        return SYMPHONY_ERR_INTERNAL;
    }
    
    /* Allow address reuse */
    int opt = 1;
    setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    /* Bind */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  /* 127.0.0.1 only */
    addr.sin_port = htons((uint16_t)srv->port);
    
    if (bind(srv->server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        LOG_ERROR("Failed to bind port %d: %s", srv->port, strerror(errno));
        close(srv->server_fd);
        srv->server_fd = -1;
        return SYMPHONY_ERR_INTERNAL;
    }
    
    /* Get actual port if ephemeral */
    if (srv->port == 0) {
        socklen_t len = sizeof(addr);
        getsockname(srv->server_fd, (struct sockaddr *)&addr, &len);
        srv->port = ntohs(addr.sin_port);
    }
    
    /* Listen */
    if (listen(srv->server_fd, MAX_CONNECTIONS) < 0) {
        LOG_ERROR("Failed to listen: %s", strerror(errno));
        close(srv->server_fd);
        srv->server_fd = -1;
        return SYMPHONY_ERR_INTERNAL;
    }
    
    /* Start thread */
    srv->running = true;
    if (pthread_create(&srv->thread, NULL, server_thread, srv) != 0) {
        LOG_ERROR("Failed to create server thread");
        close(srv->server_fd);
        srv->server_fd = -1;
        srv->running = false;
        return SYMPHONY_ERR_INTERNAL;
    }
    
    return SYMPHONY_OK;
}

/*
 * Stop HTTP server
 */
void http_server_stop(http_server_t *srv) {
    if (!srv || !srv->running) return;
    
    srv->running = false;
    
    if (srv->server_fd >= 0) {
        shutdown(srv->server_fd, SHUT_RDWR);
        close(srv->server_fd);
        srv->server_fd = -1;
    }
    
    pthread_join(srv->thread, NULL);
}

/*
 * Check if server is running
 */
bool http_server_is_running(http_server_t *srv) {
    return srv && srv->running;
}

/*
 * Get bound port
 */
int http_server_get_port(http_server_t *srv) {
    return srv ? srv->port : 0;
}
