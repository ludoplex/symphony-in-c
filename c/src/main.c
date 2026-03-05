/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * main.c - Entry point and CLI
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <getopt.h>

#include "symphony.h"
#include "config.h"
#include "orchestrator.h"
#include "http_server.h"
#include "util.h"

/* Global orchestrator for signal handling */
static symphony_orchestrator_t *g_orchestrator = NULL;
static http_server_t *g_http_server = NULL;

/*
 * Signal handler for graceful shutdown
 */
static void signal_handler(int sig) {
    (void)sig;
    LOG_INFO("Received shutdown signal, stopping...");
    if (g_orchestrator) {
        orchestrator_stop(g_orchestrator);
    }
    if (g_http_server) {
        http_server_stop(g_http_server);
    }
}

/*
 * Print usage information
 */
static void print_usage(const char *prog) {
    fprintf(stderr, 
        "Symphony - Autonomous coding agent orchestrator\n"
        "\n"
        "Usage: %s [OPTIONS] [WORKFLOW_PATH]\n"
        "\n"
        "Arguments:\n"
        "  WORKFLOW_PATH    Path to WORKFLOW.md (default: ./WORKFLOW.md)\n"
        "\n"
        "Options:\n"
        "  -p, --port PORT  Enable HTTP server on PORT (0 for ephemeral)\n"
        "  -h, --help       Show this help message\n"
        "  -v, --version    Show version information\n"
        "\n"
        "Environment variables:\n"
        "  LINEAR_API_KEY   API key for Linear issue tracker\n"
        "\n"
        "For more information, see the README.md or SPEC.md files.\n",
        prog);
}

/*
 * Print version information
 */
static void print_version(void) {
    printf("Symphony %s\n", SYMPHONY_VERSION);
    printf("Built with Cosmopolitan libc for cross-platform portability.\n");
}

/*
 * Main entry point
 */
int main(int argc, char *argv[]) {
    const char *workflow_path = "WORKFLOW.md";
    int server_port = -1;  /* -1 = use config, 0+ = override */
    int opt;
    
    /* Command line options */
    static struct option long_options[] = {
        {"port",    required_argument, 0, 'p'},
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'v'},
        {"test",    no_argument,       0, 't'},
        {0, 0, 0, 0}
    };
    
    while ((opt = getopt_long(argc, argv, "p:hvt", long_options, NULL)) != -1) {
        switch (opt) {
            case 'p':
                server_port = atoi(optarg);
                if (server_port < 0 || server_port > 65535) {
                    fprintf(stderr, "Error: Invalid port number: %s\n", optarg);
                    return 1;
                }
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            case 'v':
                print_version();
                return 0;
            case 't':
                /* Test mode - just validate and exit */
                printf("Test mode: validating configuration...\n");
                break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }
    
    /* Get workflow path from positional argument */
    if (optind < argc) {
        workflow_path = argv[optind];
    }
    
    /* Check if workflow file exists */
    if (!util_path_exists(workflow_path)) {
        fprintf(stderr, "Error: Workflow file not found: %s\n", workflow_path);
        return 1;
    }
    
    LOG_INFO("Starting Symphony %s", SYMPHONY_VERSION);
    LOG_INFO("Workflow file: %s", workflow_path);
    
    /* Load configuration */
    symphony_config_t *cfg = config_create();
    if (!cfg) {
        LOG_ERROR("Failed to create configuration");
        return 1;
    }
    
    symphony_error_t err = config_load(cfg, workflow_path);
    if (err != SYMPHONY_OK) {
        LOG_ERROR("Failed to load configuration: %s", symphony_error_string(err));
        config_destroy(cfg);
        return 1;
    }
    
    /* Validate configuration */
    char error_buf[256];
    if (!config_validate(cfg, error_buf, sizeof(error_buf))) {
        LOG_ERROR("Configuration validation failed: %s", error_buf);
        config_destroy(cfg);
        return 1;
    }
    
    LOG_INFO("Configuration loaded successfully");
    LOG_INFO("Tracker: %s", cfg->tracker.kind);
    LOG_INFO("Project: %s", cfg->tracker.project_slug);
    LOG_INFO("Workspace root: %s", cfg->workspace.root);
    LOG_INFO("Max concurrent agents: %d", cfg->agent.max_concurrent_agents);
    
    /* Create orchestrator */
    g_orchestrator = orchestrator_create(cfg);
    if (!g_orchestrator) {
        LOG_ERROR("Failed to create orchestrator");
        config_destroy(cfg);
        return 1;
    }
    
    /* Set up signal handlers */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    /* Start HTTP server if configured */
    int actual_port = server_port >= 0 ? server_port : cfg->server.port;
    if (actual_port >= 0) {
        g_http_server = http_server_create(g_orchestrator, actual_port);
        if (g_http_server) {
            err = http_server_start(g_http_server);
            if (err == SYMPHONY_OK) {
                LOG_INFO("HTTP server started on port %d", 
                         http_server_get_port(g_http_server));
            } else {
                LOG_WARN("Failed to start HTTP server: %s", 
                         symphony_error_string(err));
            }
        }
    }
    
    /* Start orchestrator main loop */
    LOG_INFO("Starting orchestration loop...");
    err = orchestrator_start(g_orchestrator);
    
    /* Cleanup */
    LOG_INFO("Shutting down...");
    
    if (g_http_server) {
        http_server_stop(g_http_server);
        http_server_destroy(g_http_server);
        g_http_server = NULL;
    }
    
    orchestrator_destroy(g_orchestrator);
    g_orchestrator = NULL;
    
    config_destroy(cfg);
    
    LOG_INFO("Symphony stopped");
    return err == SYMPHONY_OK ? 0 : 1;
}

/*
 * Error code to string conversion
 */
const char *symphony_error_string(symphony_error_t err) {
    switch (err) {
        case SYMPHONY_OK:
            return "OK";
        case SYMPHONY_ERR_MISSING_WORKFLOW_FILE:
            return "Missing workflow file";
        case SYMPHONY_ERR_WORKFLOW_PARSE_ERROR:
            return "Workflow parse error";
        case SYMPHONY_ERR_WORKFLOW_NOT_A_MAP:
            return "Workflow front matter is not a map";
        case SYMPHONY_ERR_TEMPLATE_PARSE_ERROR:
            return "Template parse error";
        case SYMPHONY_ERR_TEMPLATE_RENDER_ERROR:
            return "Template render error";
        case SYMPHONY_ERR_MISSING_API_KEY:
            return "Missing tracker API key";
        case SYMPHONY_ERR_MISSING_PROJECT_SLUG:
            return "Missing tracker project slug";
        case SYMPHONY_ERR_UNSUPPORTED_TRACKER:
            return "Unsupported tracker kind";
        case SYMPHONY_ERR_NETWORK_ERROR:
            return "Network error";
        case SYMPHONY_ERR_WORKSPACE_ERROR:
            return "Workspace error";
        case SYMPHONY_ERR_AGENT_ERROR:
            return "Agent error";
        case SYMPHONY_ERR_INTERNAL:
            return "Internal error";
        default:
            return "Unknown error";
    }
}
