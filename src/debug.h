/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * debug.h
 * Tue, 24 Aug 2004 12:05:38 +0700
 * -------------------------------
 * Debugging code/utilities
 * -------------------------------*/

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>
#include <stdlib.h>

#include "common.h"
#include "settings.h"
#include "tray.h"

#include <string.h>
#include <time.h>

#define LOG_LEVEL_ERR 0
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_TRACE 2

extern int debug_output_disabled;

/* Disables all output from debugging macros */
void debug_disable_output();

/* Print the message to STDERR (in the sake of portability, we do not use
 * varadic macros) */
void print_message_to_stderr(const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((__format__(__printf__, 1, 2)))
#endif
    ;

#ifdef DEBUG
/* The following defines control what is printed in each logged line */
/* Print window name */
#define TRACE_WM_NAME
/* Print pid */
#undef TRACE_PID
/* Print name of display */
#undef TRACE_DPY
/* Print timestamp */
#define TRACE_TIMESTAMP
/* Print name of a function, file and line which outputs the message */
/* Othewise, only function name is printed */
#undef TRACE_VERBOSE_LOCATION
/* Print the debug header as specified by defines below */
void print_trace_header(
    const char *funcname, const char *fname, const int line);
#define PRINT_TRACE_HEADER() \
    do { \
        print_trace_header(__FUNC__, __FILE__, __LINE__); \
    } while (0)
/* Print the debug message of specified level */
#define LOG_TRACE(message) \
    do { \
        if (!debug_output_disabled \
            && settings.log_level >= LOG_LEVEL_TRACE) { \
            PRINT_TRACE_HEADER(); \
            print_message_to_stderr message; \
        }; \
    } while (0)
#else
#define PRINT_TRACE_HEADER() \
    do { \
    } while (0)
#define LOG_TRACE(message) \
    do { \
    } while (0)
#endif

#define LOG_ERROR(message) \
    do { \
        if (!debug_output_disabled) { \
            if (settings.log_level >= LOG_LEVEL_TRACE) PRINT_TRACE_HEADER(); \
            if (settings.log_level >= LOG_LEVEL_ERR) \
                print_message_to_stderr message; \
        } \
    } while (0)

#define LOG_INFO(message) \
    do { \
        if (!debug_output_disabled) { \
            if (settings.log_level >= LOG_LEVEL_TRACE) PRINT_TRACE_HEADER(); \
            if (settings.log_level >= LOG_LEVEL_INFO) \
                print_message_to_stderr message; \
        } \
    } while (0)

/* Print the summary of icon data */
int print_icon_data(struct TrayIcon *ti);
/* Print icon list contents */
void dump_icon_list();

#endif
