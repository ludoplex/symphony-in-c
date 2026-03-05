/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * template.c - Template rendering using a simple Liquid-like syntax
 *
 * This is a simplified template engine that handles basic variable
 * substitution in the format {{ variable }} or {{ object.property }}.
 * For a full Lua-based implementation, link against luajit.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "template.h"
#include "util.h"

/*
 * Template engine structure
 */
struct template_engine {
    char error[256];
};

/*
 * Create template engine
 */
template_engine_t *template_create(void) {
    template_engine_t *eng = calloc(1, sizeof(template_engine_t));
    return eng;
}

/*
 * Destroy template engine
 */
void template_destroy(template_engine_t *eng) {
    free(eng);
}

/*
 * Get value for a variable path like "issue.title" or "attempt"
 */
static char *get_variable_value(symphony_issue_t *issue, int attempt, const char *path) {
    if (!path) return NULL;
    
    /* Handle attempt */
    if (strcmp(path, "attempt") == 0) {
        if (attempt == 0) return NULL;  /* nil for first run */
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", attempt);
        return util_strdup(buf);
    }
    
    /* Handle issue.* */
    if (strncmp(path, "issue.", 6) == 0) {
        const char *field = path + 6;
        
        if (strcmp(field, "id") == 0) {
            return util_strdup(issue->id);
        }
        if (strcmp(field, "identifier") == 0) {
            return util_strdup(issue->identifier);
        }
        if (strcmp(field, "title") == 0) {
            return util_strdup(issue->title);
        }
        if (strcmp(field, "description") == 0) {
            return util_strdup(issue->description ? issue->description : "");
        }
        if (strcmp(field, "state") == 0) {
            return util_strdup(issue->state);
        }
        if (strcmp(field, "url") == 0) {
            return util_strdup(issue->url);
        }
        if (strcmp(field, "branch_name") == 0) {
            return util_strdup(issue->branch_name);
        }
        if (strcmp(field, "priority") == 0) {
            if (issue->priority == 0) return NULL;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", issue->priority);
            return util_strdup(buf);
        }
        if (strcmp(field, "labels") == 0) {
            /* Build comma-separated list */
            size_t size = 1;
            for (int i = 0; i < issue->label_count; i++) {
                size += strlen(issue->labels[i]) + 2;
            }
            char *result = malloc(size);
            if (!result) return NULL;
            result[0] = '\0';
            for (int i = 0; i < issue->label_count; i++) {
                if (i > 0) strcat(result, ", ");
                strcat(result, issue->labels[i]);
            }
            return result;
        }
        if (strcmp(field, "created_at") == 0) {
            return util_format_iso8601(issue->created_at);
        }
        if (strcmp(field, "updated_at") == 0) {
            return util_format_iso8601(issue->updated_at);
        }
        
        /* Unknown field */
        return NULL;
    }
    
    /* Unknown variable */
    return NULL;
}

/*
 * Render a prompt template with issue data
 *
 * Supports:
 *   {{ variable }} - substitute variable value
 *   {{ issue.field }} - substitute issue field
 *
 * Unknown variables cause an error (strict mode).
 */
symphony_error_t template_render(template_engine_t *eng,
                                  const char *template_str,
                                  symphony_issue_t *issue,
                                  int attempt,
                                  char **output) {
    if (!eng || !template_str || !issue || !output) {
        return SYMPHONY_ERR_INTERNAL;
    }
    
    *output = NULL;
    
    /* Allocate output buffer (start with 2x template size) */
    size_t template_len = strlen(template_str);
    size_t out_capacity = template_len * 2 + 1024;
    char *out = malloc(out_capacity);
    if (!out) return SYMPHONY_ERR_INTERNAL;
    
    size_t out_len = 0;
    const char *ptr = template_str;
    
    while (*ptr) {
        /* Look for {{ */
        if (ptr[0] == '{' && ptr[1] == '{') {
            ptr += 2;
            
            /* Skip whitespace */
            while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
            
            /* Find variable name */
            const char *var_start = ptr;
            while (*ptr && *ptr != '}' && *ptr != ' ' && *ptr != '\t') ptr++;
            
            size_t var_len = (size_t)(ptr - var_start);
            char *var_name = util_strndup(var_start, var_len);
            
            /* Skip whitespace and closing }} */
            while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
            if (ptr[0] == '}' && ptr[1] == '}') {
                ptr += 2;
            } else {
                free(var_name);
                free(out);
                snprintf(eng->error, sizeof(eng->error), "Unclosed template expression");
                return SYMPHONY_ERR_TEMPLATE_PARSE_ERROR;
            }
            
            /* Get variable value */
            char *value = get_variable_value(issue, attempt, var_name);
            if (!value) {
                /* Unknown variable in strict mode */
                snprintf(eng->error, sizeof(eng->error), 
                         "Unknown template variable: %s", var_name);
                free(var_name);
                free(out);
                return SYMPHONY_ERR_TEMPLATE_RENDER_ERROR;
            }
            
            /* Append value */
            size_t value_len = strlen(value);
            if (out_len + value_len + 1 > out_capacity) {
                out_capacity = out_capacity * 2 + value_len;
                char *new_out = realloc(out, out_capacity);
                if (!new_out) {
                    free(var_name);
                    free(value);
                    free(out);
                    return SYMPHONY_ERR_INTERNAL;
                }
                out = new_out;
            }
            
            memcpy(out + out_len, value, value_len);
            out_len += value_len;
            
            free(var_name);
            free(value);
        } else {
            /* Copy literal character */
            if (out_len + 2 > out_capacity) {
                out_capacity *= 2;
                char *new_out = realloc(out, out_capacity);
                if (!new_out) {
                    free(out);
                    return SYMPHONY_ERR_INTERNAL;
                }
                out = new_out;
            }
            out[out_len++] = *ptr++;
        }
    }
    
    out[out_len] = '\0';
    *output = out;
    
    return SYMPHONY_OK;
}

/*
 * Validate template syntax without rendering
 */
symphony_error_t template_validate(template_engine_t *eng,
                                    const char *template_str,
                                    char *error_buf,
                                    size_t error_buf_size) {
    if (!eng || !template_str) {
        return SYMPHONY_ERR_INTERNAL;
    }
    
    const char *ptr = template_str;
    int depth = 0;
    
    while (*ptr) {
        if (ptr[0] == '{' && ptr[1] == '{') {
            depth++;
            ptr += 2;
            
            /* Skip to }} */
            bool found_close = false;
            while (*ptr) {
                if (ptr[0] == '}' && ptr[1] == '}') {
                    depth--;
                    ptr += 2;
                    found_close = true;
                    break;
                }
                ptr++;
            }
            
            if (!found_close) {
                if (error_buf && error_buf_size > 0) {
                    snprintf(error_buf, error_buf_size, "Unclosed {{ expression");
                }
                return SYMPHONY_ERR_TEMPLATE_PARSE_ERROR;
            }
        } else {
            ptr++;
        }
    }
    
    return SYMPHONY_OK;
}
