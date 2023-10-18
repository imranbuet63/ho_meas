/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVLOG_H_
#define _NVLOG_H_

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#ifdef __cplusplus /* For both C and C++ */
extern "C" {
#endif

// Default nvlog configuration file
#define NVLOG_DEFAULT_CONFIG_FILE "cuPHY-CP/gt_common_libs/nvlog/config/nvlog_config.yaml"

// Module tag base numbers
#define NVLOG_TAG_BASE_RESERVED 0            // reserved
#define NVLOG_TAG_BASE_NVLOG 10              // nvlog
#define NVLOG_TAG_BASE_NVIPC 30              // nvIPC
#define NVLOG_TAG_BASE_CUPHY_CONTROLLER 100  // cuphycontroller
#define NVLOG_TAG_BASE_CUPHY_DRIVER 200      // cuphydriver
#define NVLOG_TAG_BASE_L2_ADAPTER 300        // cuphyl2adapter
#define NVLOG_TAG_BASE_SCF_L2_ADAPTER 330    // scfl2adapter
#define NVLOG_TAG_BASE_ALTRAN_L2_ADAPTER 360 // altranl2adapter
#define NVLOG_TAG_BASE_TEST_MAC 400          // testMAC
#define NVLOG_TAG_BASE_RU_EMULATOR 500       // ru-emulator
#define NVLOG_TAG_BASE_FH_DRIVER 600         // aerial-fh-driver
#define NVLOG_TAG_BASE_FH_GENERATOR 650      // fh_generator
#define NVLOG_TAG_BASE_COMPRESSION 700       // compression_decompression
#define NVLOG_TAG_BASE_CUPHY_OAM 800         // cuphyoam
#define NVLOG_TAG_BASE_CUPHY 900             // cuPHY

// Log levels
#define NVLOG_NONE 0 // Set config.shm_log_level or config.console_log_level to NVLOG_NONE can disable all SHM or console log
#define NVLOG_FATAL 1
#define NVLOG_ERROR 2
#define NVLOG_CONSOLE 3
#define NVLOG_WARN 4
#define NVLOG_INFO 5
#define NVLOG_DEBUG 6
#define NVLOG_VERBOSE 7

// Log prefix style options
#define NVLOG_PREFIX_TIME (1 << 0)        // Time Only: 00:00:00.000000
#define NVLOG_PREFIX_DATE (1 << 1)        // Date and Time: 1970-01-01 00:00:00.000000
#define NVLOG_PREFIX_MODULE (1 << 2)      // Module type: 'P', 'S', 'U' (Primary, Secondary, Unknown)
#define NVLOG_PREFIX_LEVEL (1 << 3)       // 'E', 'C', 'W', 'I', 'D', 'V' (Error, Console, Warning, Info, Debug, Verbose)
#define NVLOG_PREFIX_SEQUENCE (1 << 4)    // Global 64-bit sequence number
#define NVLOG_PREFIX_THREAD_ID (1 << 5)   // Caller thread ID
#define NVLOG_PREFIX_THREAD_NAME (1 << 6) // Caller thread name
#define NVLOG_PREFIX_CPU_CORE (1 << 7)    // CPU core number that the caller thread is running on

// Default configurations
#define NVLOG_DEFAULT_NAME "nvlog"
#define NVLOG_DEFAULT_SHM_LOG_LEVEL NVLOG_INFO
#define NVLOG_DEFAULT_CONSOLE_LOG_LEVEL NVLOG_CONSOLE
#define NVLOG_DEFAULT_BUF_SIZE 1024
#define NVLOG_DEFAULT_MAX_THREADS 64
#define NVLOG_DEFAULT_SHM_CACHE_SIZE (1 << 22) // 4MB, value will be aligned to 2^n automatically
#define NVLOG_DEFAULT_MAX_FILE_SIZE (1 << 28)  // 256MB, value will be aligned to multiples of 'shm_cache_size' automatically
#define NVLOG_DEFAULT_PREFIX_OPTIONS (NVLOG_PREFIX_TIME | NVLOG_PREFIX_LEVEL)

#define NVLOG_NAME_MAX_LEN 32                  // Log name string length should be less than 32
#define NVLOG_TEMP_FILE_PATH "/var/log/aerial" // The temporary log file path

typedef enum
{
    NVLOG_FILE_NONE     = 0,
    NVLOG_FILE_ROTATING = 1,
    NVLOG_FILE_STDOUT   = 2,
    NVLOG_FILE_INVALID  = 3,
} nvlog_file_t;

typedef struct
{
    char tag_name[NVLOG_NAME_MAX_LEN];
    int  shm_level;
    int  console_level;
} nvlog_tag_t;

typedef struct
{
    int32_t shm_log_level;     // Log level of printing to SHM
    int32_t console_log_level; // Log level of printing to console

    uint64_t max_file_size;  // Max log length for one time log API call
    int32_t  shm_cache_size; // Shared memory size, see /dev/shm/${name}.log. Value will be aligned to 2^n automatically

    int32_t log_buf_size; // Max log length for one time log API call
    int32_t max_threads;  // Max threads of the caller process. Pre-allocate a buffer array with number of 'max_threads'

    int32_t save_to_file; // Whether to start a background thread for save SHM cache to ${tmp_path}/${name}.log
    int32_t cpu_core_id;  // CPU core ID for the background if enabled. -1 means no core binding.

    int32_t prefix_opts;

    int32_t max_tag_len;
    int32_t max_tag_num;

    nvlog_tag_t tags[];

} nvlog_config_t;

static inline void nvlog_set_default_config(nvlog_config_t* config)
{
    config->shm_log_level     = NVLOG_DEFAULT_SHM_LOG_LEVEL;     // Log level of printing to SHM cache and disk file
    config->console_log_level = NVLOG_DEFAULT_CONSOLE_LOG_LEVEL; // Log level of printing to console

    config->shm_cache_size = NVLOG_DEFAULT_SHM_CACHE_SIZE;
    config->max_file_size  = NVLOG_DEFAULT_MAX_FILE_SIZE;

    config->log_buf_size = NVLOG_DEFAULT_BUF_SIZE;
    config->max_threads  = NVLOG_DEFAULT_MAX_THREADS; // Max threads of the caller process

    config->save_to_file = 1;
    config->cpu_core_id  = -1; // -1 means no core binding by default

    config->prefix_opts = NVLOG_DEFAULT_PREFIX_OPTIONS;

    config->max_tag_len = NVLOG_NAME_MAX_LEN; // Max to 32
    config->max_tag_num = 1024;
}

typedef struct nvlog_t nvlog_t;

// Parameter "primary" and "name" are mandatory, default configuration is used if "cfg" is NULL
nvlog_t* nvlog_open(int primary, const char* name, nvlog_config_t* cfg);
nvlog_t* nvlog_open_from_yaml(int primary, const char* name, const char* yaml_file);
int      nvlog_close(nvlog_t* logger);

// Get default nvlog instance
nvlog_t* nvlog_get_default_logger(void);

nvlog_tag_t*    nvlog_get_tag(nvlog_t* logger, int itag);
nvlog_config_t* nvlog_get_config(nvlog_t* logger);
int             nvlog_get_shm_log_level(nvlog_t* logger);
int             nvlog_get_console_log_level(nvlog_t* logger);

void nvlog_set_tag(nvlog_t* logger, int itag, nvlog_tag_t* tag);
void nvlog_set_tag_primitive(nvlog_t* logger, int itag, const char* tag_name, int shm_level, int console_level);
void nvlog_set_shm_log_level(nvlog_t* logger, int level);
void nvlog_set_console_log_level(nvlog_t* logger, int level);
void nvlog_set_prefix_options(nvlog_t* logger, int prefix_opts);

// Collect /dev/shm/name.log and ${tmp_path}/name.log to <path>/name.log
int nvlog_collect(const char* name, const char* path);

// vprint
void nvlog_vprint(nvlog_t* logger, int level, nvlog_tag_t* tag, const char* format, va_list va);

// String tag print and integer tag print
void nvlog_stag_print(nvlog_t* logger, int level, const char* stag, const char* format, ...);
void nvlog_itag_print(nvlog_t* logger, int level, int itag, const char* format, ...);

extern nvlog_t* nvlog_default;

/*
 * (1) Below NVLOG macros are for C language only, include "nvlog.h" before use.
 * (2) Use NVLOG_USE_INTEGER_TAG_IN_C to select tag type (string or integer) in C source code.
 * (3) For C++ language, the macros are redefined in "nvlog.hpp", both types are supported in C++.
 */
#ifdef NVLOG_USE_INTEGER_TAG_IN_C
#define NVLOG(level, itag, format, ...) nvlog_itag_print(nvlog_default, level, itag, format, ##__VA_ARGS__)
#define NVLOGF(itag, format, ...) nvlog_itag_print(nvlog_default, NVLOG_FATAL, itag, format, ##__VA_ARGS__)
#define NVLOGE(itag, format, ...) nvlog_itag_print(nvlog_default, NVLOG_ERROR, itag, format, ##__VA_ARGS__)
#define NVLOGC(itag, format, ...) nvlog_itag_print(nvlog_default, NVLOG_CONSOLE, itag, format, ##__VA_ARGS__)
#define NVLOGW(itag, format, ...) nvlog_itag_print(nvlog_default, NVLOG_WARN, itag, format, ##__VA_ARGS__)
#define NVLOGI(itag, format, ...) nvlog_itag_print(nvlog_default, NVLOG_INFO, itag, format, ##__VA_ARGS__)
#define NVLOGD(itag, format, ...) nvlog_itag_print(nvlog_default, NVLOG_DEBUG, itag, format, ##__VA_ARGS__)
#define NVLOGV(itag, format, ...) nvlog_itag_print(nvlog_default, NVLOG_VERBOSE, itag, format, ##__VA_ARGS__)
#else
#define NVLOG(level, stag, format, ...) nvlog_stag_print(nvlog_default, level, stag, format, ##__VA_ARGS__)
#define NVLOGF(stag, format, ...) nvlog_stag_print(nvlog_default, NVLOG_FATAL, stag, format, ##__VA_ARGS__)
#define NVLOGE(stag, format, ...) nvlog_stag_print(nvlog_default, NVLOG_ERROR, stag, format, ##__VA_ARGS__)
#define NVLOGC(stag, format, ...) nvlog_stag_print(nvlog_default, NVLOG_CONSOLE, stag, format, ##__VA_ARGS__)
#define NVLOGW(stag, format, ...) nvlog_stag_print(nvlog_default, NVLOG_WARN, stag, format, ##__VA_ARGS__)
#define NVLOGI(stag, format, ...) nvlog_stag_print(nvlog_default, NVLOG_INFO, stag, format, ##__VA_ARGS__)
#define NVLOGD(stag, format, ...) nvlog_stag_print(nvlog_default, NVLOG_DEBUG, stag, format, ##__VA_ARGS__)
#define NVLOGV(stag, format, ...) nvlog_stag_print(nvlog_default, NVLOG_VERBOSE, stag, format, ##__VA_ARGS__)
#endif

// Copy at most (dest_size - 1) bytes and make sure it is terminated by '\0'.
static inline char* nvlog_safe_strncpy(char* dest, const char* src, size_t dest_size)
{
    if(dest == NULL)
    {
        return dest;
    }

    if(src == NULL)
    {
        *dest = '\0'; // Set destination to empty string
        return dest;
    }

    char* ret_dest          = strncpy(dest, src, dest_size - 1); // Reserve 1 byte for '\0'
    *(dest + dest_size - 1) = '\0';                              // Safely terminate the string with '\0'
    return ret_dest;
}

// Get monotonic time stamp
static inline int nvlog_gettime(struct timespec* ts)
{
    return clock_gettime(CLOCK_MONOTONIC, ts);
}

// Get real-time time stamp
static inline int nvlog_gettime_rt(struct timespec* ts)
{
    return clock_gettime(CLOCK_REALTIME, ts);
}

static inline int64_t nvlog_timespec_interval(struct timespec* t1, struct timespec* t2)
{
    return (t2->tv_sec - t1->tv_sec) * 1000000000LL + t2->tv_nsec - t1->tv_nsec;
}

static inline void nvlog_timespec_add(struct timespec* ts, int64_t ns)
{
    ns += ts->tv_nsec;
    ts->tv_sec += ns / 1000000000L;
    ts->tv_nsec = ns % 1000000000L;
}

static inline int64_t nvlog_get_interval(struct timespec* start)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    return (now.tv_sec - start->tv_sec) * 1000000000LL + now.tv_nsec - start->tv_nsec;
}

// struct timeval
static inline int64_t nvlog_timeval_interval(struct timeval* t1, struct timeval* t2)
{
    return (t2->tv_sec - t1->tv_sec) * 1000000LL + t2->tv_usec - t1->tv_usec;
}

#if defined(__cplusplus) /* For both C and C++ */
} /* extern "C" */
#endif

#endif /* _NVLOG_H_ */
