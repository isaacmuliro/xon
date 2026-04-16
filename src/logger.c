#include "logger.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
    int initialized;
    FILE* file;
    char app_name[64];
    char directory[PATH_MAX];
    char file_path[PATH_MAX];
    XonLogLevel level;
    int stderr_enabled;
} XonLoggerState;

static XonLoggerState g_logger = {
    0, NULL, "xon", "logs", "", XON_LOG_INFO, 0
};

static const char* level_to_string(XonLogLevel level) {
    switch (level) {
        case XON_LOG_DEBUG: return "DEBUG";
        case XON_LOG_INFO: return "INFO";
        case XON_LOG_WARN: return "WARN";
        case XON_LOG_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

static XonLogLevel parse_level(const char* value) {
    if (!value) return XON_LOG_INFO;
    if (strcmp(value, "debug") == 0 || strcmp(value, "DEBUG") == 0) return XON_LOG_DEBUG;
    if (strcmp(value, "info") == 0 || strcmp(value, "INFO") == 0) return XON_LOG_INFO;
    if (strcmp(value, "warn") == 0 || strcmp(value, "WARN") == 0) return XON_LOG_WARN;
    if (strcmp(value, "error") == 0 || strcmp(value, "ERROR") == 0) return XON_LOG_ERROR;
    return XON_LOG_INFO;
}

static int ensure_directory(const char* path) {
    char buffer[PATH_MAX];
    char* p;

    if (!path || !path[0]) return 0;
    if (strlen(path) >= sizeof(buffer)) return 0;

    snprintf(buffer, sizeof(buffer), "%s", path);
    for (p = buffer + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(buffer, 0755) != 0 && errno != EEXIST) return 0;
            *p = '/';
        }
    }
    if (mkdir(buffer, 0755) != 0 && errno != EEXIST) return 0;
    return 1;
}

static void timestamp(char* out, size_t out_size) {
    time_t now = time(NULL);
    struct tm tm_buf;
#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    strftime(out, out_size, "%Y-%m-%dT%H:%M:%S%z", &tm_buf);
}

static int open_log_file(void) {
    char day[32];
    time_t now = time(NULL);
    struct tm tm_buf;

    if (!ensure_directory(g_logger.directory)) return 0;

#if defined(_WIN32)
    localtime_s(&tm_buf, &now);
#else
    localtime_r(&now, &tm_buf);
#endif
    strftime(day, sizeof(day), "%Y-%m-%d", &tm_buf);
    snprintf(g_logger.file_path, sizeof(g_logger.file_path), "%s/%s-%s.log",
             g_logger.directory, g_logger.app_name, day);

    g_logger.file = fopen(g_logger.file_path, "a");
    if (!g_logger.file) return 0;
    return 1;
}

int xon_logger_init(const char* app_name) {
    const char* env_dir;
    const char* env_level;
    const char* env_stderr;

    if (g_logger.initialized) return 1;

    if (app_name && app_name[0]) {
        snprintf(g_logger.app_name, sizeof(g_logger.app_name), "%s", app_name);
    }

    env_dir = getenv("XON_LOG_DIR");
    if (env_dir && env_dir[0]) {
        snprintf(g_logger.directory, sizeof(g_logger.directory), "%s", env_dir);
    }

    env_level = getenv("XON_LOG_LEVEL");
    g_logger.level = parse_level(env_level);

    env_stderr = getenv("XON_LOG_STDERR");
    if (env_stderr) {
        g_logger.stderr_enabled = (strcmp(env_stderr, "1") == 0) ? 1 : 0;
    }

    if (!open_log_file()) {
        g_logger.initialized = 0;
        return 0;
    }

    g_logger.initialized = 1;
    xon_log_info("logger", "Logger initialized at %s", g_logger.file_path);
    return 1;
}

int xon_logger_set_directory(const char* directory) {
    if (!directory || !directory[0]) return 0;
    snprintf(g_logger.directory, sizeof(g_logger.directory), "%s", directory);

    if (g_logger.file) {
        fclose(g_logger.file);
        g_logger.file = NULL;
    }
    if (!open_log_file()) return 0;
    g_logger.initialized = 1;
    xon_log_info("logger", "Log directory set to %s", g_logger.directory);
    return 1;
}

void xon_logger_set_level(XonLogLevel level) {
    g_logger.level = level;
    if (!g_logger.initialized) return;
    xon_log_info("logger", "Log level changed to %s", level_to_string(level));
}

void xon_logger_enable_stderr(int enabled) {
    g_logger.stderr_enabled = enabled ? 1 : 0;
}

void xon_logger_shutdown(void) {
    if (!g_logger.initialized) return;
    xon_log_info("logger", "Logger shutdown");
    if (g_logger.file) {
        fclose(g_logger.file);
        g_logger.file = NULL;
    }
    g_logger.initialized = 0;
}

void xon_log_write(XonLogLevel level, const char* component, const char* fmt, ...) {
    char ts[64];
    char message[2048];
    va_list args;

    if (!g_logger.initialized) {
        if (!xon_logger_init("xon")) {
            if (level >= XON_LOG_WARN) {
                va_start(args, fmt);
                vsnprintf(message, sizeof(message), fmt, args);
                va_end(args);
                fprintf(stderr, "[%s] %s\n", component ? component : "xon", message);
            }
            return;
        }
    }

    if (level < g_logger.level) return;

    timestamp(ts, sizeof(ts));
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (g_logger.file) {
        fprintf(g_logger.file, "[%s] [%s] [%s] %s\n",
                ts, level_to_string(level), component ? component : "xon", message);
        fflush(g_logger.file);
    }

    if (g_logger.stderr_enabled && level >= XON_LOG_WARN) {
        fprintf(stderr, "[%s] [%s] %s\n", level_to_string(level), component ? component : "xon", message);
    }
}
