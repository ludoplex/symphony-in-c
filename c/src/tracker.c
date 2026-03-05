/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * tracker.c - Issue tracker client interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

#include "tracker.h"
#include "util.h"

/* HTTP timeout */
#define HTTP_TIMEOUT_MS 30000
#define PAGE_SIZE 50

/*
 * Tracker client structure
 */
struct tracker_client {
    symphony_config_t *config;
    CURL *curl;
    struct curl_slist *headers;
};

/*
 * cURL write callback
 */
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
} response_buffer_t;

static size_t curl_write_callback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    response_buffer_t *buf = (response_buffer_t *)userdata;
    size_t realsize = size * nmemb;
    
    /* Grow buffer if needed */
    if (buf->size + realsize + 1 > buf->capacity) {
        size_t new_capacity = buf->capacity * 2;
        if (new_capacity < buf->size + realsize + 1) {
            new_capacity = buf->size + realsize + 1;
        }
        char *new_data = realloc(buf->data, new_capacity);
        if (!new_data) return 0;
        buf->data = new_data;
        buf->capacity = new_capacity;
    }
    
    memcpy(buf->data + buf->size, ptr, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    
    return realsize;
}

/*
 * Create tracker client
 */
tracker_client_t *tracker_create(symphony_config_t *cfg) {
    if (!cfg) return NULL;
    
    tracker_client_t *client = calloc(1, sizeof(tracker_client_t));
    if (!client) return NULL;
    
    client->config = cfg;
    
    /* Initialize cURL */
    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client);
        return NULL;
    }
    
    /* Set up headers */
    char auth_header[640];
    snprintf(auth_header, sizeof(auth_header), "Authorization: %s", cfg->tracker.api_key);
    client->headers = curl_slist_append(NULL, auth_header);
    client->headers = curl_slist_append(client->headers, "Content-Type: application/json");
    
    return client;
}

/*
 * Destroy tracker client
 */
void tracker_destroy(tracker_client_t *client) {
    if (!client) return;
    
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    if (client->headers) {
        curl_slist_free_all(client->headers);
    }
    free(client);
}

/*
 * Execute GraphQL query
 */
static char *tracker_graphql(tracker_client_t *client, const char *query, const char *variables) {
    if (!client || !query) return NULL;
    
    CURL *curl = client->curl;
    const char *endpoint = client->config->tracker.endpoint;
    
    /* Build request body */
    char *escaped_query = util_json_escape(query);
    char *escaped_vars = variables ? util_json_escape(variables) : util_strdup("{}");
    
    char *body = malloc(strlen(escaped_query) + strlen(escaped_vars) + 100);
    if (!body) {
        free(escaped_query);
        free(escaped_vars);
        return NULL;
    }
    sprintf(body, "{\"query\":\"%s\",\"variables\":%s}", escaped_query, variables ? variables : "{}");
    free(escaped_query);
    free(escaped_vars);
    
    /* Set up response buffer */
    response_buffer_t response = {0};
    response.capacity = 4096;
    response.data = malloc(response.capacity);
    if (!response.data) {
        free(body);
        return NULL;
    }
    response.data[0] = '\0';
    
    /* Configure cURL */
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, endpoint);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, client->headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, HTTP_TIMEOUT_MS);
    
    /* Execute request */
    CURLcode res = curl_easy_perform(curl);
    free(body);
    
    if (res != CURLE_OK) {
        LOG_ERROR("HTTP request failed: %s", curl_easy_strerror(res));
        free(response.data);
        return NULL;
    }
    
    /* Check HTTP status */
    long http_code;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code != 200) {
        LOG_ERROR("HTTP error %ld: %s", http_code, response.data);
        free(response.data);
        return NULL;
    }
    
    return response.data;
}

/*
 * Parse issue from GraphQL response node
 * This is a simplified parser for the Linear API response format
 */
static void parse_issue_node(const char *node_json, symphony_issue_t *issue) {
    if (!node_json || !issue) return;
    
    char *val;
    
    /* Parse ID */
    val = util_json_get_string(node_json, "id");
    if (val) {
        strncpy(issue->id, val, sizeof(issue->id) - 1);
        free(val);
    }
    
    /* Parse identifier */
    val = util_json_get_string(node_json, "identifier");
    if (val) {
        strncpy(issue->identifier, val, sizeof(issue->identifier) - 1);
        free(val);
    }
    
    /* Parse title */
    val = util_json_get_string(node_json, "title");
    if (val) {
        strncpy(issue->title, val, sizeof(issue->title) - 1);
        free(val);
    }
    
    /* Parse description */
    val = util_json_get_string(node_json, "description");
    if (val) {
        issue->description = val;  /* Transfer ownership */
    }
    
    /* Parse priority */
    issue->priority = util_json_get_int(node_json, "priority", 0);
    
    /* Parse state name */
    val = util_json_get_nested(node_json, "state", "name");
    if (val) {
        strncpy(issue->state, val, sizeof(issue->state) - 1);
        free(val);
    }
    
    /* Parse branch name */
    val = util_json_get_string(node_json, "branchName");
    if (val) {
        strncpy(issue->branch_name, val, sizeof(issue->branch_name) - 1);
        free(val);
    }
    
    /* Parse URL */
    val = util_json_get_string(node_json, "url");
    if (val) {
        strncpy(issue->url, val, sizeof(issue->url) - 1);
        free(val);
    }
    
    /* Parse timestamps */
    val = util_json_get_string(node_json, "createdAt");
    if (val) {
        issue->created_at = util_parse_iso8601(val);
        free(val);
    }
    
    val = util_json_get_string(node_json, "updatedAt");
    if (val) {
        issue->updated_at = util_parse_iso8601(val);
        free(val);
    }
    
    /* Note: Labels and blockers require more complex parsing */
    /* For simplicity, we'll parse them with basic string matching */
}

/*
 * Linear GraphQL query for candidate issues
 */
static const char *CANDIDATE_ISSUES_QUERY = 
    "query($projectSlug: String!, $after: String) {"
    "  issues(first: 50, after: $after, filter: {"
    "    project: { slugId: { eq: $projectSlug } }"
    "  }) {"
    "    pageInfo { hasNextPage endCursor }"
    "    nodes {"
    "      id identifier title description priority"
    "      branchName url createdAt updatedAt"
    "      state { name }"
    "      labels { nodes { name } }"
    "      relations(first: 50) {"
    "        nodes {"
    "          type"
    "          relatedIssue { id identifier state { name } }"
    "        }"
    "      }"
    "    }"
    "  }"
    "}";

/*
 * Fetch candidate issues from Linear
 */
symphony_error_t tracker_fetch_candidates(tracker_client_t *client,
                                          symphony_issue_t **issues,
                                          int *count) {
    if (!client || !issues || !count) return SYMPHONY_ERR_INTERNAL;
    
    *issues = NULL;
    *count = 0;
    
    symphony_config_t *cfg = client->config;
    
    /* Allocate initial array */
    int capacity = 64;
    symphony_issue_t *result = calloc((size_t)capacity, sizeof(symphony_issue_t));
    if (!result) return SYMPHONY_ERR_INTERNAL;
    
    char *cursor = NULL;
    bool has_more = true;
    
    while (has_more) {
        /* Build variables */
        char variables[512];
        if (cursor) {
            snprintf(variables, sizeof(variables),
                     "{\"projectSlug\":\"%s\",\"after\":\"%s\"}",
                     cfg->tracker.project_slug, cursor);
        } else {
            snprintf(variables, sizeof(variables),
                     "{\"projectSlug\":\"%s\"}", cfg->tracker.project_slug);
        }
        
        /* Execute query */
        char *response = tracker_graphql(client, CANDIDATE_ISSUES_QUERY, variables);
        if (!response) {
            free(result);
            free(cursor);
            return SYMPHONY_ERR_NETWORK_ERROR;
        }
        
        /* Check for GraphQL errors */
        if (strstr(response, "\"errors\"")) {
            LOG_ERROR("GraphQL errors in response: %s", response);
            free(response);
            free(result);
            free(cursor);
            return SYMPHONY_ERR_NETWORK_ERROR;
        }
        
        /* Parse response - find nodes array */
        /* This is a simplified parser; a real implementation would use a JSON library */
        const char *nodes_start = strstr(response, "\"nodes\"");
        if (!nodes_start) {
            free(response);
            break;
        }
        
        /* Find array start */
        nodes_start = strchr(nodes_start, '[');
        if (!nodes_start) {
            free(response);
            break;
        }
        
        /* Parse each node (simplified - real impl would use JSON parser) */
        const char *ptr = nodes_start + 1;
        while (*ptr) {
            /* Skip whitespace */
            while (*ptr && (*ptr == ' ' || *ptr == '\n' || *ptr == '\r' || *ptr == '\t')) ptr++;
            
            if (*ptr == ']') break;
            if (*ptr == ',') { ptr++; continue; }
            if (*ptr != '{') break;
            
            /* Find matching closing brace */
            int depth = 1;
            const char *node_start = ptr;
            ptr++;
            while (*ptr && depth > 0) {
                if (*ptr == '{') depth++;
                else if (*ptr == '}') depth--;
                else if (*ptr == '"') {
                    ptr++;
                    while (*ptr && *ptr != '"') {
                        if (*ptr == '\\' && *(ptr + 1)) ptr++;
                        ptr++;
                    }
                }
                ptr++;
            }
            
            /* Extract node JSON */
            size_t node_len = (size_t)(ptr - node_start);
            char *node_json = util_strndup(node_start, node_len);
            
            if (node_json) {
                /* Grow array if needed */
                if (*count >= capacity) {
                    capacity *= 2;
                    symphony_issue_t *new_result = realloc(result, 
                        (size_t)capacity * sizeof(symphony_issue_t));
                    if (!new_result) {
                        free(node_json);
                        free(response);
                        free(result);
                        free(cursor);
                        return SYMPHONY_ERR_INTERNAL;
                    }
                    result = new_result;
                    memset(&result[*count], 0, 
                           (size_t)(capacity - *count) * sizeof(symphony_issue_t));
                }
                
                /* Parse and filter issue */
                symphony_issue_t *issue = &result[*count];
                parse_issue_node(node_json, issue);
                
                /* Only include issues in active states */
                if (issue->id[0] && config_is_active_state(cfg, issue->state)) {
                    (*count)++;
                } else if (issue->description) {
                    /* Not active, clean up description */
                    free(issue->description);
                    issue->description = NULL;
                }
                
                free(node_json);
            }
        }
        
        /* Check for more pages */
        free(cursor);
        cursor = NULL;
        
        const char *has_next = strstr(response, "\"hasNextPage\"");
        if (has_next && strstr(has_next, "true")) {
            char *end_cursor = util_json_get_string(response, "endCursor");
            if (end_cursor && end_cursor[0]) {
                cursor = end_cursor;
            } else {
                free(end_cursor);
                has_more = false;
            }
        } else {
            has_more = false;
        }
        
        free(response);
    }
    
    free(cursor);
    
    /* Sort issues */
    if (*count > 0) {
        issue_sort(result, *count);
    }
    
    *issues = result;
    return SYMPHONY_OK;
}

/*
 * Linear GraphQL query for issues by states
 */
static const char *ISSUES_BY_STATES_QUERY =
    "query($projectSlug: String!, $states: [String!]!) {"
    "  issues(first: 100, filter: {"
    "    project: { slugId: { eq: $projectSlug } }"
    "    state: { name: { in: $states } }"
    "  }) {"
    "    nodes { id identifier state { name } }"
    "  }"
    "}";

/*
 * Fetch issues by specific states
 */
symphony_error_t tracker_fetch_by_states(tracker_client_t *client,
                                         const char *states[],
                                         int state_count,
                                         symphony_issue_t **issues,
                                         int *count) {
    if (!client || !issues || !count) return SYMPHONY_ERR_INTERNAL;
    
    *issues = NULL;
    *count = 0;
    
    if (state_count == 0) return SYMPHONY_OK;
    
    /* Build states array JSON */
    char states_json[1024] = "[";
    for (int i = 0; i < state_count; i++) {
        if (i > 0) strcat(states_json, ",");
        strcat(states_json, "\"");
        strncat(states_json, states[i], 100);
        strcat(states_json, "\"");
    }
    strcat(states_json, "]");
    
    char variables[1536];
    snprintf(variables, sizeof(variables),
             "{\"projectSlug\":\"%s\",\"states\":%s}",
             client->config->tracker.project_slug, states_json);
    
    char *response = tracker_graphql(client, ISSUES_BY_STATES_QUERY, variables);
    if (!response) return SYMPHONY_ERR_NETWORK_ERROR;
    
    /* Allocate result array */
    symphony_issue_t *result = calloc(100, sizeof(symphony_issue_t));
    if (!result) {
        free(response);
        return SYMPHONY_ERR_INTERNAL;
    }
    
    /* Parse nodes (simplified) */
    const char *nodes_start = strstr(response, "\"nodes\"");
    if (nodes_start) {
        nodes_start = strchr(nodes_start, '[');
        if (nodes_start) {
            const char *ptr = nodes_start + 1;
            while (*ptr && *count < 100) {
                while (*ptr && (*ptr == ' ' || *ptr == '\n' || *ptr == ',' || *ptr == '\r')) ptr++;
                if (*ptr == ']') break;
                if (*ptr != '{') break;
                
                int depth = 1;
                const char *node_start = ptr++;
                while (*ptr && depth > 0) {
                    if (*ptr == '{') depth++;
                    else if (*ptr == '}') depth--;
                    else if (*ptr == '"') {
                        ptr++;
                        while (*ptr && *ptr != '"') {
                            if (*ptr == '\\' && *(ptr + 1)) ptr++;
                            ptr++;
                        }
                    }
                    ptr++;
                }
                
                char *node_json = util_strndup(node_start, (size_t)(ptr - node_start));
                if (node_json) {
                    parse_issue_node(node_json, &result[*count]);
                    if (result[*count].id[0]) (*count)++;
                    free(node_json);
                }
            }
        }
    }
    
    free(response);
    *issues = result;
    return SYMPHONY_OK;
}

/*
 * Linear GraphQL query for issue states by IDs
 */
static const char *ISSUE_STATES_BY_IDS_QUERY =
    "query($ids: [ID!]!) {"
    "  nodes(ids: $ids) {"
    "    ... on Issue {"
    "      id identifier state { name }"
    "    }"
    "  }"
    "}";

/*
 * Fetch current state for specific issue IDs
 */
symphony_error_t tracker_fetch_states_by_ids(tracker_client_t *client,
                                             const char *ids[],
                                             int id_count,
                                             symphony_issue_t **issues,
                                             int *count) {
    if (!client || !issues || !count) return SYMPHONY_ERR_INTERNAL;
    
    *issues = NULL;
    *count = 0;
    
    if (id_count == 0) return SYMPHONY_OK;
    
    /* Build IDs array JSON */
    size_t ids_json_size = 2 + (size_t)id_count * 260;
    char *ids_json = malloc(ids_json_size);
    if (!ids_json) return SYMPHONY_ERR_INTERNAL;
    
    strcpy(ids_json, "[");
    for (int i = 0; i < id_count; i++) {
        if (i > 0) strcat(ids_json, ",");
        strcat(ids_json, "\"");
        strncat(ids_json, ids[i], 256);
        strcat(ids_json, "\"");
    }
    strcat(ids_json, "]");
    
    char *variables = malloc(ids_json_size + 32);
    if (!variables) {
        free(ids_json);
        return SYMPHONY_ERR_INTERNAL;
    }
    sprintf(variables, "{\"ids\":%s}", ids_json);
    free(ids_json);
    
    char *response = tracker_graphql(client, ISSUE_STATES_BY_IDS_QUERY, variables);
    free(variables);
    
    if (!response) return SYMPHONY_ERR_NETWORK_ERROR;
    
    /* Allocate result array */
    symphony_issue_t *result = calloc((size_t)id_count, sizeof(symphony_issue_t));
    if (!result) {
        free(response);
        return SYMPHONY_ERR_INTERNAL;
    }
    
    /* Parse nodes (simplified) */
    const char *nodes_start = strstr(response, "\"nodes\"");
    if (nodes_start) {
        nodes_start = strchr(nodes_start, '[');
        if (nodes_start) {
            const char *ptr = nodes_start + 1;
            while (*ptr && *count < id_count) {
                while (*ptr && (*ptr == ' ' || *ptr == '\n' || *ptr == ',' || *ptr == '\r')) ptr++;
                if (*ptr == ']') break;
                
                /* Skip null nodes */
                if (strncmp(ptr, "null", 4) == 0) {
                    ptr += 4;
                    continue;
                }
                
                if (*ptr != '{') break;
                
                int depth = 1;
                const char *node_start = ptr++;
                while (*ptr && depth > 0) {
                    if (*ptr == '{') depth++;
                    else if (*ptr == '}') depth--;
                    else if (*ptr == '"') {
                        ptr++;
                        while (*ptr && *ptr != '"') {
                            if (*ptr == '\\' && *(ptr + 1)) ptr++;
                            ptr++;
                        }
                    }
                    ptr++;
                }
                
                char *node_json = util_strndup(node_start, (size_t)(ptr - node_start));
                if (node_json) {
                    parse_issue_node(node_json, &result[*count]);
                    if (result[*count].id[0]) (*count)++;
                    free(node_json);
                }
            }
        }
    }
    
    free(response);
    *issues = result;
    return SYMPHONY_OK;
}

/*
 * Issue utilities
 */
symphony_issue_t *issue_create(void) {
    return calloc(1, sizeof(symphony_issue_t));
}

void issue_destroy(symphony_issue_t *issue) {
    if (!issue) return;
    free(issue->description);
    free(issue);
}

void issue_list_destroy(symphony_issue_t *issues, int count) {
    if (!issues) return;
    for (int i = 0; i < count; i++) {
        free(issues[i].description);
    }
    free(issues);
}

symphony_issue_t *issue_clone(symphony_issue_t *issue) {
    if (!issue) return NULL;
    
    symphony_issue_t *clone = calloc(1, sizeof(symphony_issue_t));
    if (!clone) return NULL;
    
    *clone = *issue;
    if (issue->description) {
        clone->description = util_strdup(issue->description);
    }
    
    return clone;
}

/*
 * Issue comparison for sorting
 * Order: priority (1-4, null last), created_at (oldest first), identifier (alpha)
 */
int issue_compare(const void *a, const void *b) {
    const symphony_issue_t *ia = (const symphony_issue_t *)a;
    const symphony_issue_t *ib = (const symphony_issue_t *)b;
    
    /* Priority: lower is better, 0 (null) goes last */
    int pa = ia->priority > 0 ? ia->priority : 100;
    int pb = ib->priority > 0 ? ib->priority : 100;
    if (pa != pb) return pa - pb;
    
    /* Created at: older first */
    if (ia->created_at != ib->created_at) {
        return ia->created_at < ib->created_at ? -1 : 1;
    }
    
    /* Identifier: alphabetical */
    return strcmp(ia->identifier, ib->identifier);
}

void issue_sort(symphony_issue_t *issues, int count) {
    if (!issues || count <= 1) return;
    qsort(issues, (size_t)count, sizeof(symphony_issue_t), issue_compare);
}

/*
 * Check if issue has non-terminal blockers
 */
bool issue_has_nonterminal_blockers(symphony_issue_t *issue, symphony_config_t *cfg) {
    if (!issue || !cfg) return false;
    
    for (int i = 0; i < issue->blocker_count; i++) {
        const char *blocker_state = issue->blocked_by[i].identifier;  /* Using identifier as state holder */
        if (blocker_state[0] && !config_is_terminal_state(cfg, blocker_state)) {
            return true;
        }
    }
    
    return false;
}
