/* -------------------------------
 * vim:tabstop=4:shiftwidth=4
 * common.h
 * Mon, 01 May 2006 01:45:08 +0700
 * -------------------------------
 * Common declarations
 * -------------------------------*/

#ifndef _COMMON_H_
#define _COMMON_H_

#include "debug.h"

/* Default icon size */
#define FALLBACK_ICON_SIZE 24
/* Minimal icon size */
#define MIN_ICON_SIZE 16
/* Default KDE icon size */
#define KDE_ICON_SIZE 22
/* Number of time icon is allowed to change its size after which
 * stalonetray gives up */
#define ICON_SIZE_RESETS_THRESHOLD 2

/* Meaningful names for return values */
#define SUCCESS 1
#define FAILURE 0

/* Simple macro to return status and log it if necessary */
#define RETURN_STATUS(rc) \
    do { \
        LOG_TRACE( \
            ("status = %s\n", ((rc) == SUCCESS ? "SUCCESS" : "FAILURE"))); \
        return rc; \
    } while (0)

/* Meaningful names for return values of icon mass-operations */
#define MATCH 1
#define NO_MATCH 0

/* Simple macro to return mass-op status and log it if necessary */
#define RETURN_MATCH(rc) \
    do { \
        LOG_TRACE(("status = %s\n", rc == MATCH : "MATCH" : "NO_MATCH")); \
        return rc; \
    } while (0)

/* Meaningful names for return values of icon mass-operations */
#define MATCH 1
/* Portable macro for function name */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#define __FUNC__ __func__
#elif defined(__GNUC__) && __GNUC__ >= 3
#define __FUNC__ __FUNCTION__
#else
#define __FUNC__ "unknown"
#endif

/* DIE */
#define DIEDIE() exit(-1)
/* Print a message and... DIE */
#define DIE(message) \
    do { \
        LOG_ERROR(message); \
        DIEDIE(); \
    } while (0)
/* Log OOM condition */
#define LOG_ERR_IE(message) \
    do { \
        LOG_ERROR( \
            ("Internal error, please report to maintaner (see AUTHORS)\n")); \
        LOG_ERROR(message); \
    } while (0);
/* DIE on internal error */
#define DIE_IE(message) \
    do { \
        LOG_ERR_IE(message); \
        DIEDIE(); \
    } while (0)
/* Log OOM condition */
#define LOG_ERR_OOM(message) \
    do { \
        LOG_ERROR(("Out of memory\n")); \
        LOG_ERROR(message); \
    } while (0);
/* DIE on OOM error */
#define DIE_OOM(message) \
    do { \
        LOG_ERR_OOM(message); \
        DIEDIE(); \
    } while (0)

/*** WARNING: feed following macros only with side-effects-free expressions
 * ***/
/* Get a value within target interval */
#define cutoff(tgt, min, max) \
    (tgt) < (min) ? (min) : ((tgt) > (max) ? max : tgt)
/* Update value to fit into target interval */
#define val_range(tgt, min, max) (tgt) = cutoff(tgt, min, max)

#endif
