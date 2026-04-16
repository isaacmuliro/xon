#ifndef XON_LOGGER_H
#define XON_LOGGER_H

#include "../include/xon_api.h"

int xon_logger_init(const char* app_name);
int xon_logger_set_directory(const char* directory);
void xon_logger_set_level(XonLogLevel level);
void xon_logger_enable_stderr(int enabled);
void xon_logger_shutdown(void);
void xon_log_write(XonLogLevel level, const char* component, const char* fmt, ...);

#define xon_log_debug(component, fmt, ...) xon_log_write(XON_LOG_DEBUG, component, fmt, ##__VA_ARGS__)
#define xon_log_info(component, fmt, ...) xon_log_write(XON_LOG_INFO, component, fmt, ##__VA_ARGS__)
#define xon_log_warn(component, fmt, ...) xon_log_write(XON_LOG_WARN, component, fmt, ##__VA_ARGS__)
#define xon_log_error(component, fmt, ...) xon_log_write(XON_LOG_ERROR, component, fmt, ##__VA_ARGS__)

#endif // XON_LOGGER_H
