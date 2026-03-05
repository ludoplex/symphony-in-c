/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * util.c - Utility functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

#include "util.h"

/* ========== String Utilities ========== */

char *util_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

char *util_strndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len > n) len = n;
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len);
        dup[len] = '\0';
    }
    return dup;
}

void util_str_tolower(char *s) {
    if (!s) return;
    for (; *s; s++) {
        *s = (char)tolower((unsigned char)*s);
    }
}

void util_str_trim(char *s) {
    if (!s) return;
    
    /* Trim leading whitespace */
    char *start = s;
    while (*start && isspace((unsigned char)*start)) start++;
    
    /* Trim trailing whitespace */
    char *end = s + strlen(s);
    while (end > start && isspace((unsigned char)*(end - 1))) end--;
    
    /* Shift and terminate */
    size_t len = (size_t)(end - start);
    if (start != s) {
        memmove(s, start, len);
    }
    s[len] = '\0';
}

bool util_str_eq_nocase(const char *a, const char *b) {
    if (!a || !b) return a == b;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++;
        b++;
    }
    return *a == *b;
}

bool util_str_starts_with(const char *s, const char *prefix) {
    if (!s || !prefix) return false;
    size_t prefix_len = strlen(prefix);
    return strncmp(s, prefix, prefix_len) == 0;
}

bool util_str_ends_with(const char *s, const char *suffix) {
    if (!s || !suffix) return false;
    size_t s_len = strlen(s);
    size_t suffix_len = strlen(suffix);
    if (suffix_len > s_len) return false;
    return strcmp(s + s_len - suffix_len, suffix) == 0;
}

/* ========== Path Utilities ========== */

char *util_path_join(const char *base, const char *name) {
    if (!base || !name) return NULL;
    
    size_t base_len = strlen(base);
    size_t name_len = strlen(name);
    
    /* Remove trailing slash from base */
    while (base_len > 0 && base[base_len - 1] == '/') base_len--;
    
    /* Remove leading slash from name */
    while (name_len > 0 && *name == '/') {
        name++;
        name_len--;
    }
    
    char *result = malloc(base_len + 1 + name_len + 1);
    if (result) {
        memcpy(result, base, base_len);
        result[base_len] = '/';
        memcpy(result + base_len + 1, name, name_len);
        result[base_len + 1 + name_len] = '\0';
    }
    return result;
}

char *util_path_expand_home(const char *path) {
    if (!path) return NULL;
    if (path[0] != '~') return util_strdup(path);
    
    const char *home = getenv("HOME");
    if (!home) {
        home = "/tmp";  /* Fallback */
    }
    
    if (path[1] == '\0' || path[1] == '/') {
        return util_path_join(home, path + 1);
    }
    
    /* ~user expansion not supported, return as-is */
    return util_strdup(path);
}

char *util_path_normalize(const char *path) {
    if (!path) return NULL;
    
    /* First expand home */
    char *expanded = util_path_expand_home(path);
    if (!expanded) return NULL;
    
    /* Get absolute path */
    char resolved[4096];
    if (realpath(expanded, resolved)) {
        free(expanded);
        return util_strdup(resolved);
    }
    
    /* If realpath fails, return the expanded path */
    return expanded;
}

bool util_path_exists(const char *path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

bool util_path_is_dir(const char *path) {
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

bool util_path_is_under(const char *child, const char *parent) {
    if (!child || !parent) return false;
    
    char *child_norm = util_path_normalize(child);
    char *parent_norm = util_path_normalize(parent);
    
    if (!child_norm || !parent_norm) {
        free(child_norm);
        free(parent_norm);
        return false;
    }
    
    size_t parent_len = strlen(parent_norm);
    bool result = strncmp(child_norm, parent_norm, parent_len) == 0 &&
                  (child_norm[parent_len] == '/' || child_norm[parent_len] == '\0');
    
    free(child_norm);
    free(parent_norm);
    return result;
}

/* ========== File Utilities ========== */

char *util_read_file(const char *path, size_t *size) {
    if (!path) return NULL;
    
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (fsize < 0) {
        fclose(f);
        return NULL;
    }
    
    char *content = malloc((size_t)fsize + 1);
    if (!content) {
        fclose(f);
        return NULL;
    }
    
    size_t read_size = fread(content, 1, (size_t)fsize, f);
    fclose(f);
    
    content[read_size] = '\0';
    if (size) *size = read_size;
    
    return content;
}

bool util_write_file(const char *path, const char *content, size_t size) {
    if (!path || !content) return false;
    
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    
    size_t written = fwrite(content, 1, size, f);
    fclose(f);
    
    return written == size;
}

bool util_mkdir_p(const char *path) {
    if (!path) return false;
    
    char *dup = util_strdup(path);
    if (!dup) return false;
    
    char *p = dup;
    while (*p) {
        if (*p == '/' && p != dup) {
            *p = '\0';
            if (!util_path_exists(dup)) {
                if (mkdir(dup, 0755) != 0 && errno != EEXIST) {
                    free(dup);
                    return false;
                }
            }
            *p = '/';
        }
        p++;
    }
    
    bool result = true;
    if (!util_path_exists(dup)) {
        result = mkdir(dup, 0755) == 0 || errno == EEXIST;
    }
    
    free(dup);
    return result;
}

static bool rm_rf_helper(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) {
        return errno == ENOENT;
    }
    
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) return false;
        
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || 
                strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            char *child = util_path_join(path, entry->d_name);
            if (child) {
                rm_rf_helper(child);
                free(child);
            }
        }
        closedir(dir);
        return rmdir(path) == 0;
    } else {
        return unlink(path) == 0;
    }
}

bool util_rm_rf(const char *path) {
    if (!path) return false;
    return rm_rf_helper(path);
}

time_t util_file_mtime(const char *path) {
    if (!path) return 0;
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

/* ========== JSON Utilities (Simple) ========== */

char *util_json_escape(const char *s) {
    if (!s) return util_strdup("null");
    
    /* Count escaped characters */
    size_t len = 0;
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"': case '\\': case '\b': case '\f':
            case '\n': case '\r': case '\t':
                len += 2;
                break;
            default:
                len += ((unsigned char)*p < 0x20) ? 6 : 1;
                break;
        }
    }
    
    char *result = malloc(len + 1);
    if (!result) return NULL;
    
    char *out = result;
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  *out++ = '\\'; *out++ = '"';  break;
            case '\\': *out++ = '\\'; *out++ = '\\'; break;
            case '\b': *out++ = '\\'; *out++ = 'b';  break;
            case '\f': *out++ = '\\'; *out++ = 'f';  break;
            case '\n': *out++ = '\\'; *out++ = 'n';  break;
            case '\r': *out++ = '\\'; *out++ = 'r';  break;
            case '\t': *out++ = '\\'; *out++ = 't';  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    out += sprintf(out, "\\u%04x", (unsigned char)*p);
                } else {
                    *out++ = *p;
                }
                break;
        }
    }
    *out = '\0';
    return result;
}

/* Simple JSON string extraction - finds "key": "value" */
char *util_json_get_string(const char *json, const char *key) {
    if (!json || !key) return NULL;
    
    /* Build search pattern: "key": " */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *found = strstr(json, pattern);
    if (!found) return NULL;
    
    /* Find the colon */
    found += strlen(pattern);
    while (*found && (*found == ' ' || *found == ':' || *found == '\t')) {
        found++;
    }
    
    if (*found != '"') return NULL;
    found++;
    
    /* Find end of string */
    const char *end = found;
    while (*end && *end != '"') {
        if (*end == '\\' && *(end + 1)) end++;
        end++;
    }
    
    return util_strndup(found, (size_t)(end - found));
}

int util_json_get_int(const char *json, const char *key, int default_val) {
    if (!json || !key) return default_val;
    
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    
    const char *found = strstr(json, pattern);
    if (!found) return default_val;
    
    found += strlen(pattern);
    while (*found && (*found == ' ' || *found == ':' || *found == '\t')) {
        found++;
    }
    
    if (!isdigit((unsigned char)*found) && *found != '-') return default_val;
    
    return atoi(found);
}

char *util_json_get_nested(const char *json, const char *parent, const char *key) {
    if (!json || !parent || !key) return NULL;
    
    /* Find parent object */
    char pattern[256];
    snprintf(pattern, sizeof(pattern), "\"%s\"", parent);
    
    const char *found = strstr(json, pattern);
    if (!found) return NULL;
    
    found += strlen(pattern);
    while (*found && *found != '{') found++;
    if (*found != '{') return NULL;
    
    /* Find matching closing brace */
    const char *end = found + 1;
    int depth = 1;
    while (*end && depth > 0) {
        if (*end == '{') depth++;
        else if (*end == '}') depth--;
        else if (*end == '"') {
            /* Skip strings */
            end++;
            while (*end && *end != '"') {
                if (*end == '\\' && *(end + 1)) end++;
                end++;
            }
        }
        end++;
    }
    
    /* Extract nested value */
    char *nested = util_strndup(found, (size_t)(end - found));
    char *result = util_json_get_string(nested, key);
    free(nested);
    return result;
}

/* ========== Time Utilities ========== */

int64_t util_monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

time_t util_parse_iso8601(const char *s) {
    if (!s) return 0;
    
    struct tm tm = {0};
    /* Try parsing ISO 8601 format: YYYY-MM-DDTHH:MM:SSZ */
    if (sscanf(s, "%d-%d-%dT%d:%d:%d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec) >= 3) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        return timegm(&tm);
    }
    return 0;
}

char *util_format_iso8601(time_t t) {
    struct tm *tm = gmtime(&t);
    if (!tm) return NULL;
    
    char *buf = malloc(64);  /* ISO8601: YYYY-MM-DDTHH:MM:SSZ = 20 chars + padding */
    if (buf) {
        snprintf(buf, 64, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
    }
    return buf;
}

/* ========== Process Utilities ========== */

process_result_t *util_run_command(const char *cmd, const char *cwd, int timeout_ms) {
    process_result_t *result = calloc(1, sizeof(process_result_t));
    if (!result) return NULL;
    
    result->exit_code = -1;
    
    /* Create pipes for stdout and stderr */
    int stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdout_pipe) != 0 || pipe(stderr_pipe) != 0) {
        free(result);
        return NULL;
    }
    
    pid_t pid = fork();
    if (pid < 0) {
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        free(result);
        return NULL;
    }
    
    if (pid == 0) {
        /* Child process */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        if (cwd && chdir(cwd) != 0) {
            _exit(127);
        }
        
        execl("/bin/sh", "sh", "-c", cmd, NULL);
        _exit(127);
    }
    
    /* Parent process */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    
    /* Set non-blocking */
    fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK);
    
    /* Read output with timeout */
    int64_t start_ms = util_monotonic_ms();
    int64_t deadline_ms = start_ms + timeout_ms;
    
    char stdout_buf[65536] = {0};
    char stderr_buf[65536] = {0};
    size_t stdout_len = 0, stderr_len = 0;
    
    int status;
    while (1) {
        /* Check timeout */
        int64_t now_ms = util_monotonic_ms();
        if (timeout_ms > 0 && now_ms >= deadline_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            result->exit_code = -1;
            break;
        }
        
        /* Try to read */
        ssize_t n;
        while ((n = read(stdout_pipe[0], stdout_buf + stdout_len, 
                         sizeof(stdout_buf) - stdout_len - 1)) > 0) {
            stdout_len += (size_t)n;
        }
        while ((n = read(stderr_pipe[0], stderr_buf + stderr_len,
                         sizeof(stderr_buf) - stderr_len - 1)) > 0) {
            stderr_len += (size_t)n;
        }
        
        /* Check if process exited */
        int wait_result = waitpid(pid, &status, WNOHANG);
        if (wait_result > 0) {
            /* Read remaining data */
            while ((n = read(stdout_pipe[0], stdout_buf + stdout_len,
                             sizeof(stdout_buf) - stdout_len - 1)) > 0) {
                stdout_len += (size_t)n;
            }
            while ((n = read(stderr_pipe[0], stderr_buf + stderr_len,
                             sizeof(stderr_buf) - stderr_len - 1)) > 0) {
                stderr_len += (size_t)n;
            }
            
            if (WIFEXITED(status)) {
                result->exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result->exit_code = -WTERMSIG(status);
            }
            break;
        }
        
        /* Small sleep to avoid busy-waiting */
        usleep(10000);
    }
    
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    
    stdout_buf[stdout_len] = '\0';
    stderr_buf[stderr_len] = '\0';
    
    result->stdout_data = util_strdup(stdout_buf);
    result->stderr_data = util_strdup(stderr_buf);
    
    return result;
}

void util_process_result_destroy(process_result_t *res) {
    if (!res) return;
    free(res->stdout_data);
    free(res->stderr_data);
    free(res);
}

/* ========== Logging ========== */

static const char *log_level_str(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO";
        case LOG_WARN:  return "WARN";
        case LOG_ERROR: return "ERROR";
        default:        return "UNKNOWN";
    }
}

void util_log(log_level_t level, const char *issue_id, const char *session_id,
              const char *fmt, ...) {
    char timestamp[32];
    time_t now = time(NULL);
    struct tm *tm = gmtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm);
    
    /* Print timestamp and level */
    fprintf(stderr, "%s [%s]", timestamp, log_level_str(level));
    
    /* Print context if available */
    if (issue_id) {
        fprintf(stderr, " issue_id=%s", issue_id);
    }
    if (session_id) {
        fprintf(stderr, " session_id=%s", session_id);
    }
    
    /* Print message */
    fprintf(stderr, " ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
