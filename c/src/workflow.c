/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * workflow.c - WORKFLOW.md parsing
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "workflow.h"
#include "util.h"

/*
 * Create workflow definition
 */
workflow_def_t *workflow_create(void) {
    workflow_def_t *wf = calloc(1, sizeof(workflow_def_t));
    return wf;
}

/*
 * Destroy workflow definition
 */
void workflow_destroy(workflow_def_t *wf) {
    if (!wf) return;
    free(wf->raw_content);
    free(wf->yaml_front_matter);
    free(wf->prompt_body);
    free(wf);
}

/*
 * Parse workflow from file
 */
symphony_error_t workflow_parse_file(workflow_def_t *wf, const char *path) {
    if (!wf || !path) return SYMPHONY_ERR_INTERNAL;
    
    size_t size;
    char *content = util_read_file(path, &size);
    if (!content) {
        return SYMPHONY_ERR_MISSING_WORKFLOW_FILE;
    }
    
    symphony_error_t err = workflow_parse_string(wf, content);
    free(content);
    return err;
}

/*
 * Parse workflow from string content
 */
symphony_error_t workflow_parse_string(workflow_def_t *wf, const char *content) {
    if (!wf || !content) return SYMPHONY_ERR_INTERNAL;
    
    /* Clear previous content */
    free(wf->raw_content);
    free(wf->yaml_front_matter);
    free(wf->prompt_body);
    wf->raw_content = NULL;
    wf->yaml_front_matter = NULL;
    wf->prompt_body = NULL;
    
    wf->raw_content = util_strdup(content);
    if (!wf->raw_content) return SYMPHONY_ERR_INTERNAL;
    
    const char *ptr = content;
    
    /* Check for YAML front matter (starts with ---) */
    if (strncmp(ptr, "---", 3) == 0) {
        ptr += 3;
        /* Skip newline */
        while (*ptr && (*ptr == '\r' || *ptr == '\n')) ptr++;
        
        /* Find closing --- */
        const char *end = strstr(ptr, "\n---");
        if (!end) {
            end = strstr(ptr, "\r\n---");
        }
        
        if (end) {
            /* Extract YAML front matter */
            size_t yaml_len = (size_t)(end - ptr);
            wf->yaml_front_matter = util_strndup(ptr, yaml_len);
            
            /* Move past closing --- */
            ptr = end;
            while (*ptr && *ptr != '-') ptr++;
            if (strncmp(ptr, "---", 3) == 0) ptr += 3;
            while (*ptr && (*ptr == '\r' || *ptr == '\n')) ptr++;
        } else {
            /* No closing ---, treat everything as front matter error */
            return SYMPHONY_ERR_WORKFLOW_PARSE_ERROR;
        }
    }
    
    /* Rest is the prompt body */
    wf->prompt_body = util_strdup(ptr);
    if (wf->prompt_body) {
        util_str_trim(wf->prompt_body);
    }
    
    return SYMPHONY_OK;
}

/*
 * Simple YAML value extraction - handles basic key: value pairs
 * This is a simplified parser for the specific YAML subset used in WORKFLOW.md
 */
static const char *yaml_find_key(const char *yaml, const char *key) {
    if (!yaml || !key) return NULL;
    
    size_t key_len = strlen(key);
    const char *ptr = yaml;
    
    while (*ptr) {
        /* Skip leading whitespace on line */
        const char *line_start = ptr;
        while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
        
        /* Check if this line starts with key: */
        if (strncmp(ptr, key, key_len) == 0 && ptr[key_len] == ':') {
            return ptr + key_len + 1;
        }
        
        /* Skip to next line */
        while (*ptr && *ptr != '\n') ptr++;
        if (*ptr == '\n') ptr++;
        
        /* Avoid infinite loop */
        if (ptr == line_start && *ptr) ptr++;
    }
    
    return NULL;
}

static char *yaml_extract_value(const char *start) {
    if (!start) return NULL;
    
    /* Skip whitespace after colon */
    while (*start && (*start == ' ' || *start == '\t')) start++;
    
    /* Handle quoted strings */
    if (*start == '"') {
        start++;
        const char *end = start;
        while (*end && *end != '"') {
            if (*end == '\\' && *(end + 1)) end++;
            end++;
        }
        return util_strndup(start, (size_t)(end - start));
    }
    
    if (*start == '\'') {
        start++;
        const char *end = start;
        while (*end && *end != '\'') end++;
        return util_strndup(start, (size_t)(end - start));
    }
    
    /* Unquoted value - read until newline or comment */
    const char *end = start;
    while (*end && *end != '\n' && *end != '\r' && *end != '#') end++;
    
    /* Trim trailing whitespace */
    while (end > start && (*(end - 1) == ' ' || *(end - 1) == '\t')) end--;
    
    if (end == start) return NULL;
    return util_strndup(start, (size_t)(end - start));
}

/*
 * Get string value from YAML
 */
symphony_error_t workflow_get_string(workflow_def_t *wf, const char *key, 
                                      char *out, size_t out_size) {
    if (!wf || !wf->yaml_front_matter || !key || !out) {
        return SYMPHONY_ERR_WORKFLOW_PARSE_ERROR;
    }
    
    const char *val_start = yaml_find_key(wf->yaml_front_matter, key);
    if (!val_start) {
        out[0] = '\0';
        return SYMPHONY_OK;  /* Key not found is not an error */
    }
    
    char *value = yaml_extract_value(val_start);
    if (value) {
        strncpy(out, value, out_size - 1);
        out[out_size - 1] = '\0';
        free(value);
    } else {
        out[0] = '\0';
    }
    
    return SYMPHONY_OK;
}

/*
 * Get integer value from YAML
 */
symphony_error_t workflow_get_int(workflow_def_t *wf, const char *key, int *out) {
    if (!wf || !wf->yaml_front_matter || !key || !out) {
        return SYMPHONY_ERR_WORKFLOW_PARSE_ERROR;
    }
    
    const char *val_start = yaml_find_key(wf->yaml_front_matter, key);
    if (!val_start) {
        return SYMPHONY_OK;  /* Key not found is not an error */
    }
    
    char *value = yaml_extract_value(val_start);
    if (value) {
        *out = atoi(value);
        free(value);
    }
    
    return SYMPHONY_OK;
}

/*
 * Get string list from YAML (comma-separated or YAML array)
 */
symphony_error_t workflow_get_string_list(workflow_def_t *wf, const char *key,
                                           char out[][MAX_STATE_LEN], int *count, 
                                           int max_count) {
    if (!wf || !wf->yaml_front_matter || !key || !out || !count) {
        return SYMPHONY_ERR_WORKFLOW_PARSE_ERROR;
    }
    
    *count = 0;
    
    const char *val_start = yaml_find_key(wf->yaml_front_matter, key);
    if (!val_start) {
        return SYMPHONY_OK;  /* Key not found is not an error */
    }
    
    /* Skip whitespace */
    while (*val_start && (*val_start == ' ' || *val_start == '\t')) val_start++;
    
    /* Check if it's a YAML array (starts with newline then - items) */
    if (*val_start == '\n' || *val_start == '\r') {
        /* YAML array format */
        const char *ptr = val_start;
        while (*ptr) {
            /* Skip to next line */
            while (*ptr && (*ptr == '\n' || *ptr == '\r')) ptr++;
            
            /* Skip whitespace */
            while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
            
            /* Check for array item */
            if (*ptr != '-') break;
            ptr++;
            
            /* Skip whitespace after - */
            while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
            
            /* Extract value */
            const char *end = ptr;
            while (*end && *end != '\n' && *end != '\r' && *end != '#') end++;
            while (end > ptr && (*(end - 1) == ' ' || *(end - 1) == '\t')) end--;
            
            if (*count < max_count && end > ptr) {
                size_t len = (size_t)(end - ptr);
                if (len >= MAX_STATE_LEN) len = MAX_STATE_LEN - 1;
                memcpy(out[*count], ptr, len);
                out[*count][len] = '\0';
                (*count)++;
            }
            
            ptr = end;
        }
    } else {
        /* Comma-separated format */
        char *value = yaml_extract_value(val_start);
        if (value) {
            char *ptr = value;
            while (*ptr && *count < max_count) {
                /* Skip whitespace */
                while (*ptr && (*ptr == ' ' || *ptr == '\t' || *ptr == ',')) ptr++;
                if (!*ptr) break;
                
                /* Find end of item */
                const char *end = ptr;
                while (*end && *end != ',') end++;
                
                /* Trim trailing whitespace */
                const char *trim_end = end;
                while (trim_end > ptr && (*(trim_end - 1) == ' ' || *(trim_end - 1) == '\t')) {
                    trim_end--;
                }
                
                if (trim_end > ptr) {
                    size_t len = (size_t)(trim_end - ptr);
                    if (len >= MAX_STATE_LEN) len = MAX_STATE_LEN - 1;
                    memcpy(out[*count], ptr, len);
                    out[*count][len] = '\0';
                    (*count)++;
                }
                
                ptr = (char *)end;
            }
            free(value);
        }
    }
    
    return SYMPHONY_OK;
}

/*
 * Get nested string value (parent.key format)
 */
symphony_error_t workflow_get_nested_string(workflow_def_t *wf, const char *parent,
                                             const char *key, char *out, size_t out_size) {
    if (!wf || !wf->yaml_front_matter || !parent || !key || !out) {
        return SYMPHONY_ERR_WORKFLOW_PARSE_ERROR;
    }
    
    out[0] = '\0';
    
    /* Find parent section */
    const char *parent_start = yaml_find_key(wf->yaml_front_matter, parent);
    if (!parent_start) return SYMPHONY_OK;
    
    /* Skip to next line */
    while (*parent_start && *parent_start != '\n') parent_start++;
    if (*parent_start == '\n') parent_start++;
    
    /* Find the key within the indented section */
    const char *ptr = parent_start;
    while (*ptr) {
        /* Check indentation */
        if (*ptr != ' ' && *ptr != '\t') break;  /* No longer indented */
        
        const char *line_start = ptr;
        while (*ptr && (*ptr == ' ' || *ptr == '\t')) ptr++;
        
        /* No indentation means we're out of the section */
        if (ptr == line_start) break;
        
        size_t key_len = strlen(key);
        if (strncmp(ptr, key, key_len) == 0 && ptr[key_len] == ':') {
            char *value = yaml_extract_value(ptr + key_len + 1);
            if (value) {
                strncpy(out, value, out_size - 1);
                out[out_size - 1] = '\0';
                free(value);
            }
            return SYMPHONY_OK;
        }
        
        /* Skip to next line */
        while (*ptr && *ptr != '\n') ptr++;
        if (*ptr == '\n') ptr++;
    }
    
    return SYMPHONY_OK;
}

/*
 * Get nested integer value
 */
symphony_error_t workflow_get_nested_int(workflow_def_t *wf, const char *parent,
                                          const char *key, int *out) {
    if (!wf || !out) return SYMPHONY_ERR_WORKFLOW_PARSE_ERROR;
    
    char buf[64];
    symphony_error_t err = workflow_get_nested_string(wf, parent, key, buf, sizeof(buf));
    if (err != SYMPHONY_OK) return err;
    
    if (buf[0]) {
        *out = atoi(buf);
    }
    
    return SYMPHONY_OK;
}
