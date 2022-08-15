/*
 * Copyright (C) 2019-2024 Amlogic, Inc. All rights reserved.
 *
 */
#ifndef __AML_DEBUG_H__
#define __AML_DEBUG_H__
#include "aml_common.h"

#define CONFIG_AML_LOG_TRACE_LEVEL AML_DBG_ERR

#define AML_DBG_EMERG 0
#define AML_DBG_ALERT 1
#define AML_DBG_CRIT 2
#define AML_DBG_ERR 3
#define AML_DBG_WARN 4
#define AML_DBG_NOTICE 5
#define AML_DBG_INFO 6
#define AML_DBG_DEBUG 7

#define AML_PRINT printk

#define AML_ASSERT(expr)                                                                           \
    do {                                                                                           \
        if (!(expr)) {                                                                             \
            panic("\nASSERT at:\n"                                                                 \
                  "  >Function : %s\n"                                                             \
                  "  >Line No. : %d\n"                                                             \
                  "  >Condition: %s\n",                                                            \
                  __FUNCTION__, __LINE__, #expr);                                                  \
        }                                                                                          \
    } while (0)

#define AML_TRACE(level, mod_id, fmt...)                                                           \
    do {                                                                                           \
        if (level <= CONFIG_AML_LOG_TRACE_LEVEL)                                                   \
            printk(fmt);                                                                           \
    } while (0)

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_EMERG)

#define AML_EMERG_TRACE(mod, fmt...)                                                               \
    do {                                                                                           \
        AML_TRACE(AML_DBG_EMERG, mod, fmt);                                                        \
    } while (0)

#else
#define AML_EMERG_TRACE(mod, fmt...)
#endif

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_ALERT)

#define AML_ALERT_TRACE(mod, fmt...)                                                               \
    do {                                                                                           \
        AML_TRACE(AML_DBG_ALERT, mod, fmt);                                                        \
    } while (0)

#else
#define AML_ALERT_TRACE(mod, fmt...)
#endif

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_CRIT)

#define AML_CRIT_TRACE(mod, fmt...)                                                                \
    do {                                                                                           \
        AML_TRACE(AML_DBG_CRIT, mod, fmt);                                                         \
    } while (0)

#else
#define AML_CRIT_TRACE(mod, fmt...)
#endif

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_ERR)

#define AML_ERR_TRACE(mod, fmt...)                                                                 \
    do {                                                                                           \
        AML_TRACE(AML_DBG_ERR, mod, fmt);                                                          \
    } while (0)

#else
#define AML_ERR_TRACE(mod, fmt...)
#endif

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_WARN)

#define AML_WARN_TRACE(mod, fmt...)                                                                \
    do {                                                                                           \
        AML_TRACE(AML_DBG_WARN, mod, fmt);                                                         \
    } while (0)

#else
#define AML_WARN_TRACE(mod, fmt...)
#endif

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_NOTICE)

#define AML_NOTICE_TRACE(mod, fmt...)                                                              \
    do {                                                                                           \
        AML_TRACE(AML_DBG_NOTICE, mod, fmt);                                                       \
    } while (0)

#else
#define AML_NOTICE_TRACE(mod, fmt...)
#endif

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_INFO)

#define AML_INFO_TRACE(mod, fmt...)                                                                \
    do {                                                                                           \
        AML_TRACE(AML_DBG_INFO, mod, fmt);                                                         \
    } while (0)

#else
#define AML_INFO_TRACE(mod, fmt...)
#endif

#if (CONFIG_AML_LOG_TRACE_LEVEL >= AML_DBG_DEBUG)

#define AML_DEBUG_TRACE(mod, fmt...)                                                               \
    do {                                                                                           \
        AML_TRACE(AML_DBG_DEBUG, mod, fmt);                                                        \
    } while (0)

#else
#define AML_DEBUG_TRACE(mod, fmt...)
#endif

#endif /* __AML_DEBUG_H__ */
