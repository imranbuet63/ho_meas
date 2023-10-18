/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef _NVLOG_HPP_
#define _NVLOG_HPP_

#include <stdint.h>

#include <sstream>
#include <iostream>

#include "nvlog.h"
#include "nv_utils.h"

#include "yaml.hpp"

// C++ wrapper for integer tag and none tag logging call
void nvlog_cpp_print(nvlog_t* logger, int level, const char* stag, const char* format, ...);
void nvlog_cpp_print(nvlog_t* logger, int level, int itag, const char* format, ...);

// Redefine the macros to be compatible with integer tag and none tag cases
#undef NVLOGF
#undef NVLOGE
#undef NVLOGC
#undef NVLOGW
#undef NVLOGI
#undef NVLOGD
#undef NVLOGV
#define NVLOGF(tag, format, ...) nvlog_cpp_print(nvlog_default, NVLOG_FATAL, tag, format, ##__VA_ARGS__)
#define NVLOGE(tag, format, ...) nvlog_cpp_print(nvlog_default, NVLOG_ERROR, tag, format, ##__VA_ARGS__)
#define NVLOGC(tag, format, ...) nvlog_cpp_print(nvlog_default, NVLOG_CONSOLE, tag, format, ##__VA_ARGS__)
#define NVLOGW(tag, format, ...) nvlog_cpp_print(nvlog_default, NVLOG_WARN, tag, format, ##__VA_ARGS__)
#define NVLOGI(tag, format, ...) nvlog_cpp_print(nvlog_default, NVLOG_INFO, tag, format, ##__VA_ARGS__)
#define NVLOGD(tag, format, ...) nvlog_cpp_print(nvlog_default, NVLOG_DEBUG, tag, format, ##__VA_ARGS__)
#define NVLOGV(tag, format, ...) nvlog_cpp_print(nvlog_default, NVLOG_VERBOSE, tag, format, ##__VA_ARGS__)

// Create the default nvlog instance with yaml node in primary process
nvlog_t* nvlog_create(yaml::node& yaml_node, const char* name = nullptr);

// Create the default nvlog instance with yaml file name in primary process
nvlog_t* nvlog_create(const char* yaml_file, const char* name = nullptr);

// Lookup the nvlog instance in secondary process
nvlog_t* nvlog_lookup(const char* name);

namespace nv
{
class nvlog {
private:
    int      shm_log_level;     // SHM log level
    int      console_log_level; // Console log level
    nvlog_t* logger;

public:
    nvlog();
    nvlog(nvlog_t* logger);
    nvlog(int primary, const char* name, nvlog_config_t* cfg);

    ~nvlog();

    int  get_shm_log_level();
    void set_shm_log_level(int level);

    int  get_console_log_level();
    void set_console_log_level(int level);

    void print(int level, const char* tag, const char* format, ...);
    void vprint(int level, const char* tag, const char* format, va_list va);

    // Log without tag
    void logf(const char* format, ...);
    void loge(const char* format, ...);
    void logc(const char* format, ...);
    void logw(const char* format, ...);
    void logi(const char* format, ...);
    void logd(const char* format, ...);
    void logv(const char* format, ...);

    // Log with tag
    void tlogf(const char* tag, const char* format, ...);
    void tloge(const char* tag, const char* format, ...);
    void tlogc(const char* tag, const char* format, ...);
    void tlogw(const char* tag, const char* format, ...);
    void tlogi(const char* tag, const char* format, ...);
    void tlogd(const char* tag, const char* format, ...);
    void tlogv(const char* tag, const char* format, ...);
};

class nvlog_stream {
    nvlog_t*          logger    = nvlog_default;
    const char*       stag      = nullptr;
    bool              condition = true;
    int               itag      = 0;
    int               level;
    std::stringstream ss;

public:
    nvlog_stream(int itag, int level)
    {
        this->itag  = itag;
        this->level = level;
    }

    nvlog_stream(const char* stag, int level)
    {
        this->stag  = stag;
        this->level = level;
    }

    nvlog_stream(int level)
    {
        this->level = level;
    }

    nvlog_stream(int level, bool condition)
    {
        this->level     = level;
        this->condition = condition;
    }

    nvlog_stream(nvlog_t* logger, int level, bool condition)
    {
        this->logger    = logger;
        this->level     = level;
        this->condition = condition;
    }

    nvlog_stream(int level, const char* file, const char* func, int line)
    {
        this->level = level;
        ss << "[" << file << ":" << line << "]: ";
    }

    template <class T>
    nvlog_stream& operator<<(T const& x)
    {
        ss << x;
        return *this;
    }

    ~nvlog_stream()
    {
        if(condition)
        {
            ss << std::endl;
            std::string s = ss.str();
            if(stag == nullptr)
            {
                nvlog_cpp_print(logger, level, itag, s.c_str());
            }
            else
            {
                nvlog_cpp_print(logger, level, stag, s.c_str());
            }
        }
    }
};

} // namespace nv

#define NVSLOGF(tag) nv::nvlog_stream(tag, NVLOG_FATAL)
#define NVSLOGE(tag) nv::nvlog_stream(tag, NVLOG_ERROR)
#define NVSLOGC(tag) nv::nvlog_stream(tag, NVLOG_CONSOLE)
#define NVSLOGW(tag) nv::nvlog_stream(tag, NVLOG_WARN)
#define NVSLOGI(tag) nv::nvlog_stream(tag, NVLOG_INFO)
#define NVSLOGD(tag) nv::nvlog_stream(tag, NVLOG_DEBUG)
#define NVSLOGV(tag) nv::nvlog_stream(tag, NVLOG_VERBOSE)

#endif /* _NVLOG_HPP_ */
