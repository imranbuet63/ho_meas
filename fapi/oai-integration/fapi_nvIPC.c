/*
 * Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <stdatomic.h>
#include <sys/queue.h>
#include <sys/epoll.h>
#include "fapi_nvIPC.h"
#include <nfapi_interface.h>
#include <nfapi.h>
#include "nfapi_nr_interface_scf.h"
#include "nfapi/open-nFAPI/vnf/inc/vnf_p7.h"

#include "fapi_vnf_p5.h"
#include "fapi_vnf_p7.h"

#ifdef NVIPC_DPDK_ENABLE
#include "../nvidia/include/nvIPC/nv_ipc_dpdk_utils.h"
//#include "nv_ipc_dpdk_utils.h"
#endif

// Log level: NVLOG_ERROR, NVLOG_CONSOLE, NVLOG_WARN, NVLOG_INFO, NVLOG_DEBUG, NVLOG_VERBOSE
#define DEFAULT_TEST_LOG_LEVEL NVLOG_DEBUG
#define DEFAULT_TEST_LOG_LEVEL_CONSOLE NVLOG_CONSOLE

#define TEST_DUPLEX_TRANSFER 1

// Configure whether to sync by TTI or sync by one single message.
#define CONFIG_SYNC_BY_TTI 1

#define TEST_MSG_COUNT 3
#define MAX_EVENTS 10
#define TEST_DATA_BUF_LEN 8192

#define UDP_PACKET_MAX_SIZE 65000
#define SHM_MSG_BUF_SIZE (8192)//was 5000
#define SHM_DATA_BUF_SIZE (UDP_PACKET_MAX_SIZE - SHM_MSG_BUF_SIZE) // PDU buffer size

#define IPC_DATA_SIZE (100 * 1024)

#define NIC_PCI_ADDR "b5:00.0"
#define ETH_MAC_PRIMARY "b8:ce:f6:33:fe:23"
#define ETH_MAC_SECONDARY "00:00:00:00:00:00" // No need to configure secondary MAC
uint16_t sfn = 0, slot = 0;
// The CUDA device ID. Can set to -1 to fall back to CPU memory IPC
int test_cuda_device_id = -1;

atomic_ulong poll_counter;

typedef struct {
  int32_t msg_id;
  int32_t cell_id;
  int32_t msg_len;
  int32_t data_len;
  int32_t data_pool;
} test_msg_t;

// Log TAG to be configured at main()
static char TAG[16];

nv_ipc_transport_t ipc_transport;
nv_ipc_module_t module_type;

volatile nv_ipc_t *ipc;

char cpu_buf_send[TEST_DATA_BUF_LEN];
char cpu_buf_recv[TEST_DATA_BUF_LEN];

#define FAPI_SLOT_INDATION 0x82
#define FAPI_DL_TTI_REQUEST 0x80
#define FAPI_UL_TTI_REQUEST 0x81
#define FAPI_TX_DATA_REQUEST 0x84
#define FAPI_RX_DATA_INDICATION 0x85
#define FAPI_RESERVED_MSG1 0xF1
#define FAPI_RESERVED_MSG2 0xF2

nfapi_vnf_config_t *vnf_config = 0;

void set_config(nfapi_vnf_config_t *conf)
{
  vnf_config = conf;
}
static uint16_t old_sfn = 0;
static uint16_t old_slot = 0;
////////////////////////////////////////////////////////////////////////
// Handle an RX message
static int ipc_handle_rx_msg(nv_ipc_t *ipc, nv_ipc_msg_t *msg)
{
  if (msg == NULL) {
    NVLOGE(TAG, "%s: ERROR: buffer is empty\n", __func__);
    return -1;
  }

  int32_t *p_fapi = msg->msg_buf;
  msg->msg_id = *p_fapi;
  char *str = (char *)(p_fapi + 1);

  char *p_cpu_data = NULL;
  if (msg->data_buf != NULL) {
    int gpu;
    if (msg->data_pool == NV_IPC_MEMPOOL_CUDA_DATA) {
      gpu = 1;
    } else {
      gpu = 0;
    }

#ifdef NVIPC_CUDA_ENABLE
    // Test CUDA: call CUDA functions to change all string to lower case
    test_cuda_to_lower_case(test_cuda_device_id, msg->data_buf, TEST_DATA_BUF_LEN, gpu);
#endif

    if (gpu) {
      p_cpu_data = cpu_buf_recv;
      memset(cpu_buf_recv, 0, TEST_DATA_BUF_LEN);
      ipc->cuda_memcpy_to_host(ipc, p_cpu_data, msg->data_buf, TEST_DATA_BUF_LEN);
    } else {
      p_cpu_data = msg->data_buf;
    }
  }

  int messageBufLen = msg->msg_len;
  int dataBufLen = msg->data_len;
  uint8_t msgbuf[messageBufLen];
  uint8_t databuf[dataBufLen];
  memcpy(msgbuf, msg->msg_buf, messageBufLen);
  memcpy(databuf, msg->data_buf, dataBufLen);
  uint8_t *pReadPackedMessage = msgbuf;
  uint8_t *pReadData = databuf;
  uint8_t *end = msgbuf + messageBufLen;
  uint8_t *data_end = databuf + dataBufLen;

  // unpack FAPI messages and handle them
  if (vnf_config != 0) {
    // first, unpack the header
    fapi_phy_api_msg *fapi_msg = calloc(1, sizeof(fapi_phy_api_msg));
    if (!(pull8(&pReadPackedMessage, &fapi_msg->num_msg, end) && pull8(&pReadPackedMessage, &fapi_msg->opaque_handle, end)
          && pull16(&pReadPackedMessage, &fapi_msg->message_id, end)
          && pull32(&pReadPackedMessage, &fapi_msg->message_length, end))) {
      NFAPI_TRACE(NFAPI_TRACE_ERROR, "FAPI message header unpack failed\n");
      return -1;
    }

    switch (fapi_msg->message_id) {
      case NFAPI_NR_PHY_MSG_TYPE_PARAM_RESPONSE:

        if (vnf_config->nr_param_resp) {
          nfapi_nr_param_response_scf_t msg_param_resp;
          aerial_unpack_nr_param_response(&pReadPackedMessage, end, &msg_param_resp, &vnf_config->codec_config);
          (vnf_config->nr_param_resp)(vnf_config, vnf_config->pnf_list->p5_idx, &msg_param_resp);
        }
        break;

      case NFAPI_NR_PHY_MSG_TYPE_CONFIG_RESPONSE: {
        // unpack message
        nfapi_nr_config_response_scf_t msg_config_response;
        aerial_unpack_nr_config_response(&pReadPackedMessage, end, &msg_config_response, &vnf_config->codec_config);
        // Check the error code
        if (msg_config_response.error_code == NFAPI_NR_CONFIG_MSG_OK) {
          // Invoke the call back
          if (vnf_config->nr_config_resp) {
            (vnf_config->nr_config_resp)(vnf_config, vnf_config->pnf_list->p5_idx, &msg_config_response);
          }
        } else {
          // Error code not OK (MSG_INVALID_CONFIG)
          /* MSG_INVALID_CONFIG.response structure
           * Error code uint8_t
           * Number of invalid or unsupported TLVs uint8_t
           * Number of invalid TLVs that can only be configured in IDLE state uint8_t
           * Number of invalid TLVs that can only be configured in RUNNING state uint8_t
           * Number of missing TLVs uint8_t
           * List of invalid or unsupported TLVs
           * List of invalid TLVs that can only be configured in IDLE state
           * List of invalid TLVs that can only be configured in RUNNING state
           * List of missing TLVs
           * */
        }
        break;
      }

      case NFAPI_NR_PHY_MSG_TYPE_STOP_INDICATION: {
        // TODO : ADD Support for NFAPI_NR_PHY_MSG_TYPE_STOP_INDICATION (0x06)
        printf("Received NFAPI_NR_PHY_MSG_TYPE_STOP_INDICATION\n");
        break;
      }

      case NFAPI_NR_PHY_MSG_TYPE_ERROR_INDICATION: {
        // TODO: Add Support for NFAPI_NR_PHY_MSG_TYPE_ERROR_INDICATION (0x07)
        printf("Received NFAPI_NR_PHY_MSG_TYPE_ERROR_INDICATION\n");
        //for (int i = 0; i < msg->msg_len; i++) {
        //  printf(" msg->msg_buf[%d] = 0x%02x\n", i, ((uint8_t *) msg->msg_buf)[i]);
        //}
        printf("old sfn %u\n", old_sfn);
        printf("old slot %u\n", old_slot);

        break;
      }
      // P7 Messages
      // P7 Message Handlers -> ((vnf_info *)vnf_config->user_data)->p7_vnfs->config->
      case NFAPI_NR_PHY_MSG_TYPE_SLOT_INDICATION: {
        nfapi_nr_slot_indication_scf_t ind;
        aerial_unpack_nr_slot_indication(&pReadPackedMessage,
                                         end,
                                         &ind,
                                         &((vnf_p7_t *)((vnf_info *)vnf_config->user_data)->p7_vnfs->config)->_public.codec_config);
        // check if the sfn/slot unpacked come wrong at any time, should be old + 1 (slot 0 -- 19, sfn 0 -- 1023)
        // add 1 to current sfn number
        uint16_t old_slot_plus = ((old_slot + 1) % 20);
        uint16_t old_sfn_plus = old_slot_plus == 0 ? ((old_sfn + 1) % 1024) : old_sfn;
        if (old_slot_plus != ind.slot || old_sfn_plus != ind.sfn) {
          printf("============================================================================\n");
          LOG_E(NFAPI_VNF,
                "sfn slot doesn't match unpacked one! L2->L1 %d.%d  vs L1->L2 %d.%d  \n",
                old_sfn,
                old_slot,
                ind.sfn,
                ind.slot);
          printf("============================================================================\n");
        }
        old_sfn = ind.sfn;
        old_slot = ind.slot;
        if (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_slot_indication) {
          (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_slot_indication)(&ind);
        }
        break;
      }

      case NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION: {
        nfapi_nr_rx_data_indication_t ind;
        ind.header.message_id = fapi_msg->message_id;
        ind.header.message_length = fapi_msg->message_length;
        aerial_unpack_nr_rx_data_indication(
            &pReadPackedMessage,
            end,
            &pReadData,
            data_end,
            &ind,
            &((vnf_p7_t *)((vnf_info *)vnf_config->user_data)->p7_vnfs->config)->_public.codec_config);
        NFAPI_TRACE(NFAPI_TRACE_INFO, "%s: Handling RX Indication\n", __FUNCTION__);
        if (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_rx_data_indication) {
          (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_rx_data_indication)(&ind);
        }
        break;
      }

      case NFAPI_NR_PHY_MSG_TYPE_CRC_INDICATION: {
        nfapi_nr_crc_indication_t crc_ind;
        crc_ind.header.message_id = fapi_msg->message_id;
        crc_ind.header.message_length = fapi_msg->message_length;
        aerial_unpack_nr_crc_indication(&pReadPackedMessage,
                                        end,
                                        &crc_ind,
                                        &((vnf_p7_t *)((vnf_info *)vnf_config->user_data)->p7_vnfs->config)->_public.codec_config);
        if (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_crc_indication) {
          (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_crc_indication)(&crc_ind);
        }
        break;
      }

      case NFAPI_NR_PHY_MSG_TYPE_UCI_INDICATION: {
        nfapi_nr_uci_indication_t ind;
        aerial_unpack_nr_uci_indication(&pReadPackedMessage,
                                        end,
                                        &ind,
                                        &((vnf_p7_t *)((vnf_info *)vnf_config->user_data)->p7_vnfs->config)->_public.codec_config);

        if (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_uci_indication) {
          (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_uci_indication)(&ind);
        }

        break;
      }

      case NFAPI_NR_PHY_MSG_TYPE_SRS_INDICATION: {
        nfapi_nr_srs_indication_t ind;
        aerial_unpack_nr_srs_indication(&pReadPackedMessage,
                                        end,
                                        &ind,
                                        &((vnf_p7_t *)((vnf_info *)vnf_config->user_data)->p7_vnfs->config)->_public.codec_config);
        if (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_srs_indication) {
          (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_srs_indication)(&ind);
        }
        break;
      }

      case NFAPI_NR_PHY_MSG_TYPE_RACH_INDICATION: {
        nfapi_nr_rach_indication_t ind;
        aerial_unpack_nr_rach_indication(&pReadPackedMessage,
                                         end,
                                         &ind,
                                         &((vnf_p7_t *)((vnf_info *)vnf_config->user_data)->p7_vnfs->config)->_public.codec_config);
        if (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_rach_indication) {
          (((vnf_info *)vnf_config->user_data)->p7_vnfs->config->nr_rach_indication)(&ind);
        }
        break;
      }

      default: {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s P5 Unknown message ID %d\n", __FUNCTION__, fapi_msg->message_id);

        break;
      }
    }
  }
  return 0;
}

int8_t buf[1024];

// Get nvipc configuration
nv_ipc_config_t nv_ipc_config;

int test_nv_ipc_send_msg_P5(void *packedBuf, uint32_t packedMsgLength, nfapi_p4_p5_message_header_t *header)
{
  if (ipc == NULL) {
    return -1;
  }
  nv_ipc_msg_t send_msg;
  // look for the specific message
  switch (header->message_id) {
    case NFAPI_NR_PHY_MSG_TYPE_PARAM_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_PARAM_RESPONSE:
    case NFAPI_NR_PHY_MSG_TYPE_CONFIG_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_CONFIG_RESPONSE:
    case NFAPI_NR_PHY_MSG_TYPE_START_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_START_RESPONSE:
    case NFAPI_NR_PHY_MSG_TYPE_STOP_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_STOP_RESPONSE:
      break;
    default: {
      if (header->message_id >= NFAPI_VENDOR_EXT_MSG_MIN && header->message_id <= NFAPI_VENDOR_EXT_MSG_MAX) {
        // if(config && config->pack_p4_p5_vendor_extension) {
        //   result = (config->pack_p4_p5_vendor_extension)(header, ppWritePackedMsg, end, config);
        // } else {
        //   NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s VE NFAPI message ID %d. No ve ecoder provided\n", __FUNCTION__, header->message_id);
        // }
      } else {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s NFAPI Unknown message ID %d\n", __FUNCTION__, header->message_id);
      }
    } break;
  }
  send_msg.msg_id = header->message_id;
  send_msg.cell_id = 0;
  send_msg.msg_len = packedMsgLength + 8; // adding 8 to account for the size of the FAPI header
  send_msg.data_len = 0;
  send_msg.data_buf = NULL;
  send_msg.data_pool = NV_IPC_MEMPOOL_CPU_MSG;

  // procedure is  allocate->fill->send
  int alloc_retval = ipc->tx_allocate(ipc, &send_msg, 0);
  if (alloc_retval != 0) {
    NVLOGE(TAG, "%s error: allocate TX buffer failed Error: %d\n", __func__, alloc_retval);
    printf("%s error: allocate TX buffer failed Error: %d\n", __func__, alloc_retval);
    ipc->tx_release(ipc, &send_msg);
    return alloc_retval;
  }

  memcpy(send_msg.msg_buf, packedBuf, send_msg.msg_len);
  NVLOGD(TAG,
         "send: cell_id=%d msg_id=0x%02X msg_len=%d data_len=%d data_pool=%d\n",
         send_msg.cell_id,
         send_msg.msg_id,
         send_msg.msg_len,
         send_msg.data_len,
         send_msg.data_pool);
  // Send the message
  int send_retval = ipc->tx_send_msg(ipc, &send_msg);
  if (send_retval < 0) {
    NVLOGE(TAG, "%s error: send TX message failed Error: %d\n", __func__, send_retval);
    printf("%s error: send TX message failed Error: %d\n", __func__, send_retval);
    ipc->tx_release(ipc, &send_msg);
    return send_retval;
  }

  ipc->notify(ipc, 1); // notify that there's 1 message in queue
  return 0;
}

int test_nv_ipc_send_msg_P7(void *packedBuf, uint32_t packedMsgLength, nfapi_p7_message_header_t *header)
{
  if (ipc == NULL) {
    return -1;
  }
  nv_ipc_msg_t send_msg;
  uint8_t *pPacketBodyField = &packedBuf[8];
  uint8_t *pPackMessageEnd = packedBuf + packedMsgLength + 8;

  uint16_t present_sfn = 0;
  uint16_t present_slot = 0;
  pull16(&pPacketBodyField, &present_sfn, pPackMessageEnd);
  pull16(&pPacketBodyField, &present_slot, pPackMessageEnd);

  if (present_sfn != old_sfn || present_slot != old_slot) {
    LOG_E(NFAPI_VNF,
          "sfn slot doesn't match unpacked one! L2->L1 %d.%d  vs L1->L2 %d.%d  \n",
          present_sfn,
          present_slot,
          old_sfn,
          old_slot);
  }
  // look for the specific message
  switch (header->message_id) {
    case NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_UL_TTI_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_UL_DCI_REQUEST:
    case NFAPI_UE_RELEASE_REQUEST:
    case NFAPI_UE_RELEASE_RESPONSE:
    case NFAPI_NR_PHY_MSG_TYPE_SLOT_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_CRC_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_UCI_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_SRS_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_RACH_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_DL_NODE_SYNC:
    case NFAPI_NR_PHY_MSG_TYPE_UL_NODE_SYNC:
    case NFAPI_TIMING_INFO:
    case 0x8f:
      break;
    default: {
      if (header->message_id >= NFAPI_VENDOR_EXT_MSG_MIN && header->message_id <= NFAPI_VENDOR_EXT_MSG_MAX) {
      } else {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s NFAPI Unknown message ID %d\n", __FUNCTION__, header->message_id);
      }
    } break;
  }

  send_msg.msg_id = header->message_id;
  send_msg.cell_id = 0;
  send_msg.msg_len = packedMsgLength + 8; // adding 8 to account for the size of the FAPI header
  send_msg.data_len = 0;
  send_msg.data_buf = NULL;
  send_msg.data_pool = NV_IPC_MEMPOOL_CPU_MSG;
  // procedure is  allocate->fill->send

  // Allocate buffer for TX message
  int alloc_retval = ipc->tx_allocate(ipc, &send_msg, 0);
  if (alloc_retval != 0) {
    NVLOGE(TAG, "%s error: allocate TX buffer failed Error: %d\n", __func__, alloc_retval);
    printf("%s error: allocate TX buffer failed Error: %d\n", __func__, alloc_retval);
    ipc->tx_release(ipc, &send_msg);
    return alloc_retval;
  }

  memcpy(send_msg.msg_buf, packedBuf, send_msg.msg_len);
  NVLOGD(TAG,
         "send: cell_id=%d msg_id=0x%02X msg_len=%d data_len=%d data_pool=%d\n",
         send_msg.cell_id,
         send_msg.msg_id,
         send_msg.msg_len,
         send_msg.data_len,
         send_msg.data_pool);
  // Send the message
  int send_retval = ipc->tx_send_msg(ipc, &send_msg);
  if (send_retval < 0) {
    NVLOGE(TAG, "%s error: send TX message failed Error: %d\n", __func__, send_retval);
    printf("%s error: send TX message failed Error: %d\n", __func__, send_retval);
    ipc->tx_release(ipc, &send_msg);
    return send_retval;
  }

  ipc->notify(ipc, 1); // notify that there's 1 message in queue
  return 0;
}

int test_nv_ipc_send_msg_P7_with_data(void *packedBuf,
                                      uint32_t packedMsgLength,
                                      void *dataBuf,
                                      uint32_t dataLength,
                                      nfapi_p7_message_header_t *header)
{
  if (ipc == NULL) {
    return -1;
  }
  nv_ipc_msg_t send_msg;
  uint8_t *pPacketBodyField = &packedBuf[8];
  uint8_t *pPackMessageEnd = packedBuf + packedMsgLength;
  // look for the specific message
  switch (header->message_id) {
    case NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_UL_TTI_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST:
    case NFAPI_NR_PHY_MSG_TYPE_UL_DCI_REQUEST:
    case NFAPI_UE_RELEASE_REQUEST:
    case NFAPI_UE_RELEASE_RESPONSE:
    case NFAPI_NR_PHY_MSG_TYPE_SLOT_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_RX_DATA_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_CRC_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_UCI_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_SRS_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_RACH_INDICATION:
    case NFAPI_NR_PHY_MSG_TYPE_DL_NODE_SYNC:
    case NFAPI_NR_PHY_MSG_TYPE_UL_NODE_SYNC:
    case NFAPI_TIMING_INFO:
      break;
    default: {
      if (header->message_id >= NFAPI_VENDOR_EXT_MSG_MIN && header->message_id <= NFAPI_VENDOR_EXT_MSG_MAX) {
      } else {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s NFAPI Unknown message ID %d\n", __FUNCTION__, header->message_id);
      }
    } break;
  }

  send_msg.msg_id = header->message_id;
  send_msg.cell_id = 0;
  send_msg.msg_len = packedMsgLength + 8; // adding 8 to account for the size of the FAPI header
  send_msg.data_len = dataLength;
  send_msg.data_pool = NV_IPC_MEMPOOL_CPU_DATA;

  // procedure is  allocate->fill->send

  // Allocate buffer for TX message
  int alloc_retval = ipc->tx_allocate(ipc, &send_msg, 0);
  if (alloc_retval != 0) {
    NVLOGE(TAG, "%s error: allocate TX buffer failed Error: %d\n", __func__, alloc_retval);
    printf("%s error: allocate TX buffer failed Error: %d\n", __func__, alloc_retval);
    ipc->tx_release(ipc, &send_msg);
    return alloc_retval;
  }

  memcpy(send_msg.msg_buf, packedBuf, send_msg.msg_len);
  memcpy(send_msg.data_buf, dataBuf, send_msg.data_len);
  NVLOGD(TAG,
         "send: cell_id=%d msg_id=0x%02X msg_len=%d data_len=%d data_pool=%d\n",
         send_msg.cell_id,
         send_msg.msg_id,
         send_msg.msg_len,
         send_msg.data_len,
         send_msg.data_pool);
  // Send the message
  int send_retval = ipc->tx_send_msg(ipc, &send_msg);
  if (send_retval != 0) {
    NVLOGE(TAG, "%s error: send TX message failed Error: %d\n", __func__, send_retval);
    printf("%s error: send TX message failed Error: %d\n", __func__, send_retval);
    ipc->tx_release(ipc, &send_msg);
    return send_retval;
  }

  ipc->notify(ipc, 1); // notify that there's 1 message in queue
  return 0;
}

// Always allocate message buffer, but allocate data buffer only when data_len > 0
static int test_nv_ipc_recv_msg(nv_ipc_t *ipc, nv_ipc_msg_t *recv_msg)
{
  if (ipc == NULL) {
    return -1;
  }
  recv_msg->msg_buf = NULL;
  recv_msg->data_buf = NULL;

  // Allocate buffer for TX message
  if (ipc->rx_recv_msg(ipc, recv_msg) < 0) {
    NVLOGV(TAG, "%s: no more message available\n", __func__);
    return -1;
  }
  NVLOGD(TAG,
         "recv: cell_id=%d msg_id=0x%02X msg_len=%d data_len=%d data_pool=%d\n",
         recv_msg->cell_id,
         recv_msg->msg_id,
         recv_msg->msg_len,
         recv_msg->data_len,
         recv_msg->data_pool);
  ipc_handle_rx_msg(ipc, recv_msg);

  // Release buffer of RX message
  int release_retval = ipc->rx_release(ipc, recv_msg);
  if (release_retval != 0) {
    NVLOGE(TAG, "%s error: release RX buffer failed Error: %d\n", __func__, release_retval);
    printf("%s error: release RX buffer failed Error: %d\n", __func__, release_retval);
    return release_retval;
  }
  return 0;
}

int is_tti_end(nv_ipc_msg_t *msg)
{
  if (msg != NULL && (msg->msg_id == FAPI_SLOT_INDATION || msg->msg_id == FAPI_DL_TTI_REQUEST)) {
    return 1;
  } else {
    return 0;
  }
}
bool recv_task_running = false;
// Test receiver task
void *epoll_recv_task(void *arg)
{
  struct epoll_event ev, events[MAX_EVENTS];
  printf("epoll recv task \n");
  int epoll_fd = epoll_create1(0);
  if (epoll_fd == -1) {
    NVLOGE(TAG, "%s epoll_create failed\n", __func__);
  }

  int ipc_rx_event_fd = ipc->get_fd(ipc);
  ev.events = EPOLLIN;
  ev.data.fd = ipc_rx_event_fd;
  if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, ev.data.fd, &ev) == -1) {
    NVLOGE(TAG, "%s epoll_ctl failed\n", __func__);
  }

  while (1) {
    if (!recv_task_running) {
      recv_task_running = true;
    }
    NVLOGI(TAG, "%s: epoll_wait fd_rx=%d ...\n", __func__, ipc_rx_event_fd);

    int nfds;
    do {
      // epoll_wait() may return EINTR when get unexpected signal SIGSTOP from system
      nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
    } while (nfds == -1 && errno == EINTR);

    if (nfds < 0) {
      NVLOGE(TAG, "epoll_wait failed: epoll_fd=%d nfds=%d err=%d - %s\n", epoll_fd, nfds, errno, strerror(errno));
    }

    NVLOGC(TAG, "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<\n");
    int n = 0;
    for (n = 0; n < nfds; ++n) {
      if (events[n].data.fd == ipc_rx_event_fd) {
        ipc->get_value(ipc);
        nv_ipc_msg_t recv_msg;
        while (test_nv_ipc_recv_msg(ipc, &recv_msg) == 0) {
          if (module_type == NV_IPC_MODULE_PRIMARY && is_tti_end(&recv_msg) && TEST_DUPLEX_TRANSFER) {
            // epoll_send_task();
          }
        }
      }
    }
  }
  close(epoll_fd);
  return NULL;
}

int create_recv_thread(void)
{
  pthread_t thread_id;

  void *(*recv_task)(void *);

  recv_task = epoll_recv_task;

  // epoll_recv_task
  int ret = pthread_create(&thread_id, NULL, recv_task, NULL);
  if (ret != 0) {
    NVLOGE(TAG, "%s failed, ret = %d\n", __func__, ret);
  }
  // set_priority(79);
  return ret;
}

int load_hard_code_config(nv_ipc_config_t *config, int primary, nv_ipc_transport_t _transport)
{
  // Create configuration
  config->ipc_transport = _transport;
  if (set_nv_ipc_default_config(config, module_type) < 0) {
    NVLOGE(TAG, "%s: set configuration failed\n", __func__);
    return -1;
  }

#ifdef NVIPC_CUDA_ENABLE
  test_cuda_device_id = get_cuda_device_id();
#else
  test_cuda_device_id = -1;
#endif
  printf("CUDA device ID configured : %d \n", test_cuda_device_id);
  config->transport_config.shm.cuda_device_id = test_cuda_device_id;
  if (test_cuda_device_id >= 0) {
    config->transport_config.shm.mempool_size[NV_IPC_MEMPOOL_CUDA_DATA].pool_len = 128; // was 128
    config->transport_config.shm.mempool_size[NV_IPC_MEMPOOL_CPU_DATA].pool_len = 1024;
    config->transport_config.shm.mempool_size[NV_IPC_MEMPOOL_CPU_MSG].pool_len = 4096;
  }

  return 0;
}

int nvIPC_Init() {
  int module, transport;
// Want to use transport SHM, type epoll, module secondary (reads the created shm from cuphycontroller)

  transport = 1;
  module = 0;

  /*
      if(argc < 4 || (transport = atoi(argv[1])) < 0 || (blocking_flag = atoi(argv[2])) < 0 || (primary = atoi(argv[3])) < 0)
      {
          fprintf(stderr, "Usage: test_ipc <transport> <blocking_flag> <module>\n");
          fprintf(stderr, "    transport:      0 - UDP;    1 - SHM;    2 - DPDK;    3 - Config by YAML\n");
          fprintf(stderr, "    blocking_flag:  0 - epoll;  1 - blocking.\n");
          fprintf(stderr, "    module:         0 - secondary;  1 - primary.\n");
          exit(1);
      }
      else
      {
          NVLOGC("INIT", "%s: argc=%d, blocking=%d, transport=%d, module_type=%d\n", __func__, argc, blocking_flag, transport,
     primary);
      }
  */
  ipc_transport = NV_IPC_TRANSPORT_SHM;
  module_type = NV_IPC_MODULE_SECONDARY;
  sprintf(TAG, "OAI");
  load_hard_code_config(&nv_ipc_config, module, ipc_transport);
  nvlog_c_init("/tmp/test_ipc_secondary.log");
  nvlog_open(module, "phy", NULL);
  nvlog_set_shm_log_level(NULL, NVLOG_ERROR);
  nvlog_set_console_log_level(NULL, NVLOG_ERROR);
  // Create nv_ipc_t instance
  if ((ipc = create_nv_ipc_interface(&nv_ipc_config)) == NULL) {
    NVLOGE(TAG, "%s: create IPC interface failed\n", __func__);
    return -1;
  }
  NVLOGI(TAG,"%s: create IPC interface successful\n", __func__);
  sleep(1);
  create_recv_thread();
  while(!recv_task_running){usleep(100000);}
  aerial_pnf_nr_connection_indication_cb(vnf_config, 1);
  return 0;
}
