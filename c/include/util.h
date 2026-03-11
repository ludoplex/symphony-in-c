/*
 * Symphony in C
 * Copyright 2024 OpenAI
 * SPDX-License-Identifier: Apache-2.0
 *
 * util.h - Utility functions
 */

#ifndef SYMPHONY_UTIL_H
#define SYMPHONY_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

/* String utilities */
char *util_strdup(const char *s);
char *util_strndup(const char *s, size_t n);
void util_str_tolower(char *s);
void util_str_trim(char *s);
bool util_str_eq_nocase(const char *a, const char *b);
bool util_str_starts_with(const char *s, const char *prefix);
bool util_str_ends_with(const char *s, const char *suffix);

/* Path utilities */
char *util_path_join(const char *base, const char *name);
char *util_path_expand_home(const char *path);
char *util_path_normalize(const char *path);
bool util_path_exists(const char *path);
bool util_path_is_dir(const char *path);
bool util_path_is_under(const char *child, const char *parent);

/* File utilities */
char *util_read_file(const char *path, size_t *size);
bool util_write_file(const char *path, const char *content, size_t size);
bool util_mkdir_p(const char *path);
bool util_rm_rf(const char *path);
time_t util_file_mtime(const char *path);

/* JSON utilities (simple implementation) */
char *util_json_escape(const char *s);
char *util_json_get_string(const char *json, const char *key);
int util_json_get_int(const char *json, const char *key, int default_val);
char *util_json_get_nested(const char *json, const char *parent, const char *key);

/* Time utilities */
int64_t util_monotonic_ms(void);
time_t util_parse_iso8601(const char *s);
char *util_format_iso8601(time_t t);

/* Process utilities */
typedef struct {
    int exit_code;
    char *stdout_data;
    char *stderr_data;
} process_result_t;

process_result_t *util_run_command(const char *cmd, const char *cwd, int timeout_ms);
void util_process_result_destroy(process_result_t *res);

/* Logging */
typedef enum {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
} log_level_t;

void util_log(log_level_t level, const char *issue_id, const char *session_id, 
              const char *fmt, ...);

#define LOG_DEBUG(fmt, ...) util_log(LOG_DEBUG, NULL, NULL, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) util_log(LOG_INFO, NULL, NULL, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) util_log(LOG_WARN, NULL, NULL, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) util_log(LOG_ERROR, NULL, NULL, fmt, ##__VA_ARGS__)

#define LOG_DEBUG_ISSUE(id, fmt, ...) util_log(LOG_DEBUG, id, NULL, fmt, ##__VA_ARGS__)
#define LOG_INFO_ISSUE(id, fmt, ...) util_log(LOG_INFO, id, NULL, fmt, ##__VA_ARGS__)
#define LOG_WARN_ISSUE(id, fmt, ...) util_log(LOG_WARN, id, NULL, fmt, ##__VA_ARGS__)
#define LOG_ERROR_ISSUE(id, fmt, ...) util_log(LOG_ERROR, id, NULL, fmt, ##__VA_ARGS__)

#endif /* SYMPHONY_UTIL_H */
