/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#ifndef NV_IPC_UTILS_H_INCLUDED_
#define NV_IPC_UTILS_H_INCLUDED_

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "nv_ipc.h"
#include "nvlog.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef NVLOG_C // Change all NVLOGW to NVLOGI for FMT logger
#undef NVLOGW
#define NVLOGW NVLOGI
#endif

#define LOG_TEMP_FILE_PATH "/var/log/aerial"

#define SFN_SLOT_INVALID 0xFFFFFFFF

typedef union
{
    uint32_t u32;
    struct
    {
        uint16_t sfn;
        uint16_t slot;
    } u16;
} sfn_slot_t;

sfn_slot_t nv_ipc_get_sfn_slot(nv_ipc_msg_t* msg);

int64_t nv_ipc_get_ts_send(nv_ipc_t* ipc, nv_ipc_msg_t* msg);
#ifdef ENABLE_L2_SLT_RSP
int nv_ipc_shm_send_loopback(nv_ipc_t* ipc, nv_ipc_msg_t* msg);
#endif

int cuda_get_device_count(void);

int cuda_is_device_pointer(const void *ptr);

static inline int is_device_pointer(const void *ptr) {
    int is_in_gpu = 0;
#ifdef NVIPC_CUDA_ENABLE
    is_in_gpu = cuda_is_device_pointer(ptr);
#endif
    return is_in_gpu;
}

int nv_ipc_dump(nv_ipc_t* ipc);

#if defined(__cplusplus)
} /* extern "C" */
#endif

#endif /* NV_IPC_UTILS_H_INCLUDED_ */
