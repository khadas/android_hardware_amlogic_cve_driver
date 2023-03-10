// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (C) 2019-2024 Amlogic, Inc. All rights reserved.
 *
 */

#ifndef __CVE_H__
#define __CVE_H__

#include "aml_comm_cve.h"
#include "aml_common.h"
#include "aml_cve.h"
#include "aml_debug.h"
#include "cve_reg.h"

#define AML_MOD_CVE "cve"

#define CVE_EMERG_TRACE(fmt, ...)                                                                  \
    do {                                                                                           \
        AML_EMERG_TRACE("[Func]:%s [Line]:%d [Emerg]:" fmt, __FUNCTION__, __LINE__,                \
                        ##__VA_ARGS__);                                                            \
    } while (0)

#define CVE_ALERT_TRACE(fmt, ...)                                                                  \
    do {                                                                                           \
        AML_ALERT_TRACE("[Func]:%s [Line]:%d [Alert]:" fmt, __FUNCTION__, __LINE__,                \
                        ##__VA_ARGS__);                                                            \
    } while (0)

#define CVE_CRIT_TRACE(fmt, ...)                                                                   \
    do {                                                                                           \
        AML_CRIT_TRACE("[Func]:%s [Line]:%d [Crit]:" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
    } while (0)

#define CVE_ERR_TRACE(fmt, ...)                                                                    \
    do {                                                                                           \
        AML_ERR_TRACE("[Func]:%s [Line]:%d [Error]:" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
    } while (0)

#define CVE_WARN_TRACE(fmt, ...)                                                                   \
    do {                                                                                           \
        AML_WARN_TRACE("[Func]:%s [Line]:%d [Warning]:" fmt, __FUNCTION__, __LINE__,               \
                       ##__VA_ARGS__);                                                             \
    } while (0)

#define CVE_NOTICE_TRACE(fmt, ...)                                                                 \
    do {                                                                                           \
        AML_NOTICE_TRACE("[Func]:%s [Line]:%d [Notice]:" fmt, __FUNCTION__, __LINE__,              \
                         ##__VA_ARGS__);                                                           \
    } while (0)

#define CVE_INFO_TRACE(fmt, ...)                                                                   \
    do {                                                                                           \
        AML_INFO_TRACE("[Func]:%s [Line]:%d [Info]:" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);  \
    } while (0)

#define CVE_DEBUG_TRACE(fmt, ...)                                                                  \
    do {                                                                                           \
        AML_DEBUG_TRACE("[Func]:%s [Line]:%d [Debug]:" fmt, __FUNCTION__, __LINE__,                \
                        ##__VA_ARGS__);                                                            \
    } while (0)

#define PLATFORM_CVE_MINOR_BASE 17

#define CVE_ALIGN_DOWN(x, g) (((x) / (g)) * (g))
#define CVE_ALIGN_UP(x, g) ((((x) + (g)-1) / (g)) * (g))
#define CVE_ALIGN 16

#define CVE_REG_ADDR_OFFSET(reg) ((((reg)-CVE_REG_BASE) << 2))

#define HIST_OUTPUT_PROCESS_LUT (1 << 0)
#define NCC_OUTPUT_PROCESS_REG (1 << 1)
#define CCL_OUTPUT_PROCESS_REG (1 << 2)
#define MATCH_BG_MODEL_OUTPUT_PROCESS_REG (1 << 3)
#define UPDATE_BG_MODEL_OUTPUT_PROCESS_REG (1 << 4)
#define LK_OPTICAL_FLOWPYR_OUTPUT_PROCESS_REG (1 << 5)
#define STCORNER_OUTPUT_PROCESS_REG (1 << 6)
#define CANNY_OUTPUT_PROCESS_REG (1 << 7)
#define TOF_OUTPUT_PROCESS_LUT (1 << 8)

#define MAP_INPUT_PROCESS_LUT (1 << 0)
#define EQUALIZE_HIST_INPUT_PROCESS_LUT (1 << 1)
#define TOF_INPUT_PROCESS_LUT (1 << 2)

#define CVE_SRC_MAX 8
#define CVE_DST_MAX 5
#define CVE_LUMA_MULTI_CMD_MAX CVE_LUMA_RECT_MAX

#define IOC_TYPE_CVE 'F'
#define CVE_OP_DMA _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_DMA, CVE_OP_DMA_T)
#define CVE_OP_LUMA_STAT _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_LUMA_STAT, CVE_OP_LUAM_STAT_ARRAY_T)
#define CVE_OP_FILTER _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_FILTER, CVE_OP_FILTER_T)
#define CVE_OP_CSC _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_CSC, CVE_OP_CSC_T)
#define CVE_OP_FILTER_AND_CSC                                                                      \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_FILTER_AND_CSC, CVE_OP_FILTER_AND_CSC_T)
#define CVE_OP_DILATE _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_DILATE, CVE_OP_DILATE_T)
#define CVE_OP_ERODE _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_ERODE, CVE_OP_ERODE_T)
#define CVE_OP_THRESH _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_THRESH, CVE_OP_THRESH_T)
#define CVE_OP_SOBEL _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_SOBEL, CVE_OP_SOBEL_T)
#define CVE_OP_INTEG _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_INTEG, CVE_OP_INTEG_T)
#define CVE_OP_HIST _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_HIST, CVE_OP_HIST_T)
#define CVE_OP_NCC _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_NCC, CVE_OP_NCC_T)
#define CVE_OP_THRESH_S16 _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_THRESH_S16, CVE_OP_THRESH_S16_T)
#define CVE_OP_THRESH_U16 _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_THRESH_U16, CVE_OP_THRESH_U16_T)
#define CVE_OP_ORD_STAT_FILTER                                                                     \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_ORD_STAT_FILTER, CVE_OP_ORD_STAT_FILTER_T)
#define CVE_OP_16BIT_TO_8BIT                                                                       \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_16BIT_TO_8BIT, CVE_OP_16BIT_TO_8BIT_T)
#define CVE_OP_MATCH_BG_MODEL                                                                      \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_MATCH_BG_MODEL, CVE_OP_MATCH_BG_MODEL_T)
#define CVE_OP_UPDATE_BG_MODEL                                                                     \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_UPDATE_BG_MODEL, CVE_OP_UPDATE_BG_MODEL_T)
#define CVE_OP_GRAD_FG _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_GRAD_FG, CVE_OP_GRAD_FG_T)
#define CVE_OP_LBP _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_LBP, CVE_OP_LBP_T)
#define CVE_OP_GMM _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_GMM, CVE_OP_GMM_T)
#define CVE_OP_EQUALIZE_HIST                                                                       \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_EQUALIZE_HIST, CVE_OP_EQUALIZE_HIST_T)
#define CVE_OP_CCL _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_CCL, CVE_OP_CCL_T)
#define CVE_OP_CANNY_HYS_EDGE                                                                      \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_CANNY_HYS_EDGE, CVE_OP_CANNY_HYS_EDGE_T)
#define CVE_OP_ST_CANDI_CORNER                                                                     \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_ST_CANDI_CORNER, CVE_OP_ST_CANDI_CORNER_T)
#define CVE_OP_SAD _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_SAD, CVE_OP_SAD_T)
#define CVE_OP_LK_OPTICAL_FLOW_PYR                                                                 \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_LK_OPTICAL_FLOW_PYR, CVE_OP_LK_OPTICAL_FLOW_PYR_T)
#define CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR                                                           \
    _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR, CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR_T)
#define CVE_OP_MAP _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_MAP, CVE_OP_MAP_T)
#define CVE_OP_NORM_GRAD _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_NORM_GRAD, CVE_OP_NORM_GRAD_T)
#define CVE_OP_AND _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_AND, CVE_OP_AND_T)
#define CVE_OP_OR _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_OR, CVE_OP_OR_T)
#define CVE_OP_XOR _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_XOR, CVE_OP_XOR_T)
#define CVE_OP_SUB _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_SUB, CVE_OP_SUB_T)
#define CVE_OP_ADD _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_ADD, CVE_OP_ADD_T)
#define CVE_OP_MAG_AND_ANG _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_MAG_AND_ANG, CVE_OP_MAG_AND_ANG_T)
#define CVE_OP_TOF _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_TOF, CVE_OP_TOF_T)
#define CVE_OP_QUERY _IOWR(IOC_TYPE_CVE, IOC_NR_CVE_OP_QUERY, CVE_OP_QUERY_T)

#define CVE_CHECK_POINTER(ptr)                                                                     \
    do {                                                                                           \
        if (ptr == NULL) {                                                                         \
            AML_ERR_TRACE("Null Pointer!\n");                                                      \
            return AML_ERR_CVE_NULL_PTR;                                                           \
        }                                                                                          \
    } while (0)

typedef enum {
    CVE_OP_TYPE_DMA = 0,
    CVE_OP_TYPE_FILTER = 1,
    CVE_OP_TYPE_CSC = 2,
    CVE_OP_TYPE_FILTER_AND_CSC = 3,
    CVE_OP_TYPE_SOBEL = 4,
    CVE_OP_TYPE_MAG_AND_ANG = 5,
    CVE_OP_TYPE_MATCH_BG_MODEL = 6,
    CVE_OP_TYPE_DILATE_AND_ERODE = 7,
    CVE_OP_TYPE_THRESH = 8,
    CVE_OP_TYPE_ALU = 9,
    CVE_OP_TYPE_INTEG = 10,
    CVE_OP_TYPE_HIST = 11,
    CVE_OP_TYPE_THRESH_S16 = 12,
    CVE_OP_TYPE_THRESH_U16 = 13,
    CVE_OP_TYPE_16BIT_TO_8BIT = 14,
    CVE_OP_TYPE_ORD_STAT_FILTER = 15,
    CVE_OP_TYPE_MAP = 16,
    CVE_OP_TYPE_EQUALIZE_HIST = 17,
    CVE_OP_TYPE_NCC = 18,
    CVE_OP_TYPE_CCL = 19,
    CVE_OP_TYPE_GMM = 20,
    CVE_OP_TYPE_CANNY_HYS_EDGE = 21,
    CVE_OP_TYPE_LBP = 22,
    CVE_OP_TYPE_NROM_GRAD = 23,
    CVE_OP_TYPE_BULID_LK_OPTICAL_FLOW_PYR = 24,
    CVE_OP_TYPE_LK_OPTIAL_FLOW_PRY = 25,
    CVE_OP_TYPE_ST_CANDI_CORNER = 26,
    CVE_OP_TYPE_SAD = 27,
    CVE_OP_TYPE_GRAD_FG = 28,
    CVE_OP_TYPE_UPDATE_BG_MODEL = 29,
    CVE_OP_TYPE_TOF = 30,
    CVE_OP_TYPE_BUTT,
} cve_op_tpye_e;

typedef enum {
    IOC_NR_CVE_OP_DMA,
    IOC_NR_CVE_OP_LUMA_STAT,
    IOC_NR_CVE_OP_FILTER,
    IOC_NR_CVE_OP_CSC,
    IOC_NR_CVE_OP_FILTER_AND_CSC,
    IOC_NR_CVE_OP_DILATE,
    IOC_NR_CVE_OP_ERODE,
    IOC_NR_CVE_OP_THRESH,
    IOC_NR_CVE_OP_SOBEL,
    IOC_NR_CVE_OP_INTEG,
    IOC_NR_CVE_OP_HIST,
    IOC_NR_CVE_OP_NCC,
    IOC_NR_CVE_OP_THRESH_S16,
    IOC_NR_CVE_OP_THRESH_U16,
    IOC_NR_CVE_OP_ORD_STAT_FILTER,
    IOC_NR_CVE_OP_16BIT_TO_8BIT,
    IOC_NR_CVE_OP_MATCH_BG_MODEL,
    IOC_NR_CVE_OP_UPDATE_BG_MODEL,
    IOC_NR_CVE_OP_GRAD_FG,
    IOC_NR_CVE_OP_LBP,
    IOC_NR_CVE_OP_GMM,
    IOC_NR_CVE_OP_EQUALIZE_HIST,
    IOC_NR_CVE_OP_CCL,
    IOC_NR_CVE_OP_CANNY_HYS_EDGE,
    IOC_NR_CVE_OP_ST_CANDI_CORNER,
    IOC_NR_CVE_OP_SAD,
    IOC_NR_CVE_OP_LK_OPTICAL_FLOW_PYR,
    IOC_NR_CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR,
    IOC_NR_CVE_OP_MAP,
    IOC_NR_CVE_OP_NORM_GRAD,
    IOC_NR_CVE_OP_AND,
    IOC_NR_CVE_OP_OR,
    IOC_NR_CVE_OP_XOR,
    IOC_NR_CVE_OP_SUB,
    IOC_NR_CVE_OP_ADD,
    IOC_NR_CVE_OP_MAG_AND_ANG,
    IOC_NR_CVE_OP_TOF,
    IOC_NR_CVE_OP_QUERY,
} ioc_nr_cve_op_tpye_e;

typedef enum {
    CVE_ALU_SEL_SUB = 0,
    CVE_ALU_SEL_OR = 1,
    CVE_ALU_SEL_XOR = 2,
    CVE_ALU_SEL_AND = 3,
    CVE_ALU_SEL_ADD = 4,
} cve_alu_sel_e;

typedef struct {
    phys_addr_t phys_start;
    unsigned long virt_start;
    unsigned long mmap_start;
    unsigned int size;
    struct dma_buf *dma_buf;
    struct ion_buffer *ionbuffer;
    struct dma_buf_attachment *d_att;
} cve_ion_buffer_t;

typedef struct {
    cve_op_tpye_e op_type;
    unsigned int op_mode;
    unsigned int inp_flags;
    unsigned int outp_flags;
    unsigned int inp_size;
    unsigned int outp_size;
    unsigned long long inp_phys_addr;
    unsigned long long outp_phys_addr;
} cve_op_io_info_t;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_DATA_T stSrcDATA;
    CVE_DST_DATA_T stDstDATA;
    CVE_DMA_CTRL_T stDmaCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_DMA_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_MEM_INFO_T stDstMem;
    CVE_RECT_U16_T astCveLumaRect[CVE_LUMA_RECT_MAX];
    CVE_LUMA_STAT_ARRAY_CTRL_T stLumaStatArrayCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_LUAM_STAT_ARRAY_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_FILTER_CTRL_T stFilterCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_FILTER_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_CSC_CTRL_T stCscCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_CSC_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_FILTER_AND_CSC_CTRL_T stFilterCscCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_FILTER_AND_CSC_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_DILATE_CTRL_T stDilateCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_DILATE_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_ERODE_CTRL_T stErodeCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_ERODE_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_THRESH_CTRL_T stThreshCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_THRESH_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_EQUALIZE_HIST_CTRL_T stEqualizeHistCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_EQUALIZE_HIST_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_INTEG_CTRL_T stIntegCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_INTEG_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_MEM_INFO_T stDstMem;
    AML_BOOL_E bInstant;
} CVE_OP_HIST_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_DST_MEM_INFO_T stBlob;
    CVE_CCL_CTRL_T stCclCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_CCL_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrc;
    CVE_DST_IMAGE_T stLabel;
    CVE_DST_IMAGE_T stCandiCorner;
    CVE_DST_MEM_INFO_T stCandiCornerPoint;
    CVE_ST_CANDI_CORNER_CTRL_T stStCandiCornerCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_ST_CANDI_CORNER_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage1;
    CVE_SRC_IMAGE_T stSrcImage2;
    CVE_DST_MEM_INFO_T stDstmem;
    CVE_NCC_CTRL_T stNccCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_NCC_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_THRESH_S16_CTRL_T stThreshS16Ctrl;
    AML_BOOL_E bInstant;
} CVE_OP_THRESH_S16_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_THRESH_U16_CTRL_T stThreshU16Ctrl;
    AML_BOOL_E bInstant;
} CVE_OP_THRESH_U16_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_LBP_CTRL_T stLbpCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_LBP_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_ORD_STAT_FILTER_CTRL_T stOrdStatFltCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_ORD_STAT_FILTER_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstImage;
    CVE_16BIT_TO_8BIT_CTRL_T st16BitTo8BitCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_16BIT_TO_8BIT_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stCurImg;
    CVE_SRC_IMAGE_T stPreImg;
    CVE_MEM_INFO_T stBgModel;
    CVE_DST_IMAGE_T stFg;
    CVE_DST_IMAGE_T stBg;
    CVE_DST_IMAGE_T stCurDiffBg;
    CVE_DST_IMAGE_T stFrmDiff;
    CVE_DST_MEM_INFO_T stStatData;
    CVE_MATCH_BG_MODEL_CTRL_T stMatchBgModelCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_MATCH_BG_MODEL_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stCurImg;
    CVE_MEM_INFO_T stBgModel1;
    CVE_MEM_INFO_T stBgModel2;
    CVE_DST_MEM_INFO_T stStatData;
    CVE_UPDATE_BG_MODEL_CTRL_T stUpdateBgModelCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_UPDATE_BG_MODEL_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stFg;
    CVE_SRC_IMAGE_T stCurGrad;
    CVE_SRC_IMAGE_T stBgGrad;
    CVE_DST_IMAGE_T stGradFg;
    CVE_GRAD_FG_CTRL_T stGradFgCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_GRAD_FG_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_SRC_IMAGE_T stFactor;
    CVE_DST_IMAGE_T stFg;
    CVE_DST_IMAGE_T stBg;
    CVE_MEM_INFO_T stModel;
    CVE_GMM_CTRL_T stGmmCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_GMM_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stEdge;
    CVE_DST_MEM_INFO_T stStack;
    CVE_CANNY_HYS_EDGE_CTRL_T stCannyHysEdgeCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_CANNY_HYS_EDGE_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage1;
    CVE_SRC_IMAGE_T stSrcImage2;
    CVE_DST_IMAGE_T stSad;
    CVE_DST_IMAGE_T stThr;
    CVE_SAD_CTRL_T stSadCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_SAD_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T astSrcPrevPyr[4];
    CVE_SRC_IMAGE_T astSrcNextPyr[4];
    CVE_SRC_MEM_INFO_T stPrevPts;
    CVE_MEM_INFO_T stNextPts;
    CVE_DST_MEM_INFO_T stStatus;
    CVE_DST_MEM_INFO_T stErr;
    CVE_LK_OPTICAL_FLOW_PYR_CTRL_T stLkOptiFlowPyrCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_LK_OPTICAL_FLOW_PYR_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcPyr;
    CVE_DST_IMAGE_T astDstPyr[4];
    CVE_BUILD_LK_OPTICAL_FLOW_PYR_CTRL_T stLkBuildOptiFlowPyrCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_SRC_MEM_INFO_T stMap;
    CVE_DST_IMAGE_T stDstImage;
    CVE_MAP_CTRL_T stMapCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_MAP_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstMag;
    CVE_DST_IMAGE_T stDstAng;
    CVE_MAG_AND_ANG_CTRL_T stMagAndAngCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_MAG_AND_ANG_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage1;
    CVE_SRC_IMAGE_T stSrcImage2;
    CVE_DST_IMAGE_T stDst;
    AML_BOOL_E bInstant;
} CVE_OP_AND_T;

typedef CVE_OP_AND_T CVE_OP_OR_T;
typedef CVE_OP_AND_T CVE_OP_XOR_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage1;
    CVE_SRC_IMAGE_T stSrcImage2;
    CVE_DST_IMAGE_T stDst;
    CVE_SUB_CTRL_T stSubCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_SUB_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage1;
    CVE_SRC_IMAGE_T stSrcImage2;
    CVE_DST_IMAGE_T stDst;
    CVE_ADD_CTRL_T stAddCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_ADD_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstH;
    CVE_DST_IMAGE_T stDstV;
    CVE_DST_IMAGE_T stDstHV;
    CVE_NORM_GRAD_CTRL_T stNormGradCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_NORM_GRAD_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_IMAGE_T stSrcImage;
    CVE_DST_IMAGE_T stDstH;
    CVE_DST_IMAGE_T stDstV;
    CVE_SOBEL_CTRL_T stSobelCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_SOBEL_T;

typedef struct {
    CVE_HANDLE cveHandle;
    CVE_SRC_RAW_T stSrcRaw;
    CVE_SRC_RAW_T stSrcFpn;
    CVE_SRC_MEM_INFO_T stSrcCoef;
    CVE_SRC_MEM_INFO_T stBpc;
    CVE_DST_MEM_INFO_T stDtsStatus;
    CVE_DST_MEM_INFO_T stDtsIR;
    CVE_DST_MEM_INFO_T stDtsData;
    CVE_DST_MEM_INFO_T stDstHist;
    CVE_TOF_CTRL_T stTofCtrl;
    AML_BOOL_E bInstant;
} CVE_OP_TOF_T;

typedef struct {
    CVE_HANDLE cveHandle;
    AML_BOOL_E bBlock;
    AML_BOOL_E bFinish;
} CVE_OP_QUERY_T;

typedef struct {
    phys_addr_t phys_start;
    unsigned long virt_start;
    unsigned int size;
    int ion_fd;
    int map_fd;
} CVE_OP_ION_BUFFER_T;

typedef struct {
    unsigned int src_addr[CVE_SRC_MAX];
    unsigned int dst_addr[CVE_DST_MAX];
    unsigned int src_stride[CVE_SRC_MAX];
    unsigned int dst_stride[CVE_DST_MAX];
    unsigned short src_width;
    unsigned short src_height;
    unsigned short dst_width;
    unsigned short dst_height;
    unsigned short xstart;
    unsigned short ystart;
    unsigned short xSize;
    unsigned short ySize;
} cve_comm_init_params_t;

typedef struct {
    int task_id;
    unsigned long virt_addr;
    unsigned long phys_addr;
    unsigned int total_cmd_num;
    unsigned int total_cmd_line_num;
    AML_BOOL_E bInput;
    AML_BOOL_E bOutput;
    AML_BOOL_E bInvalid;
    unsigned int input_process_flags;
    unsigned int output_process_flags;
    struct list_head cmd_list;
} cve_task_desc_t;

typedef struct {
    cve_op_io_info_t io_info;
    CVE_HANDLE cveHandle;
    unsigned long virt_addr;
    unsigned long phys_addr;
    unsigned int cmd_line_num;
    unsigned int instant;
    struct list_head list;
    cve_task_desc_t *task_desc;
} cve_cmd_desc_t;

typedef struct {
    unsigned long phys_start;
    unsigned long virt_start;
    unsigned long task_phys_offset;
    unsigned long task_virt_offset;
    unsigned int length;
    unsigned int total_cmd_max;
    unsigned int task_max;
    unsigned int cur_cmd_id;
    unsigned int end_cmd_id;
    unsigned int task_instant;
    cve_task_desc_t *task_descs;
    unsigned int task_descs_create_index;
    unsigned int task_descs_invoke_index;
    cve_cmd_desc_t *cmd_descs;
    unsigned int cmd_descs_index;
} cve_cq_desc_t;

typedef struct {
    union CVE_COMMON_CTRL_REG0_02 reg_02;
    union CVE_RDMIF_PACK_MODE_REG_D8 reg_d8;
    union CVE_RDMIF1_PACK_MODE_REG_E0 reg_e0;
    union CVE_RDMIF2_PACK_MODE_REG_E8 reg_e8;
    union CVE_WRMIF_PACK_MODE_REG_C0 reg_c0;
    union CVE_WRMIF1_PACK_MODE_REG_C8 reg_c8;
    union CVE_WRMIF2_PACK_MODE_REG_D0 reg_d0;
    union CVE_COMMON_CTRL_REG1_0_03 reg_03;
    union CVE_COMMON_CTRL_REG1_1_04 reg_04;
    union CVE_COMMON_CTRL_REG1_2_05 reg_05;
    union CVE_COMMON_CTRL_REG1_3_06 reg_06;
    union CVE_COMMON_CTRL_REG1_4_07 reg_07;
    union CVE_COMMON_CTRL_REG1_5_08 reg_08;
    union CVE_COMMON_CTRL_REG1_6_09 reg_09;
    union CVE_COMMON_CTRL_REG1_7_0a reg_0a;
    union CVE_COMMON_CTRL_REG2_0_0b reg_0b;
    union CVE_COMMON_CTRL_REG2_1_0c reg_0c;
    union CVE_COMMON_CTRL_REG2_2_0d reg_0d;
    union CVE_COMMON_CTRL_REG2_3_0e reg_0e;
    union CVE_COMMON_CTRL_REG2_4_0f reg_0f;
    union CVE_COMMON_CTRL_REG3_0_10 reg_10;
    union CVE_COMMON_CTRL_REG3_1_11 reg_11;
    union CVE_COMMON_CTRL_REG3_2_12 reg_12;
    union CVE_COMMON_CTRL_REG3_3_13 reg_13;
    union CVE_COMMON_CTRL_REG4_0_14 reg_14;
    union CVE_COMMON_CTRL_REG4_1_15 reg_15;
    union CVE_COMMON_CTRL_REG5_16 reg_16;
    union CVE_COMMON_CTRL_REG6_17 reg_17;
    union CVE_COMMON_CTRL_REG7_18 reg_18;
    union CVE_COMMON_CTRL_REG8_19 reg_19;
    union CVE_COMMON_CTRL_REG9_1A reg_1a;
} cve_comm_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_DMA_REG0_1B reg_1b;
    union CVE_DMA_REG1_1C reg_1c;
    union CVE_DMA_REG2_1D reg_1d;
    union CVE_DMA_REG3_1E reg_1e;
} cve_op_dma_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_ALU_REG0_31 alu_31;
    union CVE_ALU_REG1_32 alu_32;
    union CVE_SUB_THRESH_RATIO_99 sub_99;
} cve_op_alu_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_FILTER_REG2_1F filter_1f;
    union CVE_FILTER_REG1_0_20 filter_20;
    union CVE_FILTER_REG1_1_21 filter_21;
    union CVE_FILTER_REG2_3_22 filter_22;
    union CVE_FILTER_REG0_0_67 filter_67;
    union CVE_FILTER_REG0_1_68 filter_68;
    union CVE_FILTER_REG0_2_69 filter_69;
    union CVE_FILTER_REG0_3_6A filter_6a;
    union CVE_FILTER_REG0_4_6B filter_6b;
    union CVE_FILTER_REG0_5_6C filter_6c;
    union CVE_FILTER_REG0_6_6D filter_6d;
} cve_op_filter_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_CSC_REG0_23 csc_23;
    union CVE_CSC_REG_24 csc_24;
    union CVE_CSC_REG1_6E csc_6e;
    union CVE_CSC_REG1_1_6F csc_6f;
    union CVE_CSC_REG2_0_70 csc_70;
    union CVE_CSC_REG2_1_71 csc_71;
    union CVE_CSC_REG2_2_72 csc_72;
    union CVE_CSC_REG3_73 csc_73;
    union CVE_CSC_REG3_1_74 csc_74;
} cve_op_csc_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_CSC_REG0_23 csc_23;
    union CVE_CSC_REG_24 csc_24;
    union CVE_CSC_REG1_6E csc_6e;
    union CVE_CSC_REG1_1_6F csc_6f;
    union CVE_CSC_REG2_0_70 csc_70;
    union CVE_CSC_REG2_1_71 csc_71;
    union CVE_CSC_REG2_2_72 csc_72;
    union CVE_CSC_REG3_73 csc_73;
    union CVE_CSC_REG3_1_74 csc_74;
    union CVE_FILTER_REG0_0_67 filter_67;
    union CVE_FILTER_REG0_1_68 filter_68;
    union CVE_FILTER_REG0_2_69 filter_69;
    union CVE_FILTER_REG0_3_6A filter_6a;
    union CVE_FILTER_REG0_4_6B filter_6b;
    union CVE_FILTER_REG0_5_6C filter_6c;
    union CVE_FILTER_REG0_6_6D filter_6d;
} cve_op_filter_and_csc_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_SOBEL_REG0_25 sobel_25;
    union CVE_SOBEL_REG1_0_75 sobel_75;
    union CVE_SOBEL_REG1_1_76 sobel_76;
    union CVE_SOBEL_REG1_2_77 sobel_77;
    union CVE_SOBEL_REG1_3_78 sobel_78;
    union CVE_SOBEL_REG1_4_79 sobel_79;
    union CVE_SOBEL_REG1_5_7A sobel_7a;
    union CVE_SOBEL_REG1_6_7B sobel_7b;
    union CVE_SOBEL_REG2_0_7C sobel_7c;
    union CVE_SOBEL_REG2_1_7D sobel_7d;
    union CVE_SOBEL_REG2_2_7E sobel_7e;
    union CVE_SOBEL_REG2_3_7F sobel_7f;
    union CVE_SOBEL_REG2_4_80 sobel_80;
    union CVE_SOBEL_REG2_5_81 sobel_81;
    union CVE_SOBEL_REG2_6_82 sobel_82;
} cve_op_sobel_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_ERODEDILATE_REG0_2E erode_dilate_2E;
    union CVE_FILTER_REG0_0_67 filter_67;
    union CVE_FILTER_REG0_1_68 filter_68;
    union CVE_FILTER_REG0_2_69 filter_69;
    union CVE_FILTER_REG0_3_6A filter_6a;
    union CVE_FILTER_REG0_4_6B filter_6b;
    union CVE_FILTER_REG0_5_6C filter_6c;
    union CVE_FILTER_REG0_6_6D filter_6d;
} cve_op_erode_dilate_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_THRESH_REG0_2F thresh_2f;
    union CVE_THRESH_REG1_30 thresh_30;
} cve_op_thresh_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_THRESHS16_REG0_35 thresh_s16_35;
    union CVE_THRESHS16_REG1_36 thresh_s16_36;
} cve_op_thresh_s16_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_THRESHU16_REG0_37 thresh_u16_37;
    union CVE_THRESHU16_REG1_38 thresh_u16_38;
} cve_op_thresh_u16_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_INTEG_REG0_33 integ_33;
} cve_op_integ_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_INTEG_REG0_33 integ_33;
    union CVE_EQHIST_REG0_34 eqhist_34;
} cve_op_hist_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_16BITTO8BIT_REG0_39 _16bit_to_8bit_39;
    union CVE_16BITTO8BIT_REG1_3A _16bit_to_8bit_3a;
} cve_op_16bit_to_8bit_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_STATFILTER_REG0_3B stat_filter_3b;
} cve_op_ord_stat_filter_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_NCC_REG0_3C ncc_3c;
} cve_op_ncc_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_CANNY_REG0_4A canny_4a;
    union CVE_CANNY_REG2_4C canny_4c;
    union CVE_SOBEL_REG1_0_75 sobel_75;
    union CVE_SOBEL_REG1_1_76 sobel_76;
    union CVE_SOBEL_REG1_2_77 sobel_77;
    union CVE_SOBEL_REG1_3_78 sobel_78;
    union CVE_SOBEL_REG1_4_79 sobel_79;
    union CVE_SOBEL_REG1_5_7A sobel_7a;
    union CVE_SOBEL_REG1_6_7B sobel_7b;
    union CVE_SOBEL_REG2_0_7C sobel_7c;
    union CVE_SOBEL_REG2_1_7D sobel_7d;
    union CVE_SOBEL_REG2_2_7E sobel_7e;
    union CVE_SOBEL_REG2_3_7F sobel_7f;
    union CVE_SOBEL_REG2_4_80 sobel_80;
    union CVE_SOBEL_REG2_5_81 sobel_81;
    union CVE_SOBEL_REG2_6_82 sobel_82;
} cve_op_canny_edge_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_LBP_REG0_4D lbp_4d;
} cve_op_lbp_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_MAP_REG0_3C map_3c;
} cve_op_map_params_t;

typedef struct {
    cve_comm_params_t comm_params;
} cve_op_ccl_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_GMM_REG0_44 gmm_44;
    union CVE_GMM_REG1_45 gmm_45;
    union CVE_GMM_REG2_46 gmm_46;
    union CVE_GMM_REG3_47 gmm_47;
    union CVE_GMM_REG4_48 gmm_48;
    union CVE_GMM_REG5_49 gmm_49;
} cve_op_gmm_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_NORMGRAD_RETG0_4E norm_grad_4e;
    union CVE_SOBEL_REG1_0_75 sobel_75;
    union CVE_SOBEL_REG1_1_76 sobel_76;
    union CVE_SOBEL_REG1_2_77 sobel_77;
    union CVE_SOBEL_REG1_3_78 sobel_78;
    union CVE_SOBEL_REG1_4_79 sobel_79;
    union CVE_SOBEL_REG1_5_7A sobel_7a;
    union CVE_SOBEL_REG1_6_7B sobel_7b;
    union CVE_SOBEL_REG2_0_7C sobel_7c;
    union CVE_SOBEL_REG2_1_7D sobel_7d;
    union CVE_SOBEL_REG2_2_7E sobel_7e;
    union CVE_SOBEL_REG2_3_7F sobel_7f;
    union CVE_SOBEL_REG2_4_80 sobel_80;
    union CVE_SOBEL_REG2_5_81 sobel_81;
    union CVE_SOBEL_REG2_6_82 sobel_82;
} cve_op_norm_grad_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_SAD_REG0_64 sad_64;
    union CVE_SAD_REG1_65 sad_65;
} cve_op_sad_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_GRADFG_RETG0_66 grad_fg_66;
} cve_op_grad_fg_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_MAGANDANG_REG0_26 mag_and_ang_26;
    union CVE_SOBEL_REG1_0_75 sobel_75;
    union CVE_SOBEL_REG1_1_76 sobel_76;
    union CVE_SOBEL_REG1_2_77 sobel_77;
    union CVE_SOBEL_REG1_3_78 sobel_78;
    union CVE_SOBEL_REG1_4_79 sobel_79;
    union CVE_SOBEL_REG1_5_7A sobel_7a;
    union CVE_SOBEL_REG1_6_7B sobel_7b;
    union CVE_SOBEL_REG2_0_7C sobel_7c;
    union CVE_SOBEL_REG2_1_7D sobel_7d;
    union CVE_SOBEL_REG2_2_7E sobel_7e;
    union CVE_SOBEL_REG2_3_7F sobel_7f;
    union CVE_SOBEL_REG2_4_80 sobel_80;
    union CVE_SOBEL_REG2_5_81 sobel_81;
    union CVE_SOBEL_REG2_6_82 sobel_82;
} cve_op_mag_and_ang_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_BGMODE_REG0_27 bg_mode_27;
} cve_op_match_bg_model_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_UPDATEBGMODE_REG0_2a update_bg_mode_2a;
    union CVE_UPDATEBGMODE_REG1_2b update_bg_mode_2b;
    union CVE_BGMODE_REG0_27 bg_mode_27;
} cve_op_update_bg_model_params_t;

typedef struct {
    cve_comm_params_t comm_params;
} cve_op_st_candi_corner_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_BDLK_REG0_4F bdlk_4f;
} cve_op_build_lk_optical_flow_pyr_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_LK_REG0_50 lk_50;
    union CVE_LK_REG1_51 lk_51;
} cve_op_lk_optical_flow_pyr_params_t;

typedef struct {
    cve_comm_params_t comm_params;
    union CVE_TOF_REG0_83 tof_83;
    union CVE_TOF_REG1_84 tof_84;
    union CVE_TOF_REG3_0_85 tof_85;
    union CVE_TOF_REG3_1_86 tof_86;
    union CVE_TOF_REG3_2_87 tof_87;
    union CVE_TOF_REG3_3_88 tof_88;
    union CVE_TOF_REG3_4_89 tof_89;
    union CVE_TOF_REG4_8a tof_8a;
    union CVE_TOF_REG5_8b tof_8b;
    union CVE_TOF_REG9_0_8c tof_8c;
    union CVE_TOF_REG9_1_8d tof_8d;
    union CVE_TOF_REG9_2_8e tof_8e;
    union CVE_TOF_REG9_3_8f tof_8f;
    union CVE_TOF_REG9_4_90 tof_90;
    union CVE_TOF_REG10_91 tof_91;
    union CVE_TOF_REG11_92 tof_92;
    union CVE_TOF_REG12_93 tof_93;
    union CVE_TOF_REG13_94 tof_94;
    union CVE_TOF_REG14_95 tof_95;
    union CVE_TOF_REG15_96 tof_96;
} cve_op_tof_params_t;

#endif /* __CVE_H__ */
