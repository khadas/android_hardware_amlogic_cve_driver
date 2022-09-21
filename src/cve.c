// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (C) 2019-2024 Amlogic, Inc. All rights reserved.
 */
#include "cve.h"
#include "aml_common.h"
#include "aml_cve.h"
#include <linux/clk.h>
#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/ion.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include <linux/arm-smccc.h>

typedef struct {
    int cmd_handle;
    unsigned int cmd_finish_cnt;
    unsigned int last_finish_cmd_id;
    unsigned int cur_finish_cmd_id;
    unsigned int cmd_handle_wrap;
    unsigned int finish_cmd_wrap;
} cve_task_info_t;

typedef struct {
    bool cve_dbg_run_cycles_en;
    bool cve_ccl_dbg_dis_sec_pass_en;
    bool cve_id_stat_en;
    bool cve_timeout_en;
    bool cve_ute_stat_en;
    unsigned int cve_ute_cycles;
} cve_run_time_cycle_t;

typedef struct {
    bool last_instant;
    unsigned int last_int_cnt_per_sec;
    unsigned int max_int_cnt_per_sec;
    unsigned int total_int_cnt_last_sec;
    unsigned int total_int_cnt;
    unsigned int query_timeout_cnt;
    unsigned int system_timeout_cnt;
    unsigned int last_int_cost_time;
    unsigned int int_cost_time_max;
    unsigned int last_persec_int_cost_time;
    unsigned int persec_int_cost_time_max;
    unsigned int total_int_cost_time;
    unsigned int total_cve_run_time;
    cve_run_time_cycle_t cve_cycle;
} cve_run_time_info_t;

typedef struct {
    unsigned int dma;
    unsigned int luma_stat;
    unsigned int filter;
    unsigned int csc;
    unsigned int filter_and_csc;
    unsigned int sobel;
    unsigned int mag_and_ang;
    unsigned int match_bg_model;
    unsigned int update_bg_model;
    unsigned int dilate;
    unsigned int erode;
    unsigned int thresh;
    unsigned int and;
    unsigned int or ;
    unsigned int xor ;
    unsigned int sub;
    unsigned int add;
    unsigned int integ;
    unsigned int hist;
    unsigned int thresh_s16;
    unsigned int thresh_u16;
    unsigned int _16bit_to_8bit;
    unsigned int ord_stat_filter;
    unsigned int map;
    unsigned int equalize_hist;
    unsigned int ncc;
    unsigned int ccl;
    unsigned int gmm;
    unsigned int canny_edge;
    unsigned int lbp;
    unsigned int nrom_grad;
    unsigned int bulid_lk_optical_flow_pyr;
    unsigned int lk_optial_flow_pry;
    unsigned int st_candi_corner;
    unsigned int sad;
    unsigned int grad_fg;
    unsigned int tof;
} cve_op_invoke_count_t;

typedef struct {
    unsigned long phys_start;
    unsigned long virt_start;
    unsigned int length;
    unsigned int cmd_size;
    unsigned int cur_index;
    unsigned int index_max;
    struct semaphore cmd_buf_sema;
} cve_cmd_buf_t;

typedef enum {
    CVE_STATUS_CQ0 = 0,
    CVE_STATUS_CQ1 = 1,
    CVE_STATUS_IDLE = 2,
} cve_queue_status_e;

typedef struct {
    cve_queue_status_e queue_wait;
    cve_queue_status_e queue_busy;
    cve_task_info_t task_info;
    cve_run_time_info_t run_time_info;
    cve_op_invoke_count_t invoke_count;
    struct semaphore cve_sema;
    struct wait_queue_head cve_wait;
    struct work_struct cve_wrok;
    cve_task_desc_t *task_desc_outp;
    cve_cq_desc_t cq_desc[2];
    cve_ion_buffer_t cve_cq_buffer;
    cve_cmd_buf_t cmd_bufs;
    cve_ion_buffer_t cve_cmd_buffer;
} cve_context_t;

typedef enum {
    CVE_SYS_STATUS_READY = 0,
    CVE_SYS_STATUS_STOP = 1,
    CVE_SYS_STATUS_IDLE = 2,
} cve_sys_status_e;

#define CMD_QUEUE_ADDR(op, reg, rbase, hmask, lmask)                                               \
    (((op) << OP_SHIFT) | (((reg + rbase) & 0x3ff) << ADDR_SHIFT) | (hmask << HMASK_SHIFT) |       \
     (lmask << LMASK_SHIFT))

#define CMD_QUEUE_DATE(val, hmask, lmask)                                                          \
    (val >> lmask & ((~(unsigned int)0 >> (31 - hmask + lmask))))

#define CMD_QUEUE_CREATE_PREPARE(op, buf)                                                          \
    unsigned int cmd_buf_offset = 0;                                                               \
    unsigned int cmd_op = op;

#define CMD_QUEUE_ADD_PROCESS(reg, reg_, val, n)                                                   \
    cmd_buf[cmd_buf_offset++] = CMD_QUEUE_DATE(val, reg_##HMASK##n, reg_##LMASK##n);               \
    cmd_buf[cmd_buf_offset++] =                                                                    \
        CMD_QUEUE_ADDR(cmd_op, reg, reg_##BASE, reg_##HMASK##n, reg_##LMASK##n);

#define CMD_QUEUE_ADD_PROCESS_1(reg, val) CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 0)

#define CMD_QUEUE_ADD_PROCESS_2(reg, val)                                                          \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 0)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 1)

#define CMD_QUEUE_ADD_PROCESS_3(reg, val)                                                          \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 0)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 1)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 2)

#define CMD_QUEUE_ADD_PROCESS_4(reg, val)                                                          \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 0)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 1)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 2)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 3)

#define CMD_QUEUE_ADD_PROCESS_5(reg, val)                                                          \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 0)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 1)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 2)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 3)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 4)

#define CMD_QUEUE_ADD_PROCESS_6(reg, val)                                                          \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 0)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 1)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 2)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 3)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 4)                                                     \
    CMD_QUEUE_ADD_PROCESS(reg, reg##_, val, 5)

#define CMD_QUEUE_DEBUG_DUMP(name)

#define CMD_QUEUE_ADD_COMMON(comm)                                                                 \
    CMD_QUEUE_ADD_PROCESS_6(CVE_COMMON_CTRL_REG0, comm.reg_02.reg);                                \
    CMD_QUEUE_ADD_PROCESS_1(CVE_RDMIF_PACK_MODE, comm.reg_d8.reg);                                 \
    CMD_QUEUE_ADD_PROCESS_1(CVE_RDMIF1_PACK_MODE, comm.reg_e0.reg);                                \
    CMD_QUEUE_ADD_PROCESS_1(CVE_RDMIF2_PACK_MODE, comm.reg_e8.reg);                                \
    CMD_QUEUE_ADD_PROCESS_1(CVE_WRMIF_PACK_MODE, comm.reg_c0.reg);                                 \
    CMD_QUEUE_ADD_PROCESS_1(CVE_WRMIF1_PACK_MODE, comm.reg_c8.reg);                                \
    CMD_QUEUE_ADD_PROCESS_1(CVE_WRMIF2_PACK_MODE, comm.reg_d0.reg);                                \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_0, comm.reg_03.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_1, comm.reg_04.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_2, comm.reg_05.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_3, comm.reg_06.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_4, comm.reg_07.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_5, comm.reg_08.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_6, comm.reg_09.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG1_7, comm.reg_0a.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG2_0, comm.reg_0b.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG2_1, comm.reg_0c.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG2_2, comm.reg_0d.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG2_3, comm.reg_0e.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG2_4, comm.reg_0f.reg);                              \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG3_0, comm.reg_10.reg);                              \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG3_1, comm.reg_11.reg);                              \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG3_2, comm.reg_12.reg);                              \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG3_3, comm.reg_13.reg);                              \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG4_0, comm.reg_14.reg);                              \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG4_1, comm.reg_15.reg);                              \
    CMD_QUEUE_ADD_PROCESS_1(CVE_COMMON_CTRL_REG5, comm.reg_16.reg);                                \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG6, comm.reg_17.reg);                                \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG7, comm.reg_18.reg);                                \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG8, comm.reg_19.reg);                                \
    CMD_QUEUE_ADD_PROCESS_2(CVE_COMMON_CTRL_REG9, comm.reg_1a.reg);

#define CMD_QUEUE_END(reg)                                                                         \
    cmd_buf[cmd_buf_offset++] = CMD_QUEUE_DATE(0x0, reg##_HMASK0, reg##_LMASK0);                   \
    cmd_buf[cmd_buf_offset++] =                                                                    \
        CMD_QUEUE_ADDR(cmd_op, reg, reg##_BASE, reg##_HMASK0, reg##_LMASK0);                       \
    cmd_buf[cmd_buf_offset++] = CMD_QUEUE_DATE(0x1, reg##_HMASK0, reg##_LMASK0);                   \
    cmd_buf[cmd_buf_offset++] =                                                                    \
        CMD_QUEUE_ADDR(cmd_op, reg, reg##_BASE, reg##_HMASK0, reg##_LMASK0);                       \
    cmd_buf[cmd_buf_offset++] = CMD_QUEUE_DATE(0x0, reg##_HMASK0, reg##_LMASK0);                   \
    cmd_buf[cmd_buf_offset++] = CMD_QUEUE_ADDR(0x3ff, reg, reg##_BASE, reg##_HMASK0, reg##_LMASK0);

#define CMD_QUEUE_CREATE_END CMD_QUEUE_END(CVE_TOP_CTRL_REG0)

#define CMD_QUEUE_RETURN return cmd_buf_offset >> 1;

#define CVE_OP_NODE_CMD_SIZE_MAX 1024
#define CMD_HANDLE_MAX 0x10000000
#define TASK_CMD_MAX 511
#define TASK_CMD_LINE_MAX 4095
#define TASK_CMD_LINE_SIZE 8
#define CVE_QUERY_TIMEOUT 1000
#define CVE_SYS_TIMEOUT 6000000

#define CVE_GET_HI64(x) (((x)&0xFFFFFFFF00000000ULL) >> 32)
#define CVE_GET_LO64(x) ((x)&0x00000000FFFFFFFFULL)
#define CVE_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CVE_MAX(a, b) ((a) > (b) ? (a) : (b))

typedef enum {
    CVE_DMA = 0,
    CVE_LUMA,
    CVE_FILTER,
    CVE_CSC,
    CVE_FILTER_AND_CSC,
    CVE_SOBEL,
    CVE_MAG_AND_ANG,
    CVE_MATCH_BG_MODEL,
    CVE_DILATE,
    CVE_ERODE,
    CVE_THRESH,
    CVE_ALU_SUB,
    CVE_ALU_OR,
    CVE_ALU_AND,
    CVE_ALU_XOR,
    CVE_ALU_ADD,
    CVE_INTEG,
    CVE_HIST,
    CVE_THRESH_S16,
    CVE_THRESH_U16,
    CVE_16BIT_TO_8BIT,
    CVE_ORD_STAT_FILTER,
    CVE_MAP,
    CVE_EQUALIZE_HIST,
    CVE_NCC,
    CVE_CCL,
    CVE_GMM,
    CVE_CANNY_EDGE,
    CVE_LBP,
    CVE_NORM_GRAD,
    CVE_BUILD_LK_OPTICAL_FLOW_PYR,
    CVE_LK_OPTICAL_FLOW_PYR,
    CVE_ST_CANDI_CORNER,
    CVE_SAD,
    CVE_GRAD_FG,
    CVE_UPDATE_BG_MODEL,
    CVE_TOF,
    CVE_MODULE_NUMS,
} CVE_MODULE_OPS;

typedef struct {
    CVE_MODULE_OPS ops;
    AML_U32 min_width;
    AML_U32 min_height;
    AML_U32 max_width;
    AML_U32 max_height;
} CVE_Module_Image_Size;

CVE_Module_Image_Size module_img_size[] = {
    {CVE_DMA, 64, 64, 1920, 1080},
    {CVE_LUMA, 64, 64, 1920, 1080},
    {CVE_FILTER, 64, 64, 1920, 1080},
    {CVE_CSC, 64, 64, 1920, 1080},
    {CVE_FILTER_AND_CSC, 64, 64, 1920, 1080},
    {CVE_SOBEL, 64, 64, 1920, 1080},
    {CVE_MAG_AND_ANG, 64, 64, 1920, 1080},
    {CVE_MATCH_BG_MODEL, 64, 64, 1280, 720},
    {CVE_DILATE, 64, 64, 1920, 1080},
    {CVE_ERODE, 64, 64, 1920, 1080},
    {CVE_THRESH, 64, 64, 1920, 1080},
    {CVE_ALU_SUB, 64, 64, 1920, 1080},
    {CVE_ALU_OR, 64, 64, 1920, 1080},
    {CVE_ALU_AND, 64, 64, 1920, 1080},
    {CVE_ALU_XOR, 64, 64, 1920, 1080},
    {CVE_ALU_ADD, 64, 64, 1920, 1080},
    {CVE_INTEG, 64, 64, 1920, 1080},
    {CVE_HIST, 64, 64, 1920, 1080},
    {CVE_THRESH_S16, 64, 64, 1920, 1080},
    {CVE_THRESH_U16, 64, 64, 1920, 1080},
    {CVE_16BIT_TO_8BIT, 64, 64, 1920, 1080},
    {CVE_ORD_STAT_FILTER, 64, 64, 1920, 1080},
    {CVE_MAP, 64, 64, 1920, 1080},
    {CVE_EQUALIZE_HIST, 64, 64, 1920, 1080},
    {CVE_NCC, 64, 64, 1920, 1080},
    {CVE_CCL, 64, 64, 1920, 1080},
    {CVE_GMM, 64, 64, 1920, 1080},
    {CVE_CANNY_EDGE, 64, 64, 1920, 1080},
    {CVE_LBP, 64, 64, 1920, 1080},
    {CVE_NORM_GRAD, 64, 64, 1920, 1080},
    {CVE_BUILD_LK_OPTICAL_FLOW_PYR, 64, 64, 1280, 720},
    {CVE_LK_OPTICAL_FLOW_PYR, 64, 64, 1280, 720},
    {CVE_ST_CANDI_CORNER, 64, 64, 1920, 1080},
    {CVE_SAD, 64, 64, 1920, 1080},
    {CVE_GRAD_FG, 64, 64, 1920, 1080},
    {CVE_UPDATE_BG_MODEL, 64, 64, 1280, 720},
    {CVE_TOF, 64, 64, 640, 480},
};

#define MODULE_MIN_WIDTH image_sz->min_width
#define MODULE_MIN_HEIGHT image_sz->min_height
#define MODULE_MAX_WIDTH image_sz->max_width
#define MODULE_MAX_HEIGHT image_sz->max_height

#define CVE_GET_IMAGE_SIZE(module_name)                                                            \
    CVE_Module_Image_Size *image_sz;                                                               \
    image_sz = get_image_size(module_name);                                                        \
    if (image_sz == NULL) {                                                                        \
        return AML_ERR_CVE_ILLEGAL_PARAM;                                                          \
    }

#define CVE_DATA_CHECK(module_name, cve_data)                                                      \
    do {                                                                                           \
        if ((cve_data->u32Width < MODULE_MIN_WIDTH) || (cve_data->u32Width > MODULE_MAX_WIDTH) ||  \
            (cve_data->u32Height < MODULE_MIN_HEIGHT) ||                                           \
            (cve_data->u32Height > MODULE_MAX_HEIGHT)) {                                           \
            CVE_ERR_TRACE("[%s] %s->u32Width=%d %s->u32Height=%d error\n", #module_name,           \
                          #cve_data, cve_data->u32Width, #cve_data, cve_data->u32Height);          \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
        if (cve_data->u32Stride != CVE_ALIGN_UP(cve_data->u32Width, CVE_ALIGN)) {                  \
            CVE_ERR_TRACE("[%s] %s->u32Stride=%d error\n", #module_name, #cve_data,                \
                          cve_data->u32Stride);                                                    \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
        if (cve_check_mem(cve_data->u64PhyAddr, cve_data->u32Stride * cve_data->u32Height, 1)) {   \
            CVE_ERR_TRACE("[%s] Memory area is invalid\n", #module_name);                          \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_MEM_CHECK(module_name, cve_mem)                                                        \
    do {                                                                                           \
        if (cve_mem->u32Size == 0) {                                                               \
            CVE_ERR_TRACE("[%s] %s->u32Size=%d error\n", #module_name, #cve_mem,                   \
                          cve_mem->u32Size);                                                       \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
        if (cve_check_mem(cve_mem->u64PhyAddr, cve_mem->u32Size, CVE_ALIGN)) {                     \
            CVE_ERR_TRACE("[%s] Memory area is invalid\n", #module_name);                          \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_IMAGE_CHECK(module_name, cve_image)                                                    \
    do {                                                                                           \
        if ((cve_image->u32Width < MODULE_MIN_WIDTH) ||                                            \
            (cve_image->u32Width > MODULE_MAX_WIDTH) ||                                            \
            (cve_image->u32Height < MODULE_MIN_HEIGHT) ||                                          \
            (cve_image->u32Height > MODULE_MAX_HEIGHT)) {                                          \
            CVE_ERR_TRACE("[%s] %s->u32Width=%d %s->u32Height=%d error\n", #module_name,           \
                          #cve_image, cve_image->u32Width, #cve_image, cve_image->u32Height);      \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
        if (cve_check_image_stride(cve_image)) {                                                   \
            CVE_ERR_TRACE("[%s] Stride is invalid\n", #module_name);                               \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
        if (cve_check_image_mem(cve_image, CVE_ALIGN)) {                                           \
            CVE_ERR_TRACE("[%s] Image memory area is invalid\n", #module_name);                    \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_RAW_CHECK(module_name, cve_raw)                                                        \
    do {                                                                                           \
        if ((cve_raw->u32Width < MODULE_MIN_WIDTH) || (cve_raw->u32Width > MODULE_MAX_WIDTH) ||    \
            (cve_raw->u32Height < MODULE_MIN_HEIGHT) ||                                            \
            (cve_raw->u32Height > MODULE_MAX_HEIGHT)) {                                            \
            CVE_ERR_TRACE("[%s] %s->u32Width=%d %s->u32Height=%d error\n", #module_name, #cve_raw, \
                          cve_raw->u32Width, #cve_raw, cve_raw->u32Height);                        \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
        if (cve_raw->u32Stride != CVE_ALIGN_UP(cve_raw->u32Width, CVE_ALIGN)) {                    \
            CVE_ERR_TRACE("[%s] Stride is invalid\n", #module_name);                               \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
        if (cve_check_mem(cve_raw->u64PhyAddr, cve_raw->u32Stride * cve_raw->u32Height,            \
                          CVE_ALIGN)) {                                                            \
            CVE_ERR_TRACE("[%s] Raw memory area is invalid\n", #module_name);                      \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_IMAGE_COMPARE(module_name, srcimage, dstimage)                                         \
    do {                                                                                           \
        if ((srcimage->u32Width < dstimage->u32Width) ||                                           \
            (srcimage->u32Height < dstimage->u32Height)) {                                         \
            CVE_ERR_TRACE("[%s] %s->u32Width(%d) %s->u32Width(%d) set error\n", #module_name,      \
                          #srcimage, srcimage->u32Width, #dstimage, dstimage->u32Width);           \
            CVE_ERR_TRACE("[%s] %s->u32Height(%d) %s->u32Height(%d) set error\n", #module_name,    \
                          #srcimage, srcimage->u32Height, #dstimage, dstimage->u32Height);         \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define RESOLUTION_DS_NONE 0
#define RESOLUTION_DS_MB_2X2 1
#define RESOLUTION_DS_MB_4X4 2
#define RESOLUTION_DS_MB_8X8 3
#define RESOLUTION_DS_MB_16X16 4
#define CVE_RESOLUTION_EQUAL(module_name, src, dst, ds)                                            \
    do {                                                                                           \
        if ((src->u32Width >> ds != dst->u32Width) || (src->u32Height >> ds != dst->u32Height)) {  \
            CVE_ERR_TRACE("[%s] %s->u32Width(%d) %s->u32Width(%d) set error\n", #module_name,      \
                          #src, src->u32Width, #dst, dst->u32Width);                               \
            CVE_ERR_TRACE("[%s] %s->u32Height(%d) %s->u32Height(%d) set error\n", #module_name,    \
                          #src, src->u32Height, #dst, dst->u32Height);                             \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_RANGE_CHECK(module_name, param, min, max)                                              \
    do {                                                                                           \
        if ((param > max) || (param < min)) {                                                      \
            CVE_ERR_TRACE("[%s] [%s](%d) set value error, value "                                  \
                          "must in[%d, %d]\n",                                                     \
                          #module_name, #param, param, min, max);                                  \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_MODE_CHECK(module_name, mode, mode_butt)                                               \
    do {                                                                                           \
        if (mode >= mode_butt) {                                                                   \
            CVE_ERR_TRACE("[%s][%s] not support this mode [%d], valid range [0, %u]\n",            \
                          #module_name, #mode, mode, mode_butt - 1);                               \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_BOOL_CHECK(module_name, bool)                                                          \
    do {                                                                                           \
        if (bool != AML_TRUE && bool != AML_FALSE) {                                               \
            CVE_ERR_TRACE("[%s][%s] (%d) must be AML_TRUE or AML_FALSE!\n", #module_name, #bool,   \
                          bool);                                                                   \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_IMAGE_TYPE_CHECK(module_name, src, type)                                               \
    do {                                                                                           \
        if (src->enType != type) {                                                                 \
            CVE_ERR_TRACE("[%s] %s->enType(%d) set error, need set %s.\n", #module_name, #src,     \
                          src->enType, #type);                                                     \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

#define CVE_IMAGE_TYPE_EQUAL(module_name, src, dst)                                                \
    do {                                                                                           \
        if (src->enType != dst->enType) {                                                          \
            CVE_ERR_TRACE("[%s] %s->enType(%d) not equal %s->enType(%d), need equal.\n",           \
                          #module_name, #src, src->enType, #dst, dst->enType);                     \
            return AML_ERR_CVE_ILLEGAL_PARAM;                                                      \
        }                                                                                          \
    } while (0)

struct cve_device_s {
    dev_t dev_num;
    char name[10];
    struct device *cve_pdev;
    struct class *cve_class;
    struct resource *mem;
    int cve_irq;
    int major;
    struct proc_dir_entry *proc_node_entry;
    int cve_rate;
};

struct cve_device_s cve_device;

unsigned short cve_node_num = 500;
cve_sys_status_e cve_state = CVE_SYS_STATUS_IDLE;
int cve_irq;
cve_context_t cve_context;
static spinlock_t cve_spinlock;
bool cve_timeout_flag;
atomic_t cve_user_ref;
int cve_rate;
unsigned long long g_cve_mmz_base_addr;
void __iomem *cve_regs_map;
struct device *cve_pdev;
char *multi_cmd_buf[CVE_LUMA_MULTI_CMD_MAX];
unsigned int multi_cmd_line_num[CVE_LUMA_MULTI_CMD_MAX];

static int cve_check_mem(unsigned long long phy_addr, unsigned long addr_len, unsigned int aligned);
static int cve_check_image_mem(CVE_IMAGE_T *pstImg, unsigned int aligned);
static int cve_check_image_stride(CVE_IMAGE_T *pstImg);
static int cve_reg_init(void);
static void cve_reg_write(unsigned int reg, unsigned int val);
static inline void cve_reg_bits_set(unsigned int reg, const unsigned int value,
                                    const unsigned int start, const unsigned int len);
static unsigned int cve_reg_read(unsigned int reg);
static inline void cve_reg_bits_set(unsigned int reg, const unsigned int value,
                                    const unsigned int start, const unsigned int len);
static void cve_cq0_int_clear(void);
static void cve_cq0_int_enable(void);
static void cve_cq0_int_disable(void);
static void cve_cq0_function_enable(void);
static void cve_cq0_function_disable(void);
static void cve_set_reset(void);
static unsigned int cve_int_status_get(void);
static bool cve_is_timeout_int(unsigned int status);
static void cve_start_task(cve_task_desc_t *task_desc);
static int cve_init(void);
static void cve_op_run_time_cycle_enable(void);
static void cve_ute_stat_enable(unsigned int set_total_cycle);
static unsigned int cve_get_ute_cycle_in_total(void);
#if 0
static void cve_sys_timeout_enable(unsigned int timeout);
static void cve_sys_timeout_disable(void);
static unsigned int cve_task_id_get(void);
static void cve_clk_gate_enable(void);
#endif
static int cve_cmd_queue_deinit(cve_context_t *cve_context);
static int cve_cmd_queue_init(cve_context_t *cve_context);
static char *cmd_buf_get(void);
static int request_op_cmd(cve_cmd_desc_t **cmd_desc, unsigned int cmd_num);
static void cve_input_process(cve_op_io_info_t *info);
static void cve_output_process(cve_op_io_info_t *info);

extern unsigned int meson_ion_cma_heap_id_get(void);
extern unsigned int meson_ion_fb_heap_id_get(void);
extern unsigned int meson_ion_codecmm_heap_id_get(void);
extern void meson_ion_buffer_to_phys(struct ion_buffer *buffer, phys_addr_t *addr, size_t *len);
#define ION_FLAG_EXTEND_MESON_HEAP BIT(30)
int cve_ion_alloc_buffer(cve_ion_buffer_t *cve_buffer)
{
    int id;
    size_t len;
    struct dma_buf *dma_buf = NULL;
    struct ion_buffer *ionbuffer = NULL;

    len = cve_buffer->size;
    id = meson_ion_fb_heap_id_get();
    if (id) {
        dma_buf = ion_alloc(len, (1 << id), ION_FLAG_EXTEND_MESON_HEAP);
    }
    if (IS_ERR_OR_NULL(dma_buf)) {
        id = meson_ion_cma_heap_id_get();
        dma_buf = ion_alloc(len, (1 << id), ION_FLAG_EXTEND_MESON_HEAP);
    }
    if (IS_ERR_OR_NULL(dma_buf)) {
        id = meson_ion_codecmm_heap_id_get();
        dma_buf = ion_alloc(len, (1 << id), ION_FLAG_EXTEND_MESON_HEAP);
    }
    if (IS_ERR(dma_buf)) {
        return -ENOMEM;
    }

    if (IS_ERR_OR_NULL(dma_buf)) {
        return PTR_ERR(dma_buf);
    }
    cve_buffer->dma_buf = dma_buf;
    ionbuffer = (struct ion_buffer *)(dma_buf->priv);
    cve_buffer->ionbuffer = ionbuffer;
    meson_ion_buffer_to_phys(ionbuffer, (phys_addr_t *)&cve_buffer->phys_start, &len);
    dma_buf_vmap(dma_buf);
    cve_buffer->virt_start = (unsigned long)dma_buf->vmap_ptr;
    return 0;
}

void cve_ion_free_buffer(cve_ion_buffer_t *cve_buffer)
{
    if (cve_buffer->ionbuffer) {
        dma_buf_vunmap(cve_buffer->dma_buf, (void *)cve_buffer->virt_start);
        ion_free(cve_buffer->ionbuffer);
        cve_buffer->ionbuffer = NULL;
        cve_buffer->dma_buf = NULL;
    } else {
        printk("cve_ion_free_buffer cve_bufferis null\n");
    }
}

static long cve_dmabuf_get_phyaddr(int fd)
{
    unsigned long phy_addr = 0;
    struct dma_buf *dbuf = NULL;
    struct sg_table *table = NULL;
    struct page *page = NULL;
    struct dma_buf_attachment *attach = NULL;

    dbuf = dma_buf_get(fd);
    attach = dma_buf_attach(dbuf, cve_pdev);
    if (IS_ERR(attach))
        return 0;

    table = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
    page = sg_page(table->sgl);
    phy_addr = PFN_PHYS(page_to_pfn(page));
    dma_buf_unmap_attachment(attach, table, DMA_BIDIRECTIONAL);
    dma_buf_detach(dbuf, attach);
    dma_buf_put(dbuf);

    return phy_addr;
}

static void set_image_phy_addr(CVE_IMAGE_T *pstImg)
{
    unsigned long phyaddr;

    if (pstImg->dmafd == 0)
        return;
    phyaddr = cve_dmabuf_get_phyaddr(pstImg->dmafd);

    pstImg->au64PhyAddr[0] = phyaddr;
    switch (pstImg->enType) {
    case CVE_IMAGE_TYPE_S8C2_PLANAR:
    case CVE_IMAGE_TYPE_YUV420SP:
    case CVE_IMAGE_TYPE_YUV422SP: {
        pstImg->au64PhyAddr[1] =
            pstImg->au64PhyAddr[0] + (AML_U64)pstImg->au32Stride[0] * (AML_U64)pstImg->u32Height;
    } break;
    case CVE_IMAGE_TYPE_S8C2_PACKAGE: {
        pstImg->au64PhyAddr[1] = pstImg->au64PhyAddr[0] + 1;
    } break;
    case CVE_IMAGE_TYPE_U8C3_PACKAGE: {
        pstImg->au64PhyAddr[1] = pstImg->au64PhyAddr[0] + 1;
        pstImg->au64PhyAddr[2] = pstImg->au64PhyAddr[1] + 1;
    } break;
    case CVE_IMAGE_TYPE_YUV420P: {
        pstImg->au64PhyAddr[1] = pstImg->au64PhyAddr[0] + pstImg->au32Stride[0] * pstImg->u32Height;
        pstImg->au64PhyAddr[2] =
            pstImg->au64PhyAddr[1] + pstImg->au32Stride[1] * pstImg->u32Height / 2;
    } break;
    case CVE_IMAGE_TYPE_YUV422P:
    case CVE_IMAGE_TYPE_U8C3_PLANAR: {
        pstImg->au64PhyAddr[1] = pstImg->au64PhyAddr[0] + pstImg->au32Stride[0] * pstImg->u32Height;
        pstImg->au64PhyAddr[2] = pstImg->au64PhyAddr[1] + pstImg->au32Stride[1] * pstImg->u32Height;
    } break;
    default:
        break;
    }
}

static void set_raw_phy_addr(CVE_RAW_T *pstRaw)
{
    unsigned long phyaddr;

    phyaddr = cve_dmabuf_get_phyaddr(pstRaw->dmafd);
    pstRaw->u64PhyAddr = phyaddr;
}

static void set_mem_phy_addr(CVE_MEM_INFO_T *pstMemInfo)
{
    unsigned long phyaddr;

    phyaddr = cve_dmabuf_get_phyaddr(pstMemInfo->dmafd);
    pstMemInfo->u64PhyAddr = phyaddr;
}

static void set_data_phy_addr(CVE_DATA_T *pstData)
{
    unsigned long phyaddr;

    phyaddr = cve_dmabuf_get_phyaddr(pstData->dmafd);
    pstData->u64PhyAddr = phyaddr;
}
static void *cve_vmap(unsigned long phys, unsigned int size)
{
    pgprot_t pgprot;
    unsigned int npages, offset;
    struct page **pages = NULL;
    static unsigned char *vaddr = 0;
    int i;

    npages = PAGE_ALIGN(size) / PAGE_SIZE;
    offset = phys & ~PAGE_MASK;
    if ((offset + size) > PAGE_SIZE)
        npages++;
    pages = vmalloc(sizeof(struct page *) * npages);
    if (!pages)
        return NULL;
    for (i = 0; i < npages; i++) {
        pages[i] = phys_to_page(phys);
        phys += PAGE_SIZE;
    }
    /*nocache*/
    pgprot = pgprot_writecombine(PAGE_KERNEL);
    vaddr = vmap(pages, npages, VM_MAP, pgprot);
    if (!vaddr) {
        printk("the phy(%lx) vmaped fail, size: %d\n", phys, npages << PAGE_SHIFT);
        vfree(pages);
        return NULL;
    }
    vfree(pages);
    return (void *)vaddr;
}

static void cve_vunmap(void *virt)
{
    static void *vaddr;
    vaddr = (void *)(PAGE_MASK & (ulong)virt);
    vunmap(vaddr);
}

static void cve_runtime_set_power(int enable, void *dev)
{
    int ret = -1;

    if (enable) {
        if (pm_runtime_suspended(dev)) {
            CVE_ERR_TRACE("cve power on\n");
            ret = pm_runtime_get_sync(dev);
            if (ret < 0) {
                CVE_ERR_TRACE("runtime get power error\n");
            }
        }
    } else {
        if (pm_runtime_active(dev)) {
            CVE_ERR_TRACE("cve power off\n");
            ret = pm_runtime_put_sync(dev);
            if (ret < 0) {
                CVE_ERR_TRACE("runtime put power error\n");
            }
        }
    }

    return;
}

int cve_check_cve_node_num(int cmd_num)
{
    unsigned short cmd_node;

    cmd_node = cve_context.cq_desc[cve_context.queue_wait].end_cmd_id;
    if (cmd_node <= cve_node_num - (unsigned short)cmd_num) {
        return 0;
    }
    CVE_ERR_TRACE("Can't put so much cmd nodes into waiting cmd queue, "
                  "cmd_num:%u,cur_cmd_nodes:%u, cmd_node_max = %u!\n",
                  cmd_num, cmd_node, cve_node_num);

    return AML_ERR_CVE_BUF_FULL;
}

static bool cve_task_io_process_record(cve_task_desc_t *task_desc, cve_op_io_info_t *info,
                                       bool check_only)
{
    if (info->inp_flags == 0 && info->outp_flags == 0) {
        return true;
    }

    if (task_desc->bOutput || task_desc->bInput) {
        if (task_desc->output_process_flags & info->outp_flags) {
            return false;
        }
        if (task_desc->input_process_flags & info->inp_flags) {
            return false;
        }
    }

    if (check_only) {
        return true;
    }

    if (info->inp_flags) {
        task_desc->input_process_flags |= info->inp_flags;
        task_desc->bInput = AML_TRUE;
    }
    if (info->outp_flags) {
        task_desc->output_process_flags |= info->outp_flags;
        task_desc->bOutput = AML_TRUE;
    }

    return true;
}

static int cve_manage_handle(CVE_HANDLE *cveHandle, unsigned int cmd_num)
{
    cve_task_info_t *task_info = &cve_context.task_info;

    if (task_info->cmd_handle + cmd_num > CMD_HANDLE_MAX) {
        task_info->cmd_handle = task_info->cmd_handle + cmd_num - CMD_HANDLE_MAX;
        task_info->cmd_handle_wrap++;
    } else {
        task_info->cmd_handle = task_info->cmd_handle + cmd_num;
    }

    *cveHandle = task_info->cmd_handle - 1;

    return 0;
}

static void task_descs_init(cve_task_desc_t *task_desc)
{
    task_desc->total_cmd_num = 0;
    task_desc->total_cmd_line_num = 0;
    task_desc->bOutput = AML_FALSE;
    task_desc->bInput = AML_FALSE;
    task_desc->bInvalid = AML_FALSE;
    task_desc->input_process_flags = 0;
    task_desc->output_process_flags = 0;
    INIT_LIST_HEAD(&task_desc->cmd_list);

    return;
}

static int cve_create_task(cve_cmd_desc_t *cmd_desc, char *cmd_buf)
{
    cve_cq_desc_t *cq_desc_wait;
    cve_task_desc_t *task_desc;

    AML_ASSERT(cmd_desc != NULL);
    AML_ASSERT(cmd_buf != NULL);
    AML_ASSERT((cmd_desc->instant == AML_TRUE) || (cmd_desc->instant == AML_FALSE));

    cq_desc_wait = &cve_context.cq_desc[cve_context.queue_wait];
    if (cq_desc_wait->end_cmd_id >= cve_node_num) {
        CVE_ERR_TRACE(
            "the waiting queue is full of cmd nodes! end_cmd_id = %d,max cmd nodes = %d\n",
            cq_desc_wait->end_cmd_id, cve_node_num);
        return AML_ERR_CVE_BUF_FULL;
    }

    task_desc = &cq_desc_wait->task_descs[cq_desc_wait->task_descs_create_index];
    if (task_desc->bInvalid != AML_TRUE) {
        if (cq_desc_wait->task_instant == 1 ||
            task_desc->total_cmd_line_num + cmd_desc->cmd_line_num > TASK_CMD_LINE_MAX ||
            task_desc->total_cmd_num >= TASK_CMD_MAX ||
            !cve_task_io_process_record(task_desc, &cmd_desc->io_info, true)) {
            cq_desc_wait->task_descs_create_index++;
            cq_desc_wait->task_virt_offset += task_desc->total_cmd_num * TASK_CMD_LINE_SIZE;
            cq_desc_wait->task_phys_offset += task_desc->total_cmd_num * TASK_CMD_LINE_SIZE;
            task_desc = &cq_desc_wait->task_descs[cq_desc_wait->task_descs_create_index];
            task_descs_init(task_desc);
        }
    } else {
        task_descs_init(task_desc);
    }

    task_desc->virt_addr = cq_desc_wait->virt_start + cq_desc_wait->task_virt_offset;
    task_desc->phys_addr = cq_desc_wait->phys_start + cq_desc_wait->task_phys_offset;

    memcpy((char *)(uintptr_t)task_desc->virt_addr, cmd_buf,
           cmd_desc->cmd_line_num * TASK_CMD_LINE_SIZE);
    cmd_desc->virt_addr = task_desc->virt_addr + task_desc->total_cmd_line_num * TASK_CMD_LINE_SIZE;
    cmd_desc->phys_addr = task_desc->phys_addr + task_desc->total_cmd_line_num * TASK_CMD_LINE_SIZE;
    cmd_desc->task_desc = task_desc;
    list_add_tail(&cmd_desc->list, &task_desc->cmd_list);
    task_desc->total_cmd_line_num += cmd_desc->cmd_line_num;
    task_desc->total_cmd_num++;

    cve_task_io_process_record(task_desc, &cmd_desc->io_info, false);
    cq_desc_wait->task_instant = cmd_desc->instant;

    cq_desc_wait->end_cmd_id++;

    return 0;
}

static int cve_create_task_multi_cmd(cve_cmd_desc_t *cmd_desc, char *cmd_buf_arr[],
                                     unsigned int cmd_line_num_arr[], unsigned int cmd_num)
{
    cve_cq_desc_t *cq_desc_wait;
    cve_task_desc_t *task_desc;
    int i = 0;
    int ofs = 0;

    AML_ASSERT(cmd_desc != NULL);
    AML_ASSERT(cmd_buf_arr != NULL);
    AML_ASSERT((cmd_desc->instant == AML_TRUE) || (cmd_desc->instant == AML_FALSE));

    cq_desc_wait = &cve_context.cq_desc[cve_context.queue_wait];
    if (cq_desc_wait->end_cmd_id >= cve_node_num) {
        CVE_ERR_TRACE(
            "the waiting queue is full of cmd nodes! end_cmd_id = %d,max cmd nodes = %d\n",
            cq_desc_wait->end_cmd_id, cve_node_num);
        return AML_ERR_CVE_BUF_FULL;
    }

    task_desc = &cq_desc_wait->task_descs[cq_desc_wait->task_descs_create_index];
    if (cq_desc_wait->task_instant == 1 ||
        task_desc->total_cmd_line_num + cmd_desc->cmd_line_num > TASK_CMD_LINE_MAX ||
        task_desc->total_cmd_num >= TASK_CMD_MAX ||
        !cve_task_io_process_record(task_desc, &cmd_desc->io_info, true)) {
        cq_desc_wait->task_descs_create_index++;
        cq_desc_wait->task_virt_offset += task_desc->total_cmd_line_num * TASK_CMD_LINE_SIZE;
        cq_desc_wait->task_phys_offset += task_desc->total_cmd_line_num * TASK_CMD_LINE_SIZE;
        task_desc = &cq_desc_wait->task_descs[cq_desc_wait->task_descs_create_index];
    }
    if (task_desc->bInvalid == AML_TRUE) {
        task_desc->virt_addr = cq_desc_wait->virt_start + cq_desc_wait->task_virt_offset;
        task_desc->phys_addr = cq_desc_wait->phys_start + cq_desc_wait->task_phys_offset;
        task_desc->total_cmd_num = 0;
        task_desc->total_cmd_line_num = 0;
        task_desc->bOutput = AML_FALSE;
        task_desc->bInput = AML_FALSE;
        task_desc->bInvalid = AML_FALSE;
        task_desc->input_process_flags = 0;
        task_desc->output_process_flags = 0;
        INIT_LIST_HEAD(&task_desc->cmd_list);
    }
    task_desc->virt_addr = cq_desc_wait->virt_start + cq_desc_wait->task_virt_offset;
    task_desc->phys_addr = cq_desc_wait->phys_start + cq_desc_wait->task_phys_offset;

    for (i = 0; i < cmd_num; i++) {
        memcpy((char *)(uintptr_t)task_desc->virt_addr + ofs, cmd_buf_arr[i],
               cmd_desc->cmd_line_num * 8);
        ofs += cmd_line_num_arr[i] * TASK_CMD_LINE_SIZE;
    }

    cmd_desc->virt_addr = task_desc->virt_addr + task_desc->total_cmd_line_num * TASK_CMD_LINE_SIZE;
    cmd_desc->phys_addr = task_desc->phys_addr + task_desc->total_cmd_line_num * TASK_CMD_LINE_SIZE;
    cmd_desc->task_desc = task_desc;
    list_add_tail(&cmd_desc->list, &task_desc->cmd_list);
    task_desc->total_cmd_line_num += cmd_desc->cmd_line_num;
    task_desc->total_cmd_num += cmd_num;
    cve_task_io_process_record(task_desc, &cmd_desc->io_info, false);
    cq_desc_wait->task_instant = cmd_desc->instant;
    cq_desc_wait->end_cmd_id += cmd_num;

    return 0;
}

static void cve_start_task(cve_task_desc_t *task_desc)
{
    cve_reg_write(CVE_CQ_REG0, task_desc->phys_addr >> 4);
    cve_reg_bits_set(CVE_CQ_REG2, task_desc->total_cmd_line_num, 0, 12);
    cve_reg_bits_set(CVE_CQ_REG3, task_desc->total_cmd_num, 0, 9);

    cve_cq0_int_enable();
    /* for dma luma stat */
    cve_cq0_function_enable();
    /* enable cq0 */
    cve_reg_bits_set(CVE_CQ_REG2, 1, 24, 1);
}

static void cve_ute_time_cycle(void)
{
    cve_run_time_cycle_t *cve_cycle = &cve_context.run_time_info.cve_cycle;
    cve_cycle->cve_ute_cycles = cve_get_ute_cycle_in_total();
    cve_context.run_time_info.last_persec_int_cost_time = cve_cycle->cve_ute_cycles;
}

static void cve_continue_task(void)
{
    cve_task_info_t *task_info;
    cve_cq_desc_t *cq_desc_busy;
    cve_cq_desc_t *cq_desc_wait;
    cve_task_desc_t *task_desc;

    cq_desc_busy = &cve_context.cq_desc[cve_context.queue_busy];
    cq_desc_wait = &cve_context.cq_desc[cve_context.queue_wait];
    task_info = &cve_context.task_info;

    if (cve_context.queue_busy != CVE_STATUS_IDLE) {
        if (cve_context.queue_wait != CVE_STATUS_CQ0 && cve_context.queue_wait != CVE_STATUS_CQ1) {
            panic("\nASSERT at:\n  >Function : %s\n  >Line No. : %d\n  >Condition: %s\n",
                  __FUNCTION__, __LINE__,
                  "(queue_wait == CVE_STATUS_CQ0) || (queue_wait == CVE_STATUS_CQ1)");
        }
        task_desc = &cq_desc_busy->task_descs[cq_desc_busy->task_descs_invoke_index++];
        task_desc->bInvalid = AML_TRUE;

        task_info->cur_finish_cmd_id += task_desc->total_cmd_num;

        if (task_info->cmd_finish_cnt > CMD_HANDLE_MAX) {
            task_info->cmd_finish_cnt -= CMD_HANDLE_MAX;
            task_info->finish_cmd_wrap++;
        }

        cq_desc_busy->cur_cmd_id += task_desc->total_cmd_num;

        if (cq_desc_busy->cur_cmd_id > cq_desc_busy->end_cmd_id) {
            panic("\nASSERT at:\n  >Function : %s\n  >Line No. : %d\n  >Condition: %s\n",
                  __FUNCTION__, __LINE__, "busy_cur_cmd_id <= busy_end_cmd_id");
        }

        if (cq_desc_busy->cur_cmd_id == cq_desc_busy->end_cmd_id) {
            cq_desc_busy->task_phys_offset = 0;
            cq_desc_busy->task_virt_offset = 0;
            cq_desc_busy->cur_cmd_id = 0;
            cq_desc_busy->end_cmd_id = 0;
            cq_desc_busy->task_descs_create_index = 0;
            cq_desc_busy->task_descs_invoke_index = 0;
            cq_desc_busy->task_instant = 0;
            /*FIX ME: If you want to zero out, you need to synchronize with the task creation
             * process*/
            cq_desc_busy->cmd_descs_index = 0;
            task_info->cur_finish_cmd_id = 0;
            task_info->last_finish_cmd_id = 0;

            if (cq_desc_wait->cur_cmd_id > cq_desc_wait->end_cmd_id) {
                panic("\nASSERT at:\n  >Function : %s\n  >Line No. : %d\n  >Condition: %s\n",
                      __FUNCTION__, __LINE__, "wait_cur_cmd_id <= wait_end_cmd_id");
            }
            if (cq_desc_wait->cur_cmd_id == cq_desc_wait->end_cmd_id) {
                cve_context.queue_busy = CVE_STATUS_IDLE;
            } else {
                cve_context.queue_busy = cve_context.queue_wait;
                cve_context.queue_wait = CVE_STATUS_CQ1 - cve_context.queue_wait;
            }
        } else {
            task_info->last_finish_cmd_id = task_info->cur_finish_cmd_id + 1;
        }

        if (task_desc->bOutput || task_desc->bInput) {
            cve_context.task_desc_outp = task_desc;
            schedule_work(&cve_context.cve_wrok);

        } else {
            if (cve_context.queue_busy != CVE_STATUS_IDLE) {
                cq_desc_busy = &cve_context.cq_desc[cve_context.queue_busy];
                task_desc = &cq_desc_busy->task_descs[cq_desc_busy->task_descs_invoke_index];
                cve_start_task(task_desc);
            }
            task_info->cmd_finish_cnt += task_desc->total_cmd_num;
        }
        if (cve_context.run_time_info.cve_cycle.cve_ute_stat_en == true) {
            cve_ute_time_cycle();
        }
    }

    return;
}

static int cve_irq_handler(int handle, void *data)
{
    unsigned long flags;
    unsigned int status;
    int i;

    status = cve_int_status_get();
    cve_cq0_int_clear();
    /*for dma luma stat*/
    cve_cq0_function_disable();

    spin_lock_irqsave(&cve_spinlock, flags);
    if (!cve_is_timeout_int(status)) {
        cve_continue_task();
    } else {
        cve_set_reset();
        cve_timeout_flag = true;
        cve_context.queue_wait = CVE_STATUS_CQ0;
        cve_context.queue_busy = CVE_STATUS_IDLE;
        cve_context.run_time_info.system_timeout_cnt++;
        cve_context.run_time_info.last_instant = false;
        for (i = 0; i < 2; i++) {
            cve_context.cq_desc[i].task_phys_offset = 0;
            cve_context.cq_desc[i].task_virt_offset = 0;
            cve_context.cq_desc[i].cur_cmd_id = 0;
            cve_context.cq_desc[i].end_cmd_id = 0;
            cve_context.cq_desc[i].task_descs_create_index = 0;
            cve_context.cq_desc[i].task_descs_invoke_index = 0;
            cve_context.cq_desc[i].task_instant = 0;
            cve_context.cq_desc[i].cmd_descs_index = 0;
        }
        memset(&cve_context.task_info, 0, sizeof(cve_task_info_t));
    }
    spin_unlock_irqrestore(&cve_spinlock, flags);
    wake_up(&cve_context.cve_wait);

    return 1;
}

static int cve_platform_module_init(void)
{
    int ret = 0;

    if (cve_state == CVE_SYS_STATUS_READY) {
        CVE_INFO_TRACE("cve ready, state = %d!\n", cve_state);
        return 0;
    }

    if (cve_state == CVE_SYS_STATUS_STOP) {
        CVE_ERR_TRACE("cve state = %d!\n", cve_state);
        return AML_ERR_CVE_BUSY;
    }

    ret = cve_init();
    if (ret) {
        CVE_ERR_TRACE("cve platform module init failed!\n");
        return ret;
    }

    cve_reg_init();
    cve_cq0_int_enable();
    cve_cq0_int_clear();
    cve_cq0_function_enable();
#if 0
    cve_sys_timeout_enable(CVE_SYS_TIMEOUT);
    cve_clk_gate_enable();
#endif
    if (cve_context.run_time_info.cve_cycle.cve_dbg_run_cycles_en == true) {
        cve_op_run_time_cycle_enable();
    }
    cve_state = CVE_SYS_STATUS_READY;
    pm_runtime_enable(cve_pdev);
    cve_runtime_set_power(1, cve_pdev);

    return 0;
}

static void cve_platform_module_exit(void)
{

    if (cve_state != CVE_SYS_STATUS_IDLE) {
        cve_runtime_set_power(1, cve_pdev);
        cve_set_reset();
        free_irq(cve_irq, cve_pdev);
        cve_cmd_queue_deinit(&cve_context);
        cve_ion_free_buffer(&cve_context.cve_cq_buffer);
        cve_ion_free_buffer(&cve_context.cve_cmd_buffer);
        flush_work(&cve_context.cve_wrok); //
        memset(&cve_context, 0, sizeof(cve_context_t));
#if 0
        cve_sys_timeout_disable();
#endif
        cve_cq0_function_disable();
        cve_cq0_int_disable();
        cve_runtime_set_power(0, cve_pdev);

        cve_state = CVE_SYS_STATUS_IDLE;
    }

    if (pm_runtime_enabled(cve_pdev)) {
        pm_runtime_disable(cve_pdev);
    }

    return;
}

static CVE_Module_Image_Size *get_image_size(CVE_MODULE_OPS ops)
{
    int i;
    for (i = 0; i < CVE_MODULE_NUMS; i++) {
        if (module_img_size[i].ops == ops) {
            return &module_img_size[i];
        }
    }
    if (i == CVE_MODULE_NUMS) {
        CVE_ERR_TRACE("not find cve module image size set.");
    }
    return NULL;
}

static unsigned int fill_src_image(cve_comm_init_params_t *init_params,
                                   CVE_SRC_IMAGE_T *pstSrcImage, unsigned int *src_off)
{
    unsigned int offset = *src_off;

    if (offset + 1 > CVE_SRC_MAX) {
        return AML_ERR_CVE_BUF_FULL;
    }

    switch (pstSrcImage->enType) {
    case CVE_IMAGE_TYPE_U8C1:
    case CVE_IMAGE_TYPE_S8C1: {
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[0];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[0];
    } break;
    case CVE_IMAGE_TYPE_S8C2_PLANAR:
    case CVE_IMAGE_TYPE_YUV420SP:
    case CVE_IMAGE_TYPE_YUV422SP: {
        if (offset + 2 > CVE_SRC_MAX) {
            return AML_ERR_CVE_BUF_FULL;
        }
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[0];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[0];
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[1];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[1];
    } break;
    case CVE_IMAGE_TYPE_S8C2_PACKAGE:
    case CVE_IMAGE_TYPE_S16C1:
    case CVE_IMAGE_TYPE_U16C1: {
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[0];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[0] * 2;
    } break;
    case CVE_IMAGE_TYPE_U8C3_PACKAGE: {
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[0];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[0] * 3;
    } break;
    case CVE_IMAGE_TYPE_YUV420P:
    case CVE_IMAGE_TYPE_YUV422P:
    case CVE_IMAGE_TYPE_U8C3_PLANAR:
        if (offset + 3 > CVE_SRC_MAX) {
            return AML_ERR_CVE_BUF_FULL;
        }
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[0];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[0];
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[1];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[1];
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[2];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[2];
        break;
    case CVE_IMAGE_TYPE_S32C1:
    case CVE_IMAGE_TYPE_U32C1: {
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[0];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[0] * 4;
    } break;
    case CVE_IMAGE_TYPE_S64C1:
    case CVE_IMAGE_TYPE_U64C1: {
        init_params->src_addr[offset] = pstSrcImage->au64PhyAddr[0];
        init_params->src_stride[offset++] = pstSrcImage->au32Stride[0] * 8;
    } break;
    default:
        break;
    }

    *src_off = offset;

    return AML_SUCCESS;
}

static unsigned int fill_dst_image(cve_comm_init_params_t *init_params,
                                   CVE_DST_IMAGE_T *pstDstImage, unsigned int *dst_off)
{
    unsigned int offset = *dst_off;

    if (offset + 1 > CVE_DST_MAX) {
        return AML_ERR_CVE_BUF_FULL;
    }

    switch (pstDstImage->enType) {
    case CVE_IMAGE_TYPE_U8C1:
    case CVE_IMAGE_TYPE_S8C1: {
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[0];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[0];
    } break;
    case CVE_IMAGE_TYPE_S8C2_PLANAR:
    case CVE_IMAGE_TYPE_YUV420SP:
    case CVE_IMAGE_TYPE_YUV422SP: {
        if (offset + 2 > CVE_DST_MAX) {
            return AML_ERR_CVE_BUF_FULL;
        }
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[0];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[0];
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[1];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[1];
    } break;
    case CVE_IMAGE_TYPE_S8C2_PACKAGE:
    case CVE_IMAGE_TYPE_S16C1:
    case CVE_IMAGE_TYPE_U16C1: {
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[0];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[0] * 2;
    } break;
    case CVE_IMAGE_TYPE_U8C3_PACKAGE: {
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[0];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[0] * 3;
    } break;
    case CVE_IMAGE_TYPE_YUV420P:
    case CVE_IMAGE_TYPE_YUV422P:
    case CVE_IMAGE_TYPE_U8C3_PLANAR:
        if (offset + 3 > CVE_DST_MAX) {
            return AML_ERR_CVE_BUF_FULL;
        }
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[0];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[0];
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[1];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[1];
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[2];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[2];
        break;
    case CVE_IMAGE_TYPE_S32C1:
    case CVE_IMAGE_TYPE_U32C1: {
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[0];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[0] * 4;
    } break;
    case CVE_IMAGE_TYPE_S64C1:
    case CVE_IMAGE_TYPE_U64C1: {
        init_params->dst_addr[offset] = pstDstImage->au64PhyAddr[0];
        init_params->dst_stride[offset++] = pstDstImage->au32Stride[0] * 8;
    } break;
    default:
        break;
    }

    *dst_off = offset;

    return AML_SUCCESS;
}

static unsigned int fill_src_raw(cve_comm_init_params_t *init_params, CVE_SRC_RAW_T *pstSrcRaw,
                                 unsigned int *src_off)
{
    unsigned int offset = *src_off;

    if (offset + 1 > CVE_SRC_MAX) {
        return AML_ERR_CVE_BUF_FULL;
    }

    switch (pstSrcRaw->enMode) {
    case CVE_RAW_MODE_RAW6:
    case CVE_RAW_MODE_RAW7:
    case CVE_RAW_MODE_RAW8: {
        init_params->src_addr[offset] = pstSrcRaw->u64PhyAddr;
        init_params->src_stride[offset++] = pstSrcRaw->u32Stride;
    } break;
    case CVE_RAW_MODE_RAW10: {
        init_params->src_addr[offset] = pstSrcRaw->u64PhyAddr;
        init_params->src_stride[offset++] = CVE_ALIGN_UP((pstSrcRaw->u32Stride * 5 + 3) / 4, 16);

    } break;
    case CVE_RAW_MODE_RAW12: {
        init_params->src_addr[offset] = pstSrcRaw->u64PhyAddr;
        init_params->src_stride[offset++] = CVE_ALIGN_UP((pstSrcRaw->u32Stride * 3 + 1) / 2, 16);

    } break;
    case CVE_RAW_MODE_RAW14: {
        init_params->src_addr[offset] = pstSrcRaw->u64PhyAddr;
        init_params->src_stride[offset++] = init_params->src_stride[0] * 2;

    } break;
    default:
        break;
    }
    *src_off = offset;

    return AML_SUCCESS;
}

static unsigned int fill_dst_mem(cve_comm_init_params_t *init_params, CVE_DST_MEM_INFO_T *pstDstMem,
                                 unsigned int stride, unsigned int *dst_off)
{
    unsigned int offset = *dst_off;

    if (offset + 1 > CVE_DST_MAX) {
        return AML_ERR_CVE_BUF_FULL;
    }

    init_params->dst_addr[offset] = pstDstMem->u64PhyAddr;
    init_params->dst_stride[offset++] = stride;
    *dst_off = offset;

    return AML_SUCCESS;
}

static unsigned int fill_src_mem(cve_comm_init_params_t *init_params, CVE_DST_MEM_INFO_T *pstSrcMem,
                                 unsigned int stride, unsigned int *src_off)
{
    unsigned int offset = *src_off;

    if (offset + 1 > CVE_SRC_MAX) {
        return AML_ERR_CVE_BUF_FULL;
    }

    init_params->src_addr[offset] = pstSrcMem->u64PhyAddr;
    init_params->src_stride[offset++] = stride;
    *src_off = offset;

    return AML_SUCCESS;
}

static unsigned int fill_src_data(cve_comm_init_params_t *init_params, CVE_SRC_DATA_T *pstSrcData,
                                  unsigned int *src_off)
{
    unsigned int offset = *src_off;

    if (offset + 1 > CVE_SRC_MAX) {
        return AML_ERR_CVE_BUF_FULL;
    }

    init_params->src_addr[offset] = pstSrcData->u64PhyAddr;
    init_params->src_stride[offset++] = pstSrcData->u32Stride;
    *src_off = offset;

    return AML_SUCCESS;
}

static unsigned int fill_dst_data(cve_comm_init_params_t *init_params, CVE_DST_DATA_T *pstDstData,
                                  unsigned int *dst_off)
{
    unsigned int offset = *dst_off;

    if (offset + 1 > CVE_DST_MAX) {
        return AML_ERR_CVE_BUF_FULL;
    }

    init_params->dst_addr[offset] = pstDstData->u64PhyAddr;
    init_params->dst_stride[offset++] = pstDstData->u32Stride;
    *dst_off = offset;

    return AML_SUCCESS;
}

static int cve_proc_show(struct seq_file *proc_entry, void *arg)
{
    cve_task_info_t *ti;
    cve_cq_desc_t *cqb;
    cve_cq_desc_t *cqw;
    cve_run_time_info_t *rti;
    cve_op_invoke_count_t *ic;

    ti = &cve_context.task_info;
    cqw = &cve_context.cq_desc[cve_context.queue_wait];
    cqb = &cve_context.cq_desc[CVE_STATUS_CQ1 - cve_context.queue_wait];
    rti = &cve_context.run_time_info;
    ic = &cve_context.invoke_count;

    seq_printf(
        proc_entry,
        "\n-------------------------------MODULE PARAM--------------------------------------\n");
    seq_printf(proc_entry, "%16s\n", "max_node_num");
    seq_printf(proc_entry, "%16u\n", cve_node_num);
    seq_printf(
        proc_entry,
        "\n-------------------------------CVE QUEUE INFO------------------------------------\n");
    seq_printf(proc_entry, "%8s%8s%13s%13s%13s%13s\n", "Wait", "Busy", "WaitCurId", "WaitEndId",
               "BusyCurId", "BusyEndId");
    if (!cve_state) {
        seq_printf(proc_entry, "%8d%8d%13d%13d%13d%13d\n", cve_context.queue_wait,
                   cve_context.queue_busy, cqw->cur_cmd_id, cqw->end_cmd_id, cqb->cur_cmd_id,
                   cqb->end_cmd_id);
    }
    seq_printf(
        proc_entry,
        "\n--------------------------------CVE TASK INFO------------------------------------\n");
    seq_printf(proc_entry, "%7s%11s%10s%10s%11s%11s\n", "Hnd", "TaskFsh", "LastId", "TaskId",
               "HndWrap", "FshWrap");
    if (!cve_state)
        seq_printf(proc_entry, "%7d%11d%10d%10d%11d%11d\n", ti->cmd_handle, ti->cmd_finish_cnt,
                   ti->last_finish_cmd_id, ti->cur_finish_cmd_id, ti->cmd_handle_wrap,
                   ti->finish_cmd_wrap);
    seq_printf(
        proc_entry,
        "\n-----------------------------------CVE RUN-TIME INFO-----------------------------\n");
    seq_printf(proc_entry, "%12s%13s%16s%22s%15s%9s%9s\n", "LastInst", "CntPerSec", "MaxCntPerSec",
               "TotalIntCntLastSec", "TotalIntCnt", "QTCnt", "STCnt");
    if (!cve_state)
        seq_printf(proc_entry, "%12d%13d%16d%22d%15d%9d%9d\n", rti->last_instant,
                   rti->last_int_cnt_per_sec, rti->max_int_cnt_per_sec, rti->total_int_cnt_last_sec,
                   rti->total_int_cnt, rti->query_timeout_cnt, rti->system_timeout_cnt);
    seq_printf(proc_entry, "\n%10s%11s%16s%17s%18s%9s%9s\n", "CostTm", "MCostTm", "CostTmPerSec",
               "MCostTmPerSec", "TotalIntCostTm", "RunTm", "(us)");
    if (!cve_state)
        seq_printf(proc_entry, "%10d%11d%16d%17d%18d%9d\n", rti->last_int_cost_time,
                   rti->int_cost_time_max, rti->last_persec_int_cost_time,
                   rti->persec_int_cost_time_max, rti->total_int_cost_time,
                   rti->total_cve_run_time);
    seq_printf(
        proc_entry,
        "\n----------------------------------CVE INVOKE INFO--------------------------------\n");
    seq_printf(proc_entry, "\n%13s%13s%13s%13s%13s%13s%13s%13s\n", "DMA", "Filter", "CSC", "FltCsc",
               "Sobel", "MagAng", "Dilate", "Erode");
    if (!cve_state)
        seq_printf(proc_entry, "%13d%13d%13d%13d%13d%13d%13d%13d\n", ic->dma, ic->filter, ic->csc,
                   ic->filter_and_csc, ic->sobel, ic->mag_and_ang, ic->dilate, ic->erode);
    seq_printf(proc_entry, "\n%13s%13s%13s%13s%13s%13s%13s%13s\n", "Thresh", "Integ", "Hist",
               "ThreshS16", "ThreshU16", "And", "Sub", "Or");
    if (!cve_state)
        seq_printf(proc_entry, "%13d%13d%13d%13d%13d%13d%13d%13d\n", ic->thresh, ic->integ,
                   ic->hist, ic->thresh_s16, ic->thresh_u16, ic->and, ic->sub, ic->or);
    seq_printf(proc_entry, "\n%13s%13s%13s%13s%13s%13s%13s%13s\n", "Add", "Xor", "16to8", "OrdStat",
               "Map", "EqualH", "NCC", "SAD");
    if (!cve_state)
        seq_printf(proc_entry, "%13d%13d%13d%13d%13d%13d%13d%13d\n", ic->add, ic->xor,
                   ic->_16bit_to_8bit, ic->ord_stat_filter, ic->map, ic->equalize_hist, ic->ncc,
                   ic->sad);
    seq_printf(proc_entry, "\n%13s%13s%13s%13s%13s%13s%13s%13s\n", "CCL", "GMM", "Canny", "LBP",
               "NormGrad", "LK", "ShiTomasi", "GradFg");
    if (!cve_state)
        seq_printf(proc_entry, "%13d%13d%13d%13d%13d%13d%13d%13d\n", ic->ccl, ic->gmm,
                   ic->canny_edge, ic->lbp, ic->nrom_grad, ic->lk_optial_flow_pry,
                   ic->st_candi_corner, ic->grad_fg);
    seq_printf(proc_entry, "\n%13s%13s%13s\n", "MatchMod", "UpdateMod", "TOF");
    if (!cve_state)
        seq_printf(proc_entry, "%13d%13d%13d\n", ic->match_bg_model, ic->update_bg_model, ic->tof);

    return 0;
}

static int cve_post_process(char *cmd_buf, unsigned int cmd_line_num, CVE_HANDLE *cveHandle,
                            AML_BOOL_E bInstant, cve_op_io_info_t *info)
{
    unsigned long flags;
    cve_cmd_desc_t *cmd_desc;
    cve_cmd_desc_t *cmd_desc_tmp;
    int ret = 0;

    AML_ASSERT(cmd_buf != NULL);
    AML_ASSERT(cveHandle != NULL);
    AML_ASSERT((bInstant == AML_TRUE) || (bInstant == AML_FALSE));

    spin_lock_irqsave(&cve_spinlock, flags);
    ret = request_op_cmd(&cmd_desc, 1);
    if (ret) {
        spin_unlock_irqrestore(&cve_spinlock, flags);
        return ret;
    }

    cmd_desc->instant = bInstant;
    cmd_desc->cmd_line_num = cmd_line_num;
    INIT_LIST_HEAD(&cmd_desc->list);
    if (info == NULL) {
        memset((void *)&cmd_desc->io_info, 0, sizeof(cve_op_io_info_t));
    } else {
        memcpy((void *)&cmd_desc->io_info, (void *)info, sizeof(cve_op_io_info_t));
    }

    ret = cve_create_task(cmd_desc, cmd_buf);
    if (ret) {
        CVE_ERR_TRACE("creat task failed!\n");
        spin_unlock_irqrestore(&cve_spinlock, flags);
        return ret;
    }
    cve_manage_handle(cveHandle, 1);
    cmd_desc->cveHandle = *cveHandle;
    if (cve_context.queue_busy == CVE_STATUS_IDLE) {
        cve_context.queue_busy = cve_context.queue_wait;
        cve_context.queue_wait = CVE_STATUS_CQ1 - cve_context.queue_wait;
        if (cmd_desc->task_desc->bInput) {
            list_for_each_entry(cmd_desc_tmp, &cmd_desc->task_desc->cmd_list, list)
            {
                if (cmd_desc_tmp->io_info.inp_flags != 0 &&
                    cmd_desc_tmp->io_info.inp_phys_addr != 0) {
                    spin_unlock_irqrestore(&cve_spinlock, flags);
                    cve_input_process(&cmd_desc_tmp->io_info);
                    spin_lock_irqsave(&cve_spinlock, flags);
                }
            }
        }
        cve_start_task(cmd_desc->task_desc);
    }
    spin_unlock_irqrestore(&cve_spinlock, flags);

    return ret;
}

static void cve_common_params_init(cve_comm_params_t *comm_params,
                                   cve_comm_init_params_t *init_params)
{
    comm_params->reg_03.bits.src_addr0 = init_params->src_addr[0] >> 4;
    comm_params->reg_04.bits.src_addr1 = init_params->src_addr[1] >> 4;
    comm_params->reg_05.bits.src_addr2 = init_params->src_addr[2] >> 4;
    comm_params->reg_06.bits.src_addr3 = init_params->src_addr[3] >> 4;
    comm_params->reg_07.bits.src_addr4 = init_params->src_addr[4] >> 4;
    comm_params->reg_08.bits.src_addr5 = init_params->src_addr[5] >> 4;
    comm_params->reg_09.bits.src_addr6 = init_params->src_addr[6] >> 4;
    comm_params->reg_0a.bits.src_addr7 = init_params->src_addr[7] >> 4;
    comm_params->reg_0b.bits.dst_addr0 = init_params->dst_addr[0] >> 4;
    comm_params->reg_0c.bits.dst_addr1 = init_params->dst_addr[1] >> 4;
    comm_params->reg_0d.bits.dst_addr2 = init_params->dst_addr[2] >> 4;
    comm_params->reg_0e.bits.dst_addr3 = init_params->dst_addr[3] >> 4;
    comm_params->reg_0f.bits.dst_addr4 = init_params->dst_addr[4] >> 4;

    comm_params->reg_10.bits.src_stride_0 = init_params->src_stride[0] >> 4;
    comm_params->reg_10.bits.src_stride_1 = init_params->src_stride[1] >> 4;
    comm_params->reg_11.bits.src_stride_3 = init_params->src_stride[3] >> 4;
    comm_params->reg_11.bits.src_stride_2 = init_params->src_stride[2] >> 4;
    comm_params->reg_12.bits.src_stride_5 = init_params->src_stride[5] >> 4;
    comm_params->reg_12.bits.src_stride_4 = init_params->src_stride[4] >> 4;
    comm_params->reg_13.bits.src_stride_7 = init_params->src_stride[7] >> 4;
    comm_params->reg_13.bits.src_stride_6 = init_params->src_stride[6] >> 4;
    comm_params->reg_14.bits.dst_stride_1 = init_params->dst_stride[1] >> 4;
    comm_params->reg_14.bits.dst_stride_0 = init_params->dst_stride[0] >> 4;
    comm_params->reg_15.bits.dst_stride_3 = init_params->dst_stride[3] >> 4;
    comm_params->reg_15.bits.dst_stride_2 = init_params->dst_stride[2] >> 4;
    comm_params->reg_16.bits.dst_stride_4 = init_params->dst_stride[4] >> 4;

    comm_params->reg_17.bits.src_image_width = init_params->src_width;
    comm_params->reg_17.bits.src_image_height = init_params->src_height;
    comm_params->reg_18.bits.dst_image_width = init_params->dst_width;
    comm_params->reg_18.bits.dst_image_height = init_params->dst_height;
    comm_params->reg_19.bits.cve_crop_xstart = init_params->xstart;
    comm_params->reg_19.bits.cve_crop_ystart = init_params->ystart;
    comm_params->reg_1a.bits.cve_crop_xsize = init_params->xSize;
    comm_params->reg_1a.bits.cve_crop_ysize = init_params->ySize;
}

static unsigned int dma_task_cmd_queue(cve_op_dma_params_t *dma_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(dma_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(dma_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_5(CVE_DMA_REG0, dma_params->reg_1b.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_DMA_REG1, dma_params->reg_1c.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_DMA_REG2, dma_params->reg_1d.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_DMA_REG3, dma_params->reg_1e.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(dma)

    CMD_QUEUE_RETURN
}

static unsigned int alu_task_cmd_queue(cve_op_alu_params_t *alu_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(alu_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(alu_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_ALU_REG0, alu_params->alu_31.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_ALU_REG1, alu_params->alu_32.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SUB_THRESH_RATIO, alu_params->sub_99.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(alu)

    CMD_QUEUE_RETURN
}

static unsigned int filter_task_cmd_queue(cve_op_filter_params_t *filter_params,
                                          unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(filter_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(filter_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_FILTER_REG2, filter_params->filter_1f.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG1_0, filter_params->filter_20.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG1_1, filter_params->filter_21.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_FILTER_REG2_3, filter_params->filter_22.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_0, filter_params->filter_67.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_1, filter_params->filter_68.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_2, filter_params->filter_69.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_3, filter_params->filter_6a.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_4, filter_params->filter_6b.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_5, filter_params->filter_6c.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_FILTER_REG0_6, filter_params->filter_6d.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(filter)

    CMD_QUEUE_RETURN
}

unsigned int csc_task_cmd_queue(cve_op_csc_params_t *csc_params, unsigned int *cmd_buf)
{

    CMD_QUEUE_CREATE_PREPARE(csc_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(csc_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_6(CVE_CSC_REG0, csc_params->csc_23.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG, csc_params->csc_24.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG1, csc_params->csc_6e.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_CSC_REG1_1, csc_params->csc_6f.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_CSC_REG2_0, csc_params->csc_70.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_CSC_REG2_1, csc_params->csc_71.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_CSC_REG2_2, csc_params->csc_72.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG3, csc_params->csc_73.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG3_1, csc_params->csc_74.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(csc)

    CMD_QUEUE_RETURN
}

unsigned int filter_and_csc_task_cmd_queue(cve_op_filter_and_csc_params_t *filter_and_csc_params,
                                           unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(filter_and_csc_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(filter_and_csc_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_6(CVE_CSC_REG0, filter_and_csc_params->csc_23.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG, filter_and_csc_params->csc_24.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG1, filter_and_csc_params->csc_6e.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_CSC_REG1_1, filter_and_csc_params->csc_6f.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_CSC_REG2_0, filter_and_csc_params->csc_70.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_CSC_REG2_1, filter_and_csc_params->csc_71.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_CSC_REG2_2, filter_and_csc_params->csc_72.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG3, filter_and_csc_params->csc_73.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CSC_REG3_1, filter_and_csc_params->csc_74.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_0, filter_and_csc_params->filter_67.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_1, filter_and_csc_params->filter_68.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_2, filter_and_csc_params->filter_69.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_3, filter_and_csc_params->filter_6a.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_4, filter_and_csc_params->filter_6b.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_5, filter_and_csc_params->filter_6c.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_FILTER_REG0_6, filter_and_csc_params->filter_6d.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(filter_and_csc)

    CMD_QUEUE_RETURN
}

static unsigned int sobel_task_cmd_queue(cve_op_sobel_params_t *sobel_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(sobel_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(sobel_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG0, sobel_params->sobel_25.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_0, sobel_params->sobel_75.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_1, sobel_params->sobel_76.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_2, sobel_params->sobel_77.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_3, sobel_params->sobel_78.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_4, sobel_params->sobel_79.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_5, sobel_params->sobel_7a.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG1_6, sobel_params->sobel_7b.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_0, sobel_params->sobel_7c.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_1, sobel_params->sobel_7d.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_2, sobel_params->sobel_7e.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_3, sobel_params->sobel_7f.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_4, sobel_params->sobel_80.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_5, sobel_params->sobel_81.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG2_6, sobel_params->sobel_82.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(sobel)

    CMD_QUEUE_RETURN
}

static unsigned int erode_dilate_task_cmd_queue(cve_op_erode_dilate_params_t *erode_dilate_params,
                                                unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(erode_dilate_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(erode_dilate_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_1(CVE_ERODEDILATE_REG0, erode_dilate_params->erode_dilate_2E.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_0, erode_dilate_params->filter_67.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_1, erode_dilate_params->filter_68.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_2, erode_dilate_params->filter_69.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_3, erode_dilate_params->filter_6a.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_4, erode_dilate_params->filter_6b.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_FILTER_REG0_5, erode_dilate_params->filter_6c.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_FILTER_REG0_6, erode_dilate_params->filter_6d.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(erode_dilate)

    CMD_QUEUE_RETURN
}

static unsigned int thresh_task_cmd_queue(cve_op_thresh_params_t *thresh_params,
                                          unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(thresh_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(thresh_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_THRESH_REG0, thresh_params->thresh_2f.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_THRESH_REG1, thresh_params->thresh_30.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(thresh)

    CMD_QUEUE_RETURN
}

static unsigned int thresh_s16_task_cmd_queue(cve_op_thresh_s16_params_t *thresh_s16_params,
                                              unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(thresh_s16_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(thresh_s16_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_4(CVE_THRESHS16_REG0, thresh_s16_params->thresh_s16_35.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_THRESHS16_REG1, thresh_s16_params->thresh_s16_36.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(thresh_s16)

    CMD_QUEUE_RETURN
}

static unsigned int thresh_u16_task_cmd_queue(cve_op_thresh_u16_params_t *thresh_u16_params,
                                              unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(thresh_u16_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(thresh_u16_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_4(CVE_THRESHU16_REG0, thresh_u16_params->thresh_u16_37.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_THRESHU16_REG1, thresh_u16_params->thresh_u16_38.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(thresh_u16)

    CMD_QUEUE_RETURN
}

static unsigned int integ_task_cmd_queue(cve_op_integ_params_t *integ_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(integ_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(integ_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_INTEG_REG0, integ_params->integ_33.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(integ)

    CMD_QUEUE_RETURN
}

static unsigned int hist_task_cmd_queue(cve_op_hist_params_t *hist_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(hist_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(hist_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_INTEG_REG0, hist_params->integ_33.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_EQHIST_REG0, hist_params->eqhist_34.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(hist)

    CMD_QUEUE_RETURN
}

static unsigned int
_16bit_to_8bit_task_cmd_queue(cve_op_16bit_to_8bit_params_t *_16bit_to_8bit_params,
                              unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(_16bit_to_8bit_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(_16bit_to_8bit_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_1(CVE_16BITTO8BIT_REG0, _16bit_to_8bit_params->_16bit_to_8bit_39.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_16BITTO8BIT_REG1, _16bit_to_8bit_params->_16bit_to_8bit_3a.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(_16bit_to_8bit)

    CMD_QUEUE_RETURN
}

static unsigned int
ord_stat_filter_task_cmd_queue(cve_op_ord_stat_filter_params_t *stat_filter_params,
                               unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(stat_filter_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(stat_filter_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_4(CVE_STATFILTER_REG0, stat_filter_params->stat_filter_3b.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(ord_stat_filter)

    CMD_QUEUE_RETURN
}

static unsigned int ncc_task_cmd_queue(cve_op_ncc_params_t *ncc_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(ncc_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(ncc_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_3(CVE_NCC_REG0, ncc_params->ncc_3c.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(ncc)

    CMD_QUEUE_RETURN
}

static unsigned int canny_edge_task_cmd_queue(cve_op_canny_edge_params_t *canny_params,
                                              unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(canny_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(canny_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_CANNY_REG0, canny_params->canny_4a.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_CANNY_REG2, canny_params->canny_4c.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_0, canny_params->sobel_75.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_1, canny_params->sobel_76.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_2, canny_params->sobel_77.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_3, canny_params->sobel_78.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_4, canny_params->sobel_79.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_5, canny_params->sobel_7a.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG1_6, canny_params->sobel_7b.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_0, canny_params->sobel_7c.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_1, canny_params->sobel_7d.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_2, canny_params->sobel_7e.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_3, canny_params->sobel_7f.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_4, canny_params->sobel_80.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_5, canny_params->sobel_81.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG2_6, canny_params->sobel_82.reg)

    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(canny_edge)

    CMD_QUEUE_RETURN
}

static unsigned int lbp_task_cmd_queue(cve_op_lbp_params_t *lbp_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(lbp_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(lbp_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_LBP_REG0, lbp_params->lbp_4d.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(lbp)

    CMD_QUEUE_RETURN
}

static unsigned int map_task_cmd_queue(cve_op_map_params_t *map_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(map_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(map_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_1(CVE_MAP_REG0, map_params->map_3c.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(map)

    CMD_QUEUE_RETURN
}

static unsigned int ccl_task_cmd_queue(cve_op_ccl_params_t *ccl_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(ccl_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(ccl_params->comm_params)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(ccl)

    CMD_QUEUE_RETURN
}

static unsigned int gmm_task_cmd_queue(cve_op_gmm_params_t *gmm_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(gmm_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(gmm_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_GMM_REG0, gmm_params->gmm_44.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_GMM_REG1, gmm_params->gmm_45.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_GMM_REG2, gmm_params->gmm_46.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_GMM_REG3, gmm_params->gmm_47.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_GMM_REG4, gmm_params->gmm_48.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_GMM_REG5, gmm_params->gmm_49.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(gmm)

    CMD_QUEUE_RETURN
}

static unsigned int norm_grad_task_cmd_queue(cve_op_norm_grad_params_t *norm_grad_params,
                                             unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(norm_grad_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(norm_grad_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_NORMGRAD_RETG0, norm_grad_params->norm_grad_4e.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_0, norm_grad_params->sobel_75.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_1, norm_grad_params->sobel_76.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_2, norm_grad_params->sobel_77.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_3, norm_grad_params->sobel_78.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_4, norm_grad_params->sobel_79.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_5, norm_grad_params->sobel_7a.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG1_6, norm_grad_params->sobel_7b.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_0, norm_grad_params->sobel_7c.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_1, norm_grad_params->sobel_7d.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_2, norm_grad_params->sobel_7e.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_3, norm_grad_params->sobel_7f.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_4, norm_grad_params->sobel_80.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_5, norm_grad_params->sobel_81.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG2_6, norm_grad_params->sobel_82.reg)

    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(norm_grad)

    CMD_QUEUE_RETURN
}

static unsigned int mag_and_ang_task_cmd_queue(cve_op_mag_and_ang_params_t *mag_and_ang_params,
                                               unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(mag_and_ang_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(mag_and_ang_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_3(CVE_MAGANDANG_REG0, mag_and_ang_params->mag_and_ang_26.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_0, mag_and_ang_params->sobel_75.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_1, mag_and_ang_params->sobel_76.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_2, mag_and_ang_params->sobel_77.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_3, mag_and_ang_params->sobel_78.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_4, mag_and_ang_params->sobel_79.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG1_5, mag_and_ang_params->sobel_7a.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG1_6, mag_and_ang_params->sobel_7b.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_0, mag_and_ang_params->sobel_7c.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_1, mag_and_ang_params->sobel_7d.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_2, mag_and_ang_params->sobel_7e.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_3, mag_and_ang_params->sobel_7f.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_4, mag_and_ang_params->sobel_80.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_SOBEL_REG2_5, mag_and_ang_params->sobel_81.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_SOBEL_REG2_6, mag_and_ang_params->sobel_82.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(mag_and_ang)

    CMD_QUEUE_RETURN
}

static unsigned int
match_bg_model_task_cmd_queue(cve_op_match_bg_model_params_t *match_bg_model_params,
                              unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(match_bg_model_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(match_bg_model_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_4(CVE_BGMODE_REG0, match_bg_model_params->bg_mode_27.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(match_bg_model)

    CMD_QUEUE_RETURN
}

static unsigned int
update_bg_model_task_cmd_queue(cve_op_update_bg_model_params_t *update_bg_model_params,
                               unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(update_bg_model_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(update_bg_model_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_1(CVE_BGMODE_REG0, update_bg_model_params->bg_mode_27.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_UPDATEBGMODE_REG0, update_bg_model_params->update_bg_mode_2a.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_UPDATEBGMODE_REG1, update_bg_model_params->update_bg_mode_2b.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(update_bg_model)

    CMD_QUEUE_RETURN
}

static unsigned int sad_task_cmd_queue(cve_op_sad_params_t *sad_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(sad_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(sad_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_SAD_REG0, sad_params->sad_64.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_SAD_REG1, sad_params->sad_65.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(sad)

    CMD_QUEUE_RETURN
}

static unsigned int grad_fg_task_cmd_queue(cve_op_grad_fg_params_t *grad_fg_params,
                                           unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(grad_fg_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(grad_fg_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_GRADFG_RETG0, grad_fg_params->grad_fg_66.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(grad_fg)

    CMD_QUEUE_RETURN
}

static unsigned int
st_candi_corner_task_cmd_queue(cve_op_st_candi_corner_params_t *st_corner_params,
                               unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(st_corner_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(st_corner_params->comm_params)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(st_corner)

    CMD_QUEUE_RETURN
}

static unsigned int build_lk_optical_flow_pyr_task_cmd_queue(
    cve_op_build_lk_optical_flow_pyr_params_t *build_lk_optical_flow_pyr_params,
    unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(build_lk_optical_flow_pyr_params->comm_params.reg_02.bits.cve_op_type,
                             cmd_buf)
    CMD_QUEUE_ADD_COMMON(build_lk_optical_flow_pyr_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_1(CVE_BDLK_REG0, build_lk_optical_flow_pyr_params->bdlk_4f.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(build_lk_optical_flow_pyr)

    CMD_QUEUE_RETURN
}

static unsigned int
lk_optical_flow_pyr_task_cmd_queue(cve_op_lk_optical_flow_pyr_params_t *lk_optical_flow_pyr_params,
                                   unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(lk_optical_flow_pyr_params->comm_params.reg_02.bits.cve_op_type,
                             cmd_buf)
    CMD_QUEUE_ADD_COMMON(lk_optical_flow_pyr_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_3(CVE_LK_REG0, lk_optical_flow_pyr_params->lk_50.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_LK_REG1, lk_optical_flow_pyr_params->lk_51.reg)
    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(lk_optical_flow_pyr)

    CMD_QUEUE_RETURN
}

static unsigned int tof_task_cmd_queue(cve_op_tof_params_t *tof_params, unsigned int *cmd_buf)
{
    CMD_QUEUE_CREATE_PREPARE(tof_params->comm_params.reg_02.bits.cve_op_type, cmd_buf)
    CMD_QUEUE_ADD_COMMON(tof_params->comm_params)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG0, tof_params->tof_83.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG1, tof_params->tof_84.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_TOF_REG3_0, tof_params->tof_85.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_TOF_REG3_1, tof_params->tof_86.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_TOF_REG3_2, tof_params->tof_87.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_TOF_REG3_3, tof_params->tof_88.reg)
    CMD_QUEUE_ADD_PROCESS_1(CVE_TOF_REG3_4, tof_params->tof_89.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG4, tof_params->tof_8a.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG5, tof_params->tof_8b.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_TOF_REG9_0, tof_params->tof_8c.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_TOF_REG9_1, tof_params->tof_8d.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_TOF_REG9_2, tof_params->tof_8e.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_TOF_REG9_3, tof_params->tof_8f.reg)
    CMD_QUEUE_ADD_PROCESS_3(CVE_TOF_REG9_4, tof_params->tof_90.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_TOF_REG10, tof_params->tof_91.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG11, tof_params->tof_92.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG12, tof_params->tof_93.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG13, tof_params->tof_94.reg)
    CMD_QUEUE_ADD_PROCESS_2(CVE_TOF_REG14, tof_params->tof_95.reg)
    CMD_QUEUE_ADD_PROCESS_4(CVE_TOF_REG15, tof_params->tof_96.reg)

    CMD_QUEUE_CREATE_END

    CMD_QUEUE_DEBUG_DUMP(tof)

    CMD_QUEUE_RETURN
}

static AML_U32 cve_check_dma_param(CVE_DATA_T *pstSrcDATA, CVE_DST_DATA_T *pstDstDATA,
                                   CVE_DMA_CTRL_T *pstDmaCtrl)
{
    AML_U8 hsegsize[5] = {2, 3, 4, 8, 16};
    int i;

    CVE_GET_IMAGE_SIZE(CVE_DMA);
    CVE_DATA_CHECK(CVE_DMA, pstDstDATA);
    if (pstDmaCtrl->enMode <= CVE_DMA_MODE_INTERVAL_COPY) {
        CVE_DATA_CHECK(CVE_DMA, pstSrcDATA);
        CVE_IMAGE_COMPARE(CVE_DMA, pstSrcDATA, pstDstDATA);
    }
    CVE_MODE_CHECK(CVE_DMA, pstDmaCtrl->enMode, CVE_DMA_MODE_BUTT);
    if (pstDmaCtrl->enMode == CVE_DMA_MODE_INTERVAL_COPY) {
        for (i = 0; i < 5; i++) {
            if (pstDmaCtrl->u8HorSegSize == hsegsize[i]) {
                break;
            }
        }
        if (i == 5) {
            CVE_ERR_TRACE("[CVE_DMA] pstDmaCtrl->u8HorSegSize(%d) set value error, value must in "
                          "{2, 3, 4, 8, 16}\n",
                          pstDmaCtrl->u8HorSegSize);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        if ((pstDmaCtrl->u8VerSegRows == 0) || (pstDmaCtrl->u8VerSegRows > 0xFF) ||
            (pstDmaCtrl->u8VerSegRows > ((pstDstDATA->u32Height > (0xFFFF / pstSrcDATA->u32Stride))
                                             ? (0xFFFF / pstSrcDATA->u32Stride)
                                             : pstDstDATA->u32Height))) {
            CVE_ERR_TRACE("[CVE_DMA] pstDmaCtrl->u8VerSegRows(%d), value must in [1, min(ysize, "
                          "255, 65535/srcStride)]\n",
                          pstDmaCtrl->u8HorSegSize);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        CVE_RANGE_CHECK(CVE_DMA, pstDmaCtrl->u8ElemSize, 0, pstDmaCtrl->u8HorSegSize);
    }
    return AML_SUCCESS;
}

static AML_S32 cve_check_luamStat_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_MEM_INFO_T *pstDstMem,
                                        CVE_RECT_U16_T *astCveLumaRect,
                                        CVE_LUMA_STAT_ARRAY_CTRL_T *pstLumaStatArrayCtrl)
{
    int i;

    CVE_GET_IMAGE_SIZE(CVE_LUMA);
    CVE_IMAGE_CHECK(CVE_LUMA, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_LUMA, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_MEM_CHECK(CVE_LUMA, pstDstMem);
    if (pstLumaStatArrayCtrl->u8MaxLumaRect > CVE_LUMA_RECT_MAX) {
        CVE_ERR_TRACE("[CVE_LUMA] pstLumaStatArrayCtrl->u8MaxLumaRect(%d) set value error, value "
                      "must in[1, %d]\n",
                      pstLumaStatArrayCtrl->u8MaxLumaRect, CVE_LUMA_RECT_MAX);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    } else {
        for (i = 0; i < pstLumaStatArrayCtrl->u8MaxLumaRect; i++) {
            if ((astCveLumaRect[i].u16Width < 8) || (astCveLumaRect[i].u16Height < 8)) {
                CVE_ERR_TRACE("[CVE_LUMA] astCveLumaRect[%d].u16Width(%d), "
                              "astCveLumaRect[%d].u16Height(%d) value must >8\n",
                              i, astCveLumaRect[i].u16Width, i, astCveLumaRect[i].u16Height);
                return AML_ERR_CVE_ILLEGAL_PARAM;
            }
            if (((astCveLumaRect[i].u16X + astCveLumaRect[i].u16Width) > pstSrcImage->u32Width) ||
                ((astCveLumaRect[i].u16Y + astCveLumaRect[i].u16Height) > pstSrcImage->u32Height)) {
                CVE_ERR_TRACE("[CVE_LUMA] "
                              "xstart+xsize<=src_width,ystart+ysize<=src_height,xsize=dst_width,"
                              "ysize=dst_height.\n");
                CVE_ERR_TRACE(
                    "[CVE_LUMA] astCveLumaRect[%d].u16X(%d),astCveLumaRect[%d].u16Width(%d), "
                    "srcWidth(%d)\n",
                    i, astCveLumaRect[i].u16X, i, astCveLumaRect[i].u16Width,
                    pstSrcImage->u32Width);
                CVE_ERR_TRACE(
                    "[CVE_LUMA] astCveLumaRect[%d].u16Y(%d),astCveLumaRect[%d].u16Height(%d), "
                    "srcHeight(%d)\n",
                    i, astCveLumaRect[i].u16X, i, astCveLumaRect[i].u16Width,
                    pstSrcImage->u32Height);
                return AML_ERR_CVE_ILLEGAL_PARAM;
            }
        }
    }
    return AML_SUCCESS;
}

static AML_S32 cve_check_filter_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                      CVE_FILTER_CTRL_T *pstFilterCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_FILTER);
    CVE_IMAGE_CHECK(CVE_FILTER, pstSrcImage);
    if ((pstSrcImage->enType != CVE_IMAGE_TYPE_U8C1) &&
        (pstSrcImage->enType != CVE_IMAGE_TYPE_YUV420SP) &&
        (pstSrcImage->enType != CVE_IMAGE_TYPE_YUV422SP)) {
        CVE_ERR_TRACE("[CVE_FILTER] pstSrcImage->enType(%d) set error, not support this type\n",
                      pstSrcImage->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    CVE_IMAGE_CHECK(CVE_FILTER, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_FILTER, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_IMAGE_TYPE_EQUAL(CVE_FILTER, pstSrcImage, pstDstImage);
    if (pstFilterCtrl->u8Norm > 13) {
        CVE_ERR_TRACE("[CVE_FILTER] pstFilterCtrl->u8Norm(%d) set error, need set on [0, 13]\n",
                      pstFilterCtrl->u8Norm);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    return AML_SUCCESS;
}

static AML_S32 cve_check_csc_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                   CVE_CSC_CTRL_T *pstCscCtrl)
{
    AML_U8 csc_image_type_support[] = {CVE_IMAGE_TYPE_YUV420SP, CVE_IMAGE_TYPE_YUV422SP,
                                       CVE_IMAGE_TYPE_U8C3_PACKAGE, CVE_IMAGE_TYPE_U8C3_PLANAR};
    int i;

    CVE_GET_IMAGE_SIZE(CVE_CSC);
    CVE_IMAGE_CHECK(CVE_CSC, pstSrcImage);
    CVE_IMAGE_CHECK(CVE_CSC, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_CSC, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    for (i = 0; i < 4; i++) {
        if (pstSrcImage->enType == csc_image_type_support[i]) {
            break;
        }
    }
    if (i == 4) {
        CVE_ERR_TRACE("[CVE_CSC] pstSrcImage->enType(%d) not support.\n", pstSrcImage->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    for (i = 0; i < 4; i++) {
        if (pstDstImage->enType == csc_image_type_support[i]) {
            break;
        }
    }
    if (i == 4) {
        CVE_ERR_TRACE("[CVE_CSC] stDstImage->enType(%d) not support.\n", pstDstImage->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    CVE_MODE_CHECK(CVE_CSC, pstCscCtrl->enMode, CVE_CSC_MODE_BUTT);
    return AML_SUCCESS;
}

static AML_S32 cve_check_filter_csc_param(CVE_SRC_IMAGE_T *pstSrcImage,
                                          CVE_DST_IMAGE_T *pstDstImage,
                                          CVE_FILTER_AND_CSC_CTRL_T *pstFilterCscCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_FILTER_AND_CSC);
    CVE_IMAGE_CHECK(CVE_FILTER_AND_CSC, pstSrcImage);
    CVE_IMAGE_CHECK(CVE_FILTER_AND_CSC, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_FILTER_AND_CSC, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_FILTER_AND_CSC, pstFilterCscCtrl->enMode, CVE_CSC_MODE_BUTT);
    if ((pstSrcImage->enType != CVE_IMAGE_TYPE_YUV420SP) &&
        (pstSrcImage->enType != CVE_IMAGE_TYPE_YUV422SP)) {
        CVE_ERR_TRACE("[CVE_FILTER_AND_CSC] pstSrcImage->enType(%d) not support, only yuv420sp and "
                      "yuv422sp.\n",
                      pstDstImage->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    if ((pstDstImage->enType != CVE_IMAGE_TYPE_U8C3_PACKAGE) &&
        (pstDstImage->enType != CVE_IMAGE_TYPE_U8C3_PLANAR)) {
        CVE_ERR_TRACE("[CVE_FILTER_AND_CSC] pstDstImage->enType(%d) not support, only U8C3_PACKAGE "
                      "and U8C3_PLANAR.\n",
                      pstDstImage->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_sobel_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstH,
                                     CVE_DST_IMAGE_T *pstDstV, CVE_SOBEL_CTRL_T *pstSobelCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_SOBEL);
    CVE_IMAGE_CHECK(CVE_SOBEL, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_SOBEL, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_MODE_CHECK(CVE_SOBEL, pstSobelCtrl->enOutCtrl, CVE_SOBEL_OUT_CTRL_BUTT);
    if ((pstSobelCtrl->enOutCtrl == CVE_SOBEL_OUT_CTRL_BOTH) ||
        (pstSobelCtrl->enOutCtrl == CVE_SOBEL_OUT_CTRL_HOR)) {
        CVE_IMAGE_CHECK(CVE_SOBEL, pstDstH);
        CVE_IMAGE_TYPE_CHECK(CVE_SOBEL, pstDstH, CVE_IMAGE_TYPE_S16C1);
        CVE_RESOLUTION_EQUAL(CVE_SOBEL, pstSrcImage, pstDstH, RESOLUTION_DS_NONE);
    }
    if ((pstSobelCtrl->enOutCtrl == CVE_SOBEL_OUT_CTRL_BOTH) ||
        (pstSobelCtrl->enOutCtrl == CVE_SOBEL_OUT_CTRL_VER)) {
        CVE_IMAGE_CHECK(CVE_SOBEL, pstDstV);
        CVE_IMAGE_TYPE_CHECK(CVE_SOBEL, pstDstV, CVE_IMAGE_TYPE_S16C1);
        CVE_RESOLUTION_EQUAL(CVE_SOBEL, pstSrcImage, pstDstV, RESOLUTION_DS_NONE);
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_mag_and_ang_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstMag,
                                           CVE_DST_IMAGE_T *pstDstAng,
                                           CVE_MAG_AND_ANG_CTRL_T *pstMagAndAngCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_MAG_AND_ANG);
    CVE_IMAGE_CHECK(CVE_MAG_AND_ANG, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_MAG_AND_ANG, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_MAG_AND_ANG, pstDstMag);
    CVE_RESOLUTION_EQUAL(CVE_MAG_AND_ANG, pstSrcImage, pstDstMag, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_MAG_AND_ANG, pstMagAndAngCtrl->enOutCtrl, CVE_MAG_AND_ANG_OUT_CTRL_BUTT);
    CVE_IMAGE_TYPE_CHECK(CVE_MAG_AND_ANG, pstDstMag, CVE_IMAGE_TYPE_U16C1);
    if ((pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_MAG_AND_ANG) ||
        (pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_HOG)) {
        CVE_IMAGE_CHECK(CVE_MAG_AND_ANG, pstDstAng);
        CVE_RESOLUTION_EQUAL(CVE_MAG_AND_ANG, pstSrcImage, pstDstAng, RESOLUTION_DS_NONE);
        if (pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_MAG) {
            CVE_IMAGE_TYPE_CHECK(CVE_MAG_AND_ANG, pstDstAng, CVE_IMAGE_TYPE_U8C1);
        } else if (pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_HOG) {
            CVE_IMAGE_TYPE_CHECK(CVE_MAG_AND_ANG, pstDstAng, CVE_IMAGE_TYPE_U16C1);
        }
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_match_bg_model_param(CVE_SRC_IMAGE_T *pstCurImg,
                                              CVE_SRC_IMAGE_T *pstPreImg,
                                              CVE_MEM_INFO_T *pstBgModel, CVE_DST_IMAGE_T *pstFg,
                                              CVE_DST_IMAGE_T *pstBg, CVE_DST_IMAGE_T *pstCurDiffBg,
                                              CVE_DST_IMAGE_T *pstFrmDiff,
                                              CVE_DST_MEM_INFO_T *pstStatData,
                                              CVE_MATCH_BG_MODEL_CTRL_T *pstMatchBgModelCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_MATCH_BG_MODEL);
    CVE_IMAGE_CHECK(CVE_MATCH_BG_MODEL, pstCurImg);
    CVE_IMAGE_TYPE_CHECK(CVE_MATCH_BG_MODEL, pstCurImg, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_MATCH_BG_MODEL, pstPreImg);
    CVE_MEM_CHECK(CVE_MATCH_BG_MODEL, pstBgModel);
    CVE_MEM_CHECK(CVE_MATCH_BG_MODEL, pstStatData);
    CVE_RESOLUTION_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstPreImg, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstFg,
                         pstMatchBgModelCtrl->enDownScaleMode);
    CVE_RESOLUTION_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstBg,
                         pstMatchBgModelCtrl->enDownScaleMode);
    CVE_RESOLUTION_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstCurDiffBg,
                         pstMatchBgModelCtrl->enDownScaleMode);
    CVE_RESOLUTION_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstFrmDiff,
                         pstMatchBgModelCtrl->enDownScaleMode);
    CVE_IMAGE_TYPE_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstPreImg);
    CVE_IMAGE_TYPE_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstFg);
    CVE_IMAGE_TYPE_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstBg);
    CVE_IMAGE_TYPE_EQUAL(CVE_MATCH_BG_MODEL, pstCurImg, pstCurDiffBg);
    CVE_RANGE_CHECK(CVE_MATCH_BG_MODEL, pstMatchBgModelCtrl->u8q4DistThr, 0, 4095);
    CVE_RANGE_CHECK(CVE_MATCH_BG_MODEL, pstMatchBgModelCtrl->u8GrayThr, 0, 255);
    CVE_MODE_CHECK(CVE_MATCH_BG_MODEL, pstMatchBgModelCtrl->enOutputMode,
                   CVE_MATCH_BG_MODEL_OUTPUT_MODE_BUTT);
    CVE_MODE_CHECK(CVE_MATCH_BG_MODEL, pstMatchBgModelCtrl->enDownScaleMode,
                   CVE_MATCH_BG_MODEL_DOWN_SCALE_MODE_BUTT);
    return AML_SUCCESS;
}

static AML_S32 cve_check_dilate_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                      CVE_DILATE_CTRL_T *pstDilateCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_DILATE);
    CVE_IMAGE_CHECK(CVE_DILATE, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_DILATE, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_DILATE, pstDstImage);
    CVE_IMAGE_TYPE_EQUAL(CVE_DILATE, pstSrcImage, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_DILATE, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    return AML_SUCCESS;
}

static AML_S32 cve_check_erode_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                     CVE_ERODE_CTRL_T *pstErodeCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_ERODE);
    CVE_IMAGE_CHECK(CVE_ERODE, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_ERODE, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ERODE, pstDstImage);
    CVE_IMAGE_TYPE_EQUAL(CVE_ERODE, pstSrcImage, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_ERODE, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);

    return AML_SUCCESS;
}

static AML_S32 cve_check_thresh(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                CVE_THRESH_CTRL_T *pstThreshCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_THRESH);
    CVE_IMAGE_CHECK(CVE_THRESH, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_THRESH, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_THRESH, pstDstImage);
    CVE_IMAGE_TYPE_EQUAL(CVE_THRESH, pstSrcImage, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_THRESH, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_THRESH, pstThreshCtrl->enMode, CVE_THRESH_MODE_BUTT);
    CVE_RANGE_CHECK(CVE_THRESH, pstThreshCtrl->u8LowThr, 0, 255);
    CVE_RANGE_CHECK(CVE_THRESH, pstThreshCtrl->u8HighThr, 0, 255);
    CVE_RANGE_CHECK(CVE_THRESH, pstThreshCtrl->u8MinVal, 0, 255);
    CVE_RANGE_CHECK(CVE_THRESH, pstThreshCtrl->u8MidVal, 0, 255);
    CVE_RANGE_CHECK(CVE_THRESH, pstThreshCtrl->u8MaxVal, 0, 255);
    switch (pstThreshCtrl->enMode) {
    case CVE_THRESH_MODE_BINARY:
        if ((pstThreshCtrl->u8MinVal > pstThreshCtrl->u8LowThr) ||
            (pstThreshCtrl->u8LowThr > pstThreshCtrl->u8MaxVal)) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MinVal(%d) pstThreshCtrl->u8LowThr(%d)\n",
                          pstThreshCtrl->u8MinVal, pstThreshCtrl->u8LowThr);
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MaxVal(%d)\n", pstThreshCtrl->u8MaxVal);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_MODE_TRUNC:
        if (pstThreshCtrl->u8LowThr > pstThreshCtrl->u8MaxVal) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8LowThr(%d) pstThreshCtrl->u8MaxVal(%d)\n",
                          pstThreshCtrl->u8LowThr, pstThreshCtrl->u8MaxVal);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_MODE_TO_MINVAL:
        if (pstThreshCtrl->u8MinVal > pstThreshCtrl->u8LowThr) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MinVal(%d) pstThreshCtrl->u8LowThr(%d)\n",
                          pstThreshCtrl->u8MinVal, pstThreshCtrl->u8LowThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_MODE_MIN_MID_MAX:
        if ((pstThreshCtrl->u8MinVal > pstThreshCtrl->u8LowThr) ||
            (pstThreshCtrl->u8LowThr > pstThreshCtrl->u8MidVal) ||
            (pstThreshCtrl->u8MidVal > pstThreshCtrl->u8HighThr) ||
            (pstThreshCtrl->u8HighThr > pstThreshCtrl->u8MaxVal)) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MinVal(%d) pstThreshCtrl->u8LowThr(%d)\n",
                          pstThreshCtrl->u8MinVal, pstThreshCtrl->u8LowThr);
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MidVal(%d) pstThreshCtrl->u8HighThr(%d)\n",
                          pstThreshCtrl->u8MidVal, pstThreshCtrl->u8HighThr);
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MaxVal(%d) n", pstThreshCtrl->u8MaxVal);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_MODE_ORI_MID_MAX:
        if ((pstThreshCtrl->u8LowThr > pstThreshCtrl->u8MidVal) ||
            (pstThreshCtrl->u8MidVal > pstThreshCtrl->u8HighThr) ||
            (pstThreshCtrl->u8HighThr > pstThreshCtrl->u8MaxVal)) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8LowThr(%d) pstThreshCtrl->u8MidVal(%d)\n",
                          pstThreshCtrl->u8LowThr, pstThreshCtrl->u8MidVal);
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8HighThr(%d) pstThreshCtrl->u8MaxVal(%d)\n",
                          pstThreshCtrl->u8HighThr, pstThreshCtrl->u8MaxVal);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_MODE_MIN_MID_ORI:
        if ((pstThreshCtrl->u8MinVal > pstThreshCtrl->u8LowThr) ||
            (pstThreshCtrl->u8LowThr > pstThreshCtrl->u8MidVal) ||
            (pstThreshCtrl->u8MidVal > pstThreshCtrl->u8HighThr)) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MinVal(%d) pstThreshCtrl->u8LowThr(%d)\n",
                          pstThreshCtrl->u8MinVal, pstThreshCtrl->u8LowThr);
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MidVal(%d) pstThreshCtrl->u8HighThr(%d)\n",
                          pstThreshCtrl->u8MidVal, pstThreshCtrl->u8HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_MODE_MIN_ORI_MAX:
        if ((pstThreshCtrl->u8MinVal > pstThreshCtrl->u8LowThr) ||
            (pstThreshCtrl->u8LowThr > pstThreshCtrl->u8HighThr) ||
            (pstThreshCtrl->u8HighThr > pstThreshCtrl->u8MaxVal)) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8MinVal(%d) pstThreshCtrl->u8LowThr(%d)\n",
                          pstThreshCtrl->u8MinVal, pstThreshCtrl->u8LowThr);
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8HighThr(%d) pstThreshCtrl->u8MaxVal(%d)\n",
                          pstThreshCtrl->u8HighThr, pstThreshCtrl->u8MaxVal);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_MODE_ORI_MID_ORI:
        if ((pstThreshCtrl->u8LowThr > pstThreshCtrl->u8MidVal) ||
            (pstThreshCtrl->u8MidVal > pstThreshCtrl->u8HighThr)) {
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8LowThr(%d) pstThreshCtrl->u8MidVal(%d)\n",
                          pstThreshCtrl->u8LowThr, pstThreshCtrl->u8MidVal);
            CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->u8HighThr(%d) n", pstThreshCtrl->u8HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    default:
        CVE_ERR_TRACE("[CVE_THRESH] pstThreshCtrl->enMode(%d) set error\n", pstThreshCtrl->enMode);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_sub_param(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                                   CVE_DST_IMAGE_T *pstDst, CVE_SUB_CTRL_T *pstSubCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_ALU_SUB);
    CVE_IMAGE_CHECK(CVE_ALU_SUB, pstSrcImage1);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_SUB, pstSrcImage1, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_SUB, pstSrcImage2);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_SUB, pstSrcImage2, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_SUB, pstDst);
    CVE_MODE_CHECK(CVE_ALU_SUB, pstSubCtrl->enMode, CVE_SUB_MODE_BUTT);
    if (pstSubCtrl->enMode == CVE_SUB_MODE_SHIFT) {
        CVE_IMAGE_TYPE_CHECK(CVE_ALU_SUB, pstDst, CVE_IMAGE_TYPE_S8C1);
    } else {
        CVE_IMAGE_TYPE_CHECK(CVE_ALU_SUB, pstDst, CVE_IMAGE_TYPE_U8C1);
    }
    CVE_RESOLUTION_EQUAL(CVE_ALU_SUB, pstSrcImage1, pstSrcImage2, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_ALU_SUB, pstSrcImage1, pstDst, RESOLUTION_DS_NONE);
    return AML_SUCCESS;
}

static AML_S32 cve_check_or_param(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                                  CVE_DST_IMAGE_T *pstDst)
{
    CVE_GET_IMAGE_SIZE(CVE_ALU_OR);
    CVE_IMAGE_CHECK(CVE_ALU_OR, pstSrcImage1);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_OR, pstSrcImage1, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_OR, pstSrcImage2);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_OR, pstSrcImage2, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_OR, pstDst);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_OR, pstDst, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_ALU_OR, pstSrcImage1, pstSrcImage2, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_ALU_OR, pstSrcImage1, pstDst, RESOLUTION_DS_NONE);
    return AML_SUCCESS;
}

static AML_S32 cve_check_and_param(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                                   CVE_DST_IMAGE_T *pstDst)
{
    CVE_GET_IMAGE_SIZE(CVE_ALU_AND);
    CVE_IMAGE_CHECK(CVE_ALU_AND, pstSrcImage1);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_AND, pstSrcImage1, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_AND, pstSrcImage2);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_AND, pstSrcImage2, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_AND, pstDst);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_AND, pstDst, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_ALU_AND, pstSrcImage1, pstSrcImage2, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_ALU_AND, pstSrcImage1, pstDst, RESOLUTION_DS_NONE);
    return AML_SUCCESS;
}

static AML_S32 cve_check_xor_param(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                                   CVE_DST_IMAGE_T *pstDst)
{
    CVE_GET_IMAGE_SIZE(CVE_ALU_XOR);
    CVE_IMAGE_CHECK(CVE_ALU_XOR, pstSrcImage1);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_XOR, pstSrcImage1, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_XOR, pstSrcImage2);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_XOR, pstSrcImage2, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_XOR, pstDst);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_XOR, pstDst, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_ALU_XOR, pstSrcImage1, pstSrcImage2, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_ALU_XOR, pstSrcImage1, pstDst, RESOLUTION_DS_NONE);
    return AML_SUCCESS;
}

static AML_S32 cve_check_add_param(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                                   CVE_DST_IMAGE_T *pstDst, CVE_ADD_CTRL_T *pstAddCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_ALU_ADD);
    CVE_IMAGE_CHECK(CVE_ALU_ADD, pstSrcImage1);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_ADD, pstSrcImage1, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_ADD, pstSrcImage2);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_ADD, pstSrcImage2, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ALU_ADD, pstDst);
    CVE_IMAGE_TYPE_CHECK(CVE_ALU_ADD, pstDst, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_ALU_ADD, pstSrcImage1, pstSrcImage2, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_ALU_ADD, pstSrcImage1, pstDst, RESOLUTION_DS_NONE);
    CVE_RANGE_CHECK(CVE_ALU_ADD, pstAddCtrl->u0q16X, 0, 65536);
    if (pstAddCtrl->u0q16X + pstAddCtrl->u0q16Y != 65536) {
        CVE_ERR_TRACE("[CVE_ALU_ADD] pstAddCtrl->u0q16X(%d) pstAddCtrl->u0q16Y(%d) set error. need "
                      "x+y=65535\n",
                      pstAddCtrl->u0q16X, pstAddCtrl->u0q16Y);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    return AML_SUCCESS;
}

static AML_S32 cve_check_integ_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                     CVE_INTEG_CTRL_T *pstIntegCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_INTEG);
    CVE_IMAGE_CHECK(CVE_INTEG, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_INTEG, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_INTEG, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_INTEG, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_INTEG, pstIntegCtrl->enOutCtrl, CVE_INTEG_OUT_CTRL_BUTT);
    if ((pstIntegCtrl->enOutCtrl == CVE_INTEG_OUT_CTRL_COMBINE) ||
        (pstIntegCtrl->enOutCtrl == CVE_INTEG_OUT_CTRL_TQSUM)) {
        CVE_IMAGE_TYPE_CHECK(CVE_INTEG, pstDstImage, CVE_IMAGE_TYPE_U64C1);
    } else if (pstIntegCtrl->enOutCtrl == CVE_INTEG_OUT_CTRL_TUM) {
        CVE_IMAGE_TYPE_CHECK(CVE_INTEG, pstDstImage, CVE_IMAGE_TYPE_U32C1);
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_hist_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_MEM_INFO_T *pstDstMem)
{
    CVE_GET_IMAGE_SIZE(CVE_HIST);
    CVE_IMAGE_CHECK(CVE_HIST, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_HIST, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_MEM_CHECK(CVE_HIST, pstDstMem);

    return AML_SUCCESS;
}

static AML_S32 cve_check_thresh_s16_param(CVE_SRC_IMAGE_T *pstSrcImage,
                                          CVE_DST_IMAGE_T *pstDstImage,
                                          CVE_THRESH_S16_CTRL_T *pstThreshS16Ctrl)
{
    CVE_GET_IMAGE_SIZE(CVE_THRESH_S16);
    CVE_IMAGE_CHECK(CVE_THRESH_S16, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_THRESH_S16, pstSrcImage, CVE_IMAGE_TYPE_S16C1);
    CVE_IMAGE_CHECK(CVE_THRESH_S16, pstDstImage);
    CVE_MODE_CHECK(CVE_THRESH_S16, pstThreshS16Ctrl->enMode, CVE_THRESH_S16_MODE_BUTT);
    if ((pstThreshS16Ctrl->enMode == CVE_THRESH_S16_MODE_S16_TO_S8_MIN_MID_MAX) ||
        (pstThreshS16Ctrl->enMode == CVE_THRESH_S16_MODE_S16_TO_S8_MIN_ORI_MAX)) {
        CVE_IMAGE_TYPE_CHECK(CVE_THRESH_S16, pstDstImage, CVE_IMAGE_TYPE_S8C1);
    } else if ((pstThreshS16Ctrl->enMode == CVE_THRESH_S16_MODE_S16_TO_U8_MIN_MID_MAX) ||
               (pstThreshS16Ctrl->enMode == CVE_THRESH_S16_MODE_S16_TO_U8_MIN_ORI_MAX)) {
        CVE_IMAGE_TYPE_CHECK(CVE_THRESH_S16, pstDstImage, CVE_IMAGE_TYPE_U8C1);
    }
    CVE_RESOLUTION_EQUAL(CVE_THRESH_S16, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);

    switch (pstThreshS16Ctrl->enMode) {
    case CVE_THRESH_S16_MODE_S16_TO_S8_MIN_MID_MAX:
        if (pstThreshS16Ctrl->s16LowThr > pstThreshS16Ctrl->s16HighThr) {
            CVE_ERR_TRACE("[CVE_THRESH_S16] pstThreshS16Ctrl->s16LowThr(%d) "
                          "pstThreshS16Ctrl->s16HighThr(%d) set error.\n",
                          pstThreshS16Ctrl->s16LowThr, pstThreshS16Ctrl->s16HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_S16_MODE_S16_TO_S8_MIN_ORI_MAX:
        if ((pstThreshS16Ctrl->s16LowThr > pstThreshS16Ctrl->s16HighThr) ||
            (pstThreshS16Ctrl->s16LowThr <= -32768) || (pstThreshS16Ctrl->s16HighThr >= 32767)) {
            CVE_ERR_TRACE("[CVE_THRESH_S16] pstThreshS16Ctrl->s16LowThr(%d) "
                          "pstThreshS16Ctrl->s16HighThr(%d) set error.\n",
                          pstThreshS16Ctrl->s16LowThr, pstThreshS16Ctrl->s16HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_S16_MODE_S16_TO_U8_MIN_MID_MAX:
        if (pstThreshS16Ctrl->s16LowThr > pstThreshS16Ctrl->s16HighThr) {
            CVE_ERR_TRACE("[CVE_THRESH_S16] pstThreshS16Ctrl->s16LowThr(%d) "
                          "pstThreshS16Ctrl->s16HighThr(%d) set error.\n",
                          pstThreshS16Ctrl->s16LowThr, pstThreshS16Ctrl->s16HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_S16_MODE_S16_TO_U8_MIN_ORI_MAX:
        if ((pstThreshS16Ctrl->s16LowThr > pstThreshS16Ctrl->s16HighThr) ||
            (pstThreshS16Ctrl->s16LowThr < -1) || (pstThreshS16Ctrl->s16HighThr > 255)) {
            CVE_ERR_TRACE("[CVE_THRESH_S16] pstThreshS16Ctrl->s16LowThr(%d) "
                          "pstThreshS16Ctrl->s16HighThr(%d) set error.\n",
                          pstThreshS16Ctrl->s16LowThr, pstThreshS16Ctrl->s16HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    default:
        CVE_ERR_TRACE("[CVE_THRESH_S16] pstThreshS16Ctrl->enMode(%d) set error.\n",
                      pstThreshS16Ctrl->enMode);
        return AML_ERR_CVE_ILLEGAL_PARAM;
        break;
    }
    return AML_SUCCESS;
}

static AML_S32 cve_check_thresh_u16_param(CVE_SRC_IMAGE_T *pstSrcImage,
                                          CVE_DST_IMAGE_T *pstDstImage,
                                          CVE_THRESH_U16_CTRL_T *pstThreshU16Ctrl)
{
    CVE_GET_IMAGE_SIZE(CVE_THRESH_U16);
    CVE_IMAGE_CHECK(CVE_THRESH_U16, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_THRESH_U16, pstSrcImage, CVE_IMAGE_TYPE_U16C1);
    CVE_IMAGE_CHECK(CVE_THRESH_U16, pstDstImage);
    CVE_IMAGE_TYPE_CHECK(CVE_THRESH_U16, pstDstImage, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_THRESH_U16, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_THRESH_U16, pstThreshU16Ctrl->enMode, CVE_THRESH_U16_MODE_BUTT);
    switch (pstThreshU16Ctrl->enMode) {
    case CVE_THRESH_U16_MODE_U16_TO_U8_MIN_MID_MAX:
        if (pstThreshU16Ctrl->u16LowThr > pstThreshU16Ctrl->u16HighThr) {
            CVE_ERR_TRACE("[CVE_THRESH_U16] pstThreshU16Ctrl->u16LowThr(%d) "
                          "pstThreshU16Ctrl->u16HighThr(%d) set error.\n",
                          pstThreshU16Ctrl->u16LowThr, pstThreshU16Ctrl->u16HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_THRESH_U16_MODE_U16_TO_U8_MIN_ORI_MAX:
        if ((pstThreshU16Ctrl->u16LowThr > pstThreshU16Ctrl->u16HighThr) ||
            (pstThreshU16Ctrl->u16HighThr > 65535)) {
            CVE_ERR_TRACE("[CVE_THRESH_U16] pstThreshU16Ctrl->u16LowThr(%d) "
                          "pstThreshU16Ctrl->u16HighThr(%d) set error.\n",
                          pstThreshU16Ctrl->u16LowThr, pstThreshU16Ctrl->u16HighThr);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    default:
        CVE_ERR_TRACE("[CVE_THRESH_U16] pstThreshU16Ctrl->enMode(%d) set error.\n",
                      pstThreshU16Ctrl->enMode);
        return AML_ERR_CVE_ILLEGAL_PARAM;
        break;
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_16bit_to_8bit_param(CVE_SRC_IMAGE_T *pstSrcImage,
                                             CVE_DST_IMAGE_T *pstDstImage,
                                             CVE_16BIT_TO_8BIT_CTRL_T *pst16BitTo8BitCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_16BIT_TO_8BIT);
    CVE_IMAGE_CHECK(CVE_16BIT_TO_8BIT, pstSrcImage);
    CVE_IMAGE_CHECK(CVE_16BIT_TO_8BIT, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_16BIT_TO_8BIT, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_16BIT_TO_8BIT, pst16BitTo8BitCtrl->enMode, CVE_16BIT_TO_8BIT_MODE_BUTT);
    switch (pst16BitTo8BitCtrl->enMode) {
    case CVE_16BIT_TO_8BIT_MODE_S16_TO_S8:
        if ((pstSrcImage->enType != CVE_IMAGE_TYPE_S16C1) ||
            (pstDstImage->enType != CVE_IMAGE_TYPE_S8C1)) {
            CVE_ERR_TRACE("[CVE_16BIT_TO_8BIT] pstSrcImage->enType(%d) pstDstImage->enType(%d), "
                          "not support this type\n",
                          pstSrcImage->enType, pstDstImage->enType);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_16BIT_TO_8BIT_MODE_S16_TO_U8_ABS:
    case CVE_16BIT_TO_8BIT_MODE_S16_TO_U8_BIAS:
        if ((pstSrcImage->enType != CVE_IMAGE_TYPE_S16C1) ||
            (pstDstImage->enType != CVE_IMAGE_TYPE_U8C1)) {
            CVE_ERR_TRACE("[CVE_16BIT_TO_8BIT] pstSrcImage->enType(%d) pstDstImage->enType(%d), "
                          "not support this type\n",
                          pstSrcImage->enType, pstDstImage->enType);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_16BIT_TO_8BIT_MODE_U16_TO_U8:
        if ((pstSrcImage->enType != CVE_IMAGE_TYPE_U16C1) ||
            (pstDstImage->enType != CVE_IMAGE_TYPE_U8C1)) {
            CVE_ERR_TRACE("[CVE_16BIT_TO_8BIT] pstSrcImage->enType(%d) pstDstImage->enType(%d), "
                          "not support this type\n",
                          pstSrcImage->enType, pstDstImage->enType);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    default:
        CVE_ERR_TRACE("[CVE_16BIT_TO_8BIT] pstSrcImage->enType(%d) pstDstImage->enType(%d), not "
                      "support this type\n",
                      pstSrcImage->enType, pstDstImage->enType);
        break;
    }

    return AML_SUCCESS;
}
static AML_S32 cve_check_ord_stat_filter_param(CVE_SRC_IMAGE_T *pstSrcImage,
                                               CVE_DST_IMAGE_T *pstDstImage,
                                               CVE_ORD_STAT_FILTER_CTRL_T *pstOrdStatFltCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_ORD_STAT_FILTER);
    CVE_IMAGE_CHECK(CVE_ORD_STAT_FILTER, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_ORD_STAT_FILTER, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ORD_STAT_FILTER, pstDstImage);
    CVE_IMAGE_TYPE_CHECK(CVE_ORD_STAT_FILTER, pstDstImage, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_ORD_STAT_FILTER, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_ORD_STAT_FILTER, pstOrdStatFltCtrl->enMode, CVE_ORD_STAT_FILTER_MODE_BUTT);

    return AML_SUCCESS;
}

static AML_S32 cve_check_map_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_SRC_MEM_INFO_T *pstMap,
                                   CVE_DST_IMAGE_T *pstDstImage, CVE_MAP_CTRL_T *pstMapCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_MAP);
    CVE_IMAGE_CHECK(CVE_MAP, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_MAP, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_MEM_CHECK(CVE_MAP, pstMap);
    CVE_IMAGE_CHECK(CVE_MAP, pstDstImage);
    CVE_RESOLUTION_EQUAL(CVE_MAP, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_MAP, pstMapCtrl->enMode, CVE_MAP_MODE_BUTT);
    if (pstMapCtrl->enMode == CVE_MAP_MODE_U8) {
        CVE_IMAGE_TYPE_CHECK(CVE_MAP, pstDstImage, CVE_IMAGE_TYPE_U8C1);
    } else if (pstMapCtrl->enMode == CVE_MAP_MODE_U16) {
        CVE_IMAGE_TYPE_CHECK(CVE_MAP, pstDstImage, CVE_IMAGE_TYPE_U16C1);
    } else if (pstMapCtrl->enMode == CVE_MAP_MODE_S16) {
        CVE_IMAGE_TYPE_CHECK(CVE_MAP, pstDstImage, CVE_IMAGE_TYPE_S16C1);
    }
    return AML_SUCCESS;
}

static AML_S32 cve_check_equalize_hist_param(CVE_SRC_IMAGE_T *pstSrcImage,
                                             CVE_DST_IMAGE_T *pstDstImage,
                                             CVE_EQUALIZE_HIST_CTRL_T *pstEqualizeHistCtrl)
{
    CVE_MEM_INFO_T *pstMem = &pstEqualizeHistCtrl->stMem;

    CVE_GET_IMAGE_SIZE(CVE_EQUALIZE_HIST);
    CVE_IMAGE_CHECK(CVE_EQUALIZE_HIST, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_EQUALIZE_HIST, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_EQUALIZE_HIST, pstDstImage);
    CVE_IMAGE_TYPE_CHECK(CVE_EQUALIZE_HIST, pstDstImage, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_EQUALIZE_HIST, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MEM_CHECK(CVE_EQUALIZE_HIST, pstMem);
    return AML_SUCCESS;
}

static AML_S32 cve_check_ncc_param(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                                   CVE_DST_MEM_INFO_T *pstDstmem, CVE_NCC_CTRL_T *pstNccCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_NCC);
    CVE_IMAGE_CHECK(CVE_NCC, pstSrcImage1);
    CVE_IMAGE_TYPE_CHECK(CVE_NCC, pstSrcImage1, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_NCC, pstSrcImage2);
    CVE_IMAGE_TYPE_CHECK(CVE_NCC, pstSrcImage2, CVE_IMAGE_TYPE_U8C1);
    CVE_RESOLUTION_EQUAL(CVE_NCC, pstSrcImage1, pstSrcImage2, RESOLUTION_DS_NONE);
    CVE_MEM_CHECK(CVE_NCC, pstDstmem);
    CVE_MODE_CHECK(CVE_NCC, pstNccCtrl->enMode, CVE_NCC_MODE_BUTT);
    return AML_SUCCESS;
}

static AML_S32 cve_check_ccl_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                   CVE_DST_MEM_INFO_T *pstBlob, CVE_CCL_CTRL_T *pstCclCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_CCL);
    CVE_IMAGE_CHECK(CVE_CCL, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_CCL, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_CCL, pstDstImage);
    CVE_IMAGE_TYPE_CHECK(CVE_CCL, pstDstImage, CVE_IMAGE_TYPE_U16C1);
    CVE_RESOLUTION_EQUAL(CVE_CCL, pstSrcImage, pstDstImage, RESOLUTION_DS_NONE);
    CVE_MEM_CHECK(CVE_CCL, pstBlob);
    CVE_MODE_CHECK(CVE_CCL, pstCclCtrl->enInputDataMode, CVE_CCL_INPUT_DATA_MODE_BUTT);
    return AML_SUCCESS;
}

static AML_S32 cve_check_gmm_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_SRC_IMAGE_T *pstFactor,
                                   CVE_DST_IMAGE_T *pstFg, CVE_DST_IMAGE_T *pstBg,
                                   CVE_MEM_INFO_T *pstModel, CVE_GMM_CTRL_T *pstGmmCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_GMM);
    CVE_IMAGE_CHECK(CVE_GMM, pstSrcImage);
    if ((pstSrcImage->enType != CVE_IMAGE_TYPE_U8C1) &&
        (pstSrcImage->enType != CVE_IMAGE_TYPE_U8C3_PACKAGE)) {
        CVE_ERR_TRACE("[CVE_GMM] pstSrcImage->enType(%d) set error.\n", pstSrcImage->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    CVE_IMAGE_CHECK(CVE_GMM, pstFactor);
    CVE_IMAGE_TYPE_CHECK(CVE_GMM, pstFactor, CVE_IMAGE_TYPE_U16C1);
    CVE_IMAGE_CHECK(CVE_GMM, pstFg);
    CVE_IMAGE_TYPE_CHECK(CVE_GMM, pstFg, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_GMM, pstBg);
    CVE_IMAGE_TYPE_EQUAL(CVE_GMM, pstSrcImage, pstBg);
    CVE_MEM_CHECK(CVE_GMM, pstModel);
    CVE_MODE_CHECK(CVE_GMM, pstGmmCtrl->enOutputMode, CVE_GMM_OUTPUT_MODE_BUTT);
    CVE_MODE_CHECK(CVE_GMM, pstGmmCtrl->enSnsFactorMode, CVE_GMM_SNS_FACTOR_MODE_BUTT);
    CVE_MODE_CHECK(CVE_GMM, pstGmmCtrl->enDurationUpdateFactorMode,
                   CVE_GMM_DURATION_UPDATE_FACTOR_MODE_BUTT);
    CVE_MODE_CHECK(CVE_GMM, pstGmmCtrl->enDownScaleMode, CVE_GMM_DOWN_SCALE_MODE_BUTT);
    CVE_MODE_CHECK(CVE_GMM, pstGmmCtrl->enOutputDataMode, CVE_GMM_OUTPUT_DATA_MODE_BUTT);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u10q6NoiseVar, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u10q6MaxVar, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u10q6MinVar, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u0q16LearnRate, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u0q16InitWeight, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u0q16WeightThr, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u8ModelNum, 1, 5);
    CVE_RANGE_CHECK(CVE_GMM, pstGmmCtrl->u3q7SigmaScale, 0, 1023);
    CVE_BOOL_CHECK(CVE_GMM, pstGmmCtrl->enFastLearn);
    return AML_SUCCESS;
}

static AML_S32 cve_check_lbp_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                   CVE_LBP_CTRL_T *pstLbpCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_LBP);
    CVE_IMAGE_TYPE_CHECK(CVE_LBP, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_LBP, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_LBP, pstDstImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_LBP, pstDstImage);

    CVE_MODE_CHECK(CVE_LBP, pstLbpCtrl->enMode, CVE_LBP_CMP_MODE_BUTT);

    if (pstLbpCtrl->enMode == CVE_LBP_CMP_MODE_NORMAL) {
        CVE_RANGE_CHECK(CVE_LBP, pstLbpCtrl->un8BitThr.s8Val, -128, 127);
    } else {
        CVE_RANGE_CHECK(CVE_LBP, pstLbpCtrl->un8BitThr.u8Val, 0, 255);
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_norm_grad_param(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstH,
                                         CVE_DST_IMAGE_T *pstDstV, CVE_DST_IMAGE_T *pstDstHV,
                                         CVE_NORM_GRAD_CTRL_T *pstNormGradCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_NORM_GRAD);
    CVE_IMAGE_TYPE_CHECK(CVE_NORM_GRAD, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_NORM_GRAD, pstSrcImage);

    CVE_MODE_CHECK(CVE_NORM_GRAD, pstNormGradCtrl->enOutCtrl, CVE_NORM_GRAD_OUT_CTRL_BUTT);
    switch (pstNormGradCtrl->enOutCtrl) {
    case CVE_NORM_GRAD_OUT_CTRL_HOR_AND_VER:
        CVE_IMAGE_TYPE_CHECK(CVE_NORM_GRAD, pstDstH, CVE_IMAGE_TYPE_S8C1);
        CVE_IMAGE_CHECK(CVE_NORM_GRAD, pstDstH);
        CVE_IMAGE_TYPE_CHECK(CVE_NORM_GRAD, pstDstV, CVE_IMAGE_TYPE_S8C1);
        CVE_IMAGE_CHECK(CVE_NORM_GRAD, pstDstV);
        CVE_RESOLUTION_EQUAL(CVE_NORM_GRAD, pstSrcImage, pstDstH, RESOLUTION_DS_NONE);
        CVE_RESOLUTION_EQUAL(CVE_NORM_GRAD, pstSrcImage, pstDstV, RESOLUTION_DS_NONE);
        break;
    case CVE_NORM_GRAD_OUT_CTRL_HOR:
        CVE_IMAGE_TYPE_CHECK(CVE_NORM_GRAD, pstDstH, CVE_IMAGE_TYPE_S8C1);
        CVE_IMAGE_CHECK(CVE_NORM_GRAD, pstDstH);
        CVE_RESOLUTION_EQUAL(CVE_NORM_GRAD, pstSrcImage, pstDstH, RESOLUTION_DS_NONE);
        break;
    case CVE_NORM_GRAD_OUT_CTRL_VER:
        CVE_IMAGE_TYPE_CHECK(CVE_NORM_GRAD, pstDstV, CVE_IMAGE_TYPE_S8C1);
        CVE_IMAGE_CHECK(CVE_NORM_GRAD, pstDstV);
        CVE_RESOLUTION_EQUAL(CVE_NORM_GRAD, pstSrcImage, pstDstV, RESOLUTION_DS_NONE);
        break;
    case CVE_NORM_GRAD_OUT_CTRL_COMBINE:
        CVE_IMAGE_TYPE_CHECK(CVE_NORM_GRAD, pstDstHV, CVE_IMAGE_TYPE_S8C2_PACKAGE);
        CVE_IMAGE_CHECK(CVE_NORM_GRAD, pstDstHV);
        CVE_RESOLUTION_EQUAL(CVE_NORM_GRAD, pstSrcImage, pstDstHV, RESOLUTION_DS_NONE);
        break;
    default:
        break;
    }

    CVE_RANGE_CHECK(CVE_NORM_GRAD, pstNormGradCtrl->u8Norm, 0, 13);

    return AML_SUCCESS;
}

static AML_S32 cve_check_build_lk_optical_flow_pyr_param(
    CVE_SRC_IMAGE_T *pstSrcPyr, CVE_SRC_IMAGE_T astDstPyr[],
    CVE_BUILD_LK_OPTICAL_FLOW_PYR_CTRL_T *pstBuildLkOptiFlowPyrCtrl)
{
    int i;

    CVE_GET_IMAGE_SIZE(CVE_BUILD_LK_OPTICAL_FLOW_PYR);
    CVE_IMAGE_TYPE_CHECK(CVE_BUILD_LK_OPTICAL_FLOW_PYR, pstSrcPyr, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_BUILD_LK_OPTICAL_FLOW_PYR, pstSrcPyr);
    CVE_RANGE_CHECK(CVE_BUILD_LK_OPTICAL_FLOW_PYR, pstBuildLkOptiFlowPyrCtrl->u8MaxLevel, 0, 3);
    for (i = 0; i <= pstBuildLkOptiFlowPyrCtrl->u8MaxLevel; i++) {
        CVE_IMAGE_TYPE_CHECK(CVE_BUILD_LK_OPTICAL_FLOW_PYR, (&astDstPyr[i]), CVE_IMAGE_TYPE_U8C1);
        CVE_RESOLUTION_EQUAL(CVE_BUILD_LK_OPTICAL_FLOW_PYR, pstSrcPyr, (&astDstPyr[i]), i);
    }

    return AML_SUCCESS;
}

static AML_S32 cve_check_lk_optical_flow_pyr_param(
    CVE_SRC_IMAGE_T astSrcPrevPyr[], CVE_SRC_IMAGE_T astSrcNextPyr[],
    CVE_SRC_MEM_INFO_T *pstPrevPts, CVE_MEM_INFO_T *pstNextPts, CVE_DST_MEM_INFO_T *pstStatus,
    CVE_DST_MEM_INFO_T *pstErr, CVE_LK_OPTICAL_FLOW_PYR_CTRL_T *pstLkOptiFlowPyrCtrl)
{
    int i;

    CVE_GET_IMAGE_SIZE(CVE_LK_OPTICAL_FLOW_PYR);
    CVE_RANGE_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstLkOptiFlowPyrCtrl->u8MaxLevel, 0, 3);
    for (i = 0; i <= pstLkOptiFlowPyrCtrl->u8MaxLevel; i++) {
        CVE_IMAGE_TYPE_CHECK(CVE_LK_OPTICAL_FLOW_PYR, (&astSrcPrevPyr[i]), CVE_IMAGE_TYPE_U8C1);
        CVE_IMAGE_TYPE_CHECK(CVE_LK_OPTICAL_FLOW_PYR, (&astSrcNextPyr[i]), CVE_IMAGE_TYPE_U8C1);
        CVE_RESOLUTION_EQUAL(CVE_LK_OPTICAL_FLOW_PYR, (&astSrcPrevPyr[0]), (&astSrcPrevPyr[i]), i);
        CVE_RESOLUTION_EQUAL(CVE_LK_OPTICAL_FLOW_PYR, (&astSrcPrevPyr[0]), (&astSrcNextPyr[i]), i);
    }

    CVE_MODE_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstLkOptiFlowPyrCtrl->enOutMode,
                   CVE_LK_OPTICAL_FLOW_PYR_OUT_MODE_BUTT);

    CVE_MEM_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstPrevPts);
    CVE_MEM_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstNextPts);
    CVE_MEM_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstStatus);
    if (pstLkOptiFlowPyrCtrl->enOutMode == CVE_LK_OPTICAL_FLOW_PYR_OUT_MODE_ERR) {
        CVE_MEM_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstErr);
    }

    CVE_BOOL_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstLkOptiFlowPyrCtrl->bUseInitFlow);
    CVE_RANGE_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstLkOptiFlowPyrCtrl->u16PtsNum, 1, 500);
    CVE_RANGE_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstLkOptiFlowPyrCtrl->u8IterCnt, 1, 20);
    CVE_RANGE_CHECK(CVE_LK_OPTICAL_FLOW_PYR, pstLkOptiFlowPyrCtrl->u0q8Eps, 1, 255);

    return AML_SUCCESS;
}

static AML_S32 cve_check_st_candi_corner_param(CVE_SRC_IMAGE_T *pstSrc, CVE_DST_IMAGE_T *pstLabel,
                                               CVE_DST_IMAGE_T *pstCandiCorner,
                                               CVE_DST_MEM_INFO_T *pstCandiCornerPoint,
                                               CVE_ST_CANDI_CORNER_CTRL_T *pstStCandiCornerCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_ST_CANDI_CORNER);
    CVE_IMAGE_TYPE_CHECK(CVE_ST_CANDI_CORNER, pstSrc, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ST_CANDI_CORNER, pstSrc);
    CVE_IMAGE_TYPE_CHECK(CVE_ST_CANDI_CORNER, pstLabel, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_ST_CANDI_CORNER, pstLabel);
    CVE_IMAGE_TYPE_CHECK(CVE_ST_CANDI_CORNER, pstCandiCorner, CVE_IMAGE_TYPE_U16C1);
    CVE_IMAGE_CHECK(CVE_ST_CANDI_CORNER, pstCandiCorner);
    CVE_MEM_CHECK(CVE_ST_CANDI_CORNER, pstCandiCornerPoint);
    CVE_MEM_CHECK(CVE_ST_CANDI_CORNER, (&pstStCandiCornerCtrl->stMem));

    CVE_RESOLUTION_EQUAL(CVE_ST_CANDI_CORNER, pstSrc, pstLabel, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_ST_CANDI_CORNER, pstSrc, pstCandiCorner, RESOLUTION_DS_NONE);

    CVE_MODE_CHECK(CVE_ST_CANDI_CORNER, pstStCandiCornerCtrl->enOutputDataMode,
                   CVE_ST_CANDI_CORNER_OUTPUT_DATA_MODE_BUTT);

    return AML_SUCCESS;
}

static AML_S32 cve_check_sad_param(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                                   CVE_DST_IMAGE_T *pstSad, CVE_DST_IMAGE_T *pstThr,
                                   CVE_SAD_CTRL_T *pstSadCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_SAD);
    CVE_IMAGE_TYPE_CHECK(CVE_SAD, pstSrcImage1, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_SAD, pstSrcImage1);
    CVE_IMAGE_TYPE_CHECK(CVE_SAD, pstSrcImage2, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_SAD, pstSrcImage2);
    CVE_IMAGE_TYPE_CHECK(CVE_SAD, pstSad, CVE_IMAGE_TYPE_U16C1);
    CVE_IMAGE_TYPE_CHECK(CVE_SAD, pstThr, CVE_IMAGE_TYPE_U8C1);

    CVE_RESOLUTION_EQUAL(CVE_SAD, pstSrcImage1, pstSrcImage2, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_SAD, pstSadCtrl->enMode, CVE_SAD_MODE_BUTT);
    if (pstSadCtrl->enMode == CVE_SAD_MODE_MB_4X4) {
        CVE_RESOLUTION_EQUAL(CVE_SAD, pstSrcImage1, pstSad, RESOLUTION_DS_MB_4X4);
        CVE_RESOLUTION_EQUAL(CVE_SAD, pstSrcImage1, pstThr, RESOLUTION_DS_MB_4X4);
    } else if (pstSadCtrl->enMode == CVE_SAD_MODE_MB_8X8) {
        CVE_RESOLUTION_EQUAL(CVE_SAD, pstSrcImage1, pstSad, RESOLUTION_DS_MB_8X8);
        CVE_RESOLUTION_EQUAL(CVE_SAD, pstSrcImage1, pstThr, RESOLUTION_DS_MB_8X8);
    } else {
        CVE_RESOLUTION_EQUAL(CVE_SAD, pstSrcImage1, pstSad, RESOLUTION_DS_MB_16X16);
        CVE_RESOLUTION_EQUAL(CVE_SAD, pstSrcImage1, pstThr, RESOLUTION_DS_MB_16X16);
    }

    CVE_RANGE_CHECK(CVE_SAD, pstSadCtrl->u16Thr, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_SAD, pstSadCtrl->u8MinVal, 0, 255);
    CVE_RANGE_CHECK(CVE_SAD, pstSadCtrl->u8MaxVal, 0, 255);

    return AML_SUCCESS;
}

static AML_S32 cve_check_grad_fg_param(CVE_SRC_IMAGE_T *pstBgDiffFg, CVE_SRC_IMAGE_T *pstCurGrad,
                                       CVE_SRC_IMAGE_T *pstBgGrad, CVE_DST_IMAGE_T *pstGradFg,
                                       CVE_GRAD_FG_CTRL_T *pstGradFgCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_GRAD_FG);
    CVE_IMAGE_TYPE_CHECK(CVE_GRAD_FG, pstBgDiffFg, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_GRAD_FG, pstBgDiffFg);
    CVE_IMAGE_TYPE_CHECK(CVE_GRAD_FG, pstCurGrad, CVE_IMAGE_TYPE_S8C2_PACKAGE);
    CVE_IMAGE_CHECK(CVE_GRAD_FG, pstCurGrad);
    CVE_IMAGE_TYPE_CHECK(CVE_GRAD_FG, pstBgGrad, CVE_IMAGE_TYPE_S8C2_PACKAGE);
    CVE_IMAGE_CHECK(CVE_GRAD_FG, pstBgGrad);
    CVE_IMAGE_TYPE_CHECK(CVE_GRAD_FG, pstGradFg, CVE_IMAGE_TYPE_S8C2_PACKAGE);
    CVE_IMAGE_CHECK(CVE_GRAD_FG, pstGradFg);

    CVE_RESOLUTION_EQUAL(CVE_GRAD_FG, pstBgDiffFg, pstCurGrad, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_GRAD_FG, pstBgDiffFg, pstBgGrad, RESOLUTION_DS_NONE);
    CVE_RESOLUTION_EQUAL(CVE_GRAD_FG, pstBgDiffFg, pstGradFg, RESOLUTION_DS_NONE);

    CVE_MODE_CHECK(CVE_GRAD_FG, pstGradFgCtrl->enMode, CVE_GRAD_FG_MODE_BUTT);
    CVE_RANGE_CHECK(CVE_GRAD_FG, pstGradFgCtrl->u8MinMagDiff, 0, 255);

    return AML_SUCCESS;
}

static AML_S32 cve_check_canny_hys_edge_param(CVE_SRC_IMAGE_T *pstSrcImage,
                                              CVE_DST_IMAGE_T *pstEdge,
                                              CVE_DST_MEM_INFO_T *pstStack,
                                              CVE_CANNY_HYS_EDGE_CTRL_T *pstCannyHysEdgeCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_CANNY_EDGE);
    CVE_IMAGE_TYPE_CHECK(CVE_CANNY_EDGE, pstSrcImage, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_CANNY_EDGE, pstSrcImage);
    CVE_IMAGE_TYPE_CHECK(CVE_CANNY_EDGE, pstEdge, CVE_IMAGE_TYPE_U8C1);
    CVE_IMAGE_CHECK(CVE_CANNY_EDGE, pstEdge);
    CVE_MEM_CHECK(CVE_CANNY_EDGE, pstStack);
    CVE_MEM_CHECK(CVE_CANNY_EDGE, (&pstCannyHysEdgeCtrl->stMem));
    CVE_RESOLUTION_EQUAL(CVE_CANNY_EDGE, pstSrcImage, pstEdge, RESOLUTION_DS_NONE);
    CVE_RANGE_CHECK(CVE_CANNY_EDGE, pstCannyHysEdgeCtrl->u16LowThr, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_CANNY_EDGE, pstCannyHysEdgeCtrl->u16HighThr, pstCannyHysEdgeCtrl->u16LowThr,
                    (1 << 16) - 1);
    CVE_BOOL_CHECK(CVE_CANNY_EDGE, pstCannyHysEdgeCtrl->bGauss);

    return AML_SUCCESS;
}

static AML_S32 cve_check_update_bg_model_param(CVE_SRC_IMAGE_T *pstCurImg,
                                               CVE_MEM_INFO_T *pstBgModel1,
                                               CVE_MEM_INFO_T *pstBgModel2,
                                               CVE_DST_MEM_INFO_T *pstStatData,
                                               CVE_UPDATE_BG_MODEL_CTRL_T *pstUpdateBgModelCtrl)
{
    CVE_GET_IMAGE_SIZE(CVE_UPDATE_BG_MODEL);
    CVE_IMAGE_CHECK(CVE_UPDATE_BG_MODEL, pstCurImg);
    CVE_IMAGE_TYPE_CHECK(CVE_UPDATE_BG_MODEL, pstCurImg, CVE_IMAGE_TYPE_U8C1);
    CVE_MEM_CHECK(CVE_UPDATE_BG_MODEL, pstBgModel1);
    CVE_MEM_CHECK(CVE_UPDATE_BG_MODEL, pstBgModel2);
    CVE_MEM_CHECK(CVE_UPDATE_BG_MODEL, pstStatData);
    CVE_RANGE_CHECK(CVE_UPDATE_BG_MODEL, pstUpdateBgModelCtrl->u16DelThr, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_UPDATE_BG_MODEL, pstUpdateBgModelCtrl->u16FrqThr, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_UPDATE_BG_MODEL, pstUpdateBgModelCtrl->u16IntervalThr, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_UPDATE_BG_MODEL, pstUpdateBgModelCtrl->u0q16LearnRate, 0, (1 << 16) - 1);
    CVE_MODE_CHECK(CVE_UPDATE_BG_MODEL, pstUpdateBgModelCtrl->enDownScaleMode,
                   CVE_UPDATE_BG_MODEL_DOWN_SCALE_MODE_BUTT);
    return AML_SUCCESS;
}
static AML_S32 cve_check_tof_param(CVE_SRC_RAW_T *pstSrcRaw, CVE_SRC_RAW_T *pstSrcFpn,
                                   CVE_SRC_MEM_INFO_T *pstSrcCoef, CVE_SRC_MEM_INFO_T *pstBpc,
                                   CVE_DST_MEM_INFO_T *pstDtsStatus, CVE_DST_MEM_INFO_T *pstDtsIR,
                                   CVE_DST_MEM_INFO_T *pstDtsData, CVE_DST_MEM_INFO_T *pstDstHist,
                                   CVE_TOF_CTRL_T *pstTofCtrl)
{
    int i;

    CVE_GET_IMAGE_SIZE(CVE_TOF);
    CVE_RAW_CHECK(CVE_TOF, pstSrcRaw);
    CVE_RAW_CHECK(CVE_TOF, pstSrcFpn);
    CVE_MEM_CHECK(CVE_TOF, pstSrcCoef);
    CVE_MEM_CHECK(CVE_TOF, pstBpc);
    CVE_MEM_CHECK(CVE_TOF, pstDtsStatus);
    CVE_MEM_CHECK(CVE_TOF, pstDtsIR);
    CVE_MEM_CHECK(CVE_TOF, pstDtsData);
    CVE_MEM_CHECK(CVE_TOF, pstDstHist);
    CVE_RESOLUTION_EQUAL(CVE_TOF, pstSrcRaw, pstSrcFpn, RESOLUTION_DS_NONE);
    CVE_MODE_CHECK(CVE_TOF, pstTofCtrl->enRawMode, CVE_RAW_MODE_BUTT);
    CVE_MODE_CHECK(CVE_TOF, pstTofCtrl->enFpnMode, CVE_TOF_FPN_MODE_BUTT);
    CVE_BOOL_CHECK(CVE_TOF, pstTofCtrl->bRawShift);
    CVE_BOOL_CHECK(CVE_TOF, pstTofCtrl->bBypass);
    CVE_BOOL_CHECK(CVE_TOF, pstTofCtrl->bSpa1En);
    CVE_BOOL_CHECK(CVE_TOF, pstTofCtrl->bSpa2En);
    for (i = 0; i < 5; i++) {
        CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->as32PCoef[i], -(1 << 23), (1 << 23) - 1);
    }
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->as16TCoef1[0], -(1 << 11), (1 << 11) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->as16TCoef1[1], -(1 << 8), (1 << 8) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->as16TCoef1[2], -(1 << 11), (1 << 11) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->as16TCoef1[3], -(1 << 8), (1 << 8) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16HistXstart, 0, CVE_MIN(pstSrcRaw->u32Width, 1024));
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16HistYstart, 0, CVE_MIN(pstSrcRaw->u32Height, 1024));
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16HistXend, pstTofCtrl->u16HistXstart,
                    CVE_MIN(pstSrcRaw->u32Width, 1024));
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16HistYend, pstTofCtrl->u16HistYstart,
                    CVE_MIN(pstSrcRaw->u32Height, 1024));
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16Q1HighThr, 0, (1 << 12) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16Q23HighThr, 0, (1 << 12) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16IRLowThr, 0, (1 << 12) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16IRHighThr, pstTofCtrl->u16IRLowThr, (1 << 12) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16DepthMin, 0, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u16DepthMax, pstTofCtrl->u16DepthMin, (1 << 16) - 1);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u8SpaNorm, 0, 12);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->u8BadPointNum, 0, 255);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->s8IntTemp, -128, 127);
    CVE_RANGE_CHECK(CVE_TOF, pstTofCtrl->s8IntTemp, -128, 127);

    return AML_SUCCESS;
}
static int cve_fill_dma_task(CVE_DATA_T *pstSrcDATA, CVE_DST_DATA_T *pstDstDATA,
                             CVE_DMA_CTRL_T *pstDmaCtrl, char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_dma_params_t dma_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&dma_params, 0, sizeof(cve_op_dma_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_data(&init_params, pstSrcDATA, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_data(&init_params, pstDstDATA, &dst_off);
    if (ret != 0) {
        return ret;
    }
    init_params.src_addr[0] = pstSrcDATA->u64PhyAddr;
    init_params.dst_addr[0] = pstDstDATA->u64PhyAddr;
    init_params.src_stride[0] = pstSrcDATA->u32Stride;
    init_params.dst_stride[0] = pstDstDATA->u32Stride;
    init_params.src_width = pstSrcDATA->u32Width;
    init_params.src_height = pstSrcDATA->u32Height;
    init_params.dst_width = pstDstDATA->u32Width;
    init_params.dst_height = pstDstDATA->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&dma_params.comm_params, &init_params);

    if (pstDmaCtrl->enMode == CVE_DMA_MODE_DIRECT_COPY ||
        pstDmaCtrl->enMode == CVE_DMA_MODE_SET_3BYTE ||
        pstDmaCtrl->enMode == CVE_DMA_MODE_SET_8BYTE || pstDmaCtrl->enMode == CVE_DMA_MODE_NOT) {
        dma_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0x4;
        dma_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0x4;
    } else if (pstDmaCtrl->enMode == CVE_DMA_MODE_INTERVAL_COPY) {
        dma_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
        dma_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    }
    dma_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_DMA;
    dma_params.reg_1b.bits.dma_mode_ctrl = pstDmaCtrl->enMode;

    if (dma_params.reg_1b.bits.dma_mode_ctrl == CVE_DMA_MODE_SET_3BYTE ||
        dma_params.reg_1b.bits.dma_mode_ctrl == CVE_DMA_MODE_SET_8BYTE) {
        dma_params.reg_1c.bits.dma_u64val_lsb = CVE_GET_LO64(pstDmaCtrl->u64Val);
        dma_params.reg_1d.bits.dma_u64val_msb = CVE_GET_HI64(pstDmaCtrl->u64Val);
        dma_params.reg_1b.bits.dma_endmode = 0;
    }

    if (dma_params.reg_1b.bits.dma_mode_ctrl == CVE_DMA_MODE_INTERVAL_COPY) {
        dma_params.reg_1b.bits.dma_interval_vsegsize = pstDmaCtrl->u8VerSegRows;
        dma_params.reg_1b.bits.dma_interval_hsegsize = pstDmaCtrl->u8HorSegSize;
        dma_params.reg_1b.bits.dma_interval_elemsize = pstDmaCtrl->u8ElemSize;

        dma_params.comm_params.reg_18.bits.dst_image_width =
            dma_params.comm_params.reg_1a.bits.cve_crop_xsize / pstDmaCtrl->u8HorSegSize *
            pstDmaCtrl->u8ElemSize;

        dma_params.comm_params.reg_14.bits.dst_stride_0 =
            CVE_ALIGN_UP(dma_params.comm_params.reg_18.bits.dst_image_width, CVE_ALIGN) >> 4;

        dma_params.comm_params.reg_18.bits.dst_image_height =
            CVE_ALIGN_UP(dma_params.comm_params.reg_1a.bits.cve_crop_ysize,
                         pstDmaCtrl->u8VerSegRows) /
            pstDmaCtrl->u8VerSegRows;

        dma_params.reg_1e.bits.dma_interval_xlength =
            dma_params.comm_params.reg_1a.bits.cve_crop_xsize / pstDmaCtrl->u8HorSegSize *
            pstDmaCtrl->u8HorSegSize;
    }

    *cmd_line_num = dma_task_cmd_queue(&dma_params, (unsigned int *)cmd_buf);

    return AML_SUCCESS;
}

static int cve_fill_luma_stat_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_MEM_INFO_T *pstDstMem,
                                   CVE_RECT_U16_T *pstCveLumaRect,
                                   CVE_LUMA_STAT_ARRAY_CTRL_T *pstLumaStatArrayCtrl,
                                   unsigned char idx_windows, char *cmd_buf,
                                   unsigned int *cmd_line_num)
{
    cve_op_dma_params_t luma_stat_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&luma_stat_params, 0, sizeof(cve_op_dma_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_mem(&init_params, pstDstMem,
                       CVE_ALIGN_UP(pstLumaStatArrayCtrl->u8MaxLumaRect * 4, CVE_ALIGN), &dst_off);
    if (ret != 0) {
        return ret;
    }
    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstLumaStatArrayCtrl->u8MaxLumaRect * 4;
    init_params.dst_height = 1;
    init_params.xstart = pstCveLumaRect->u16X;
    init_params.ystart = pstCveLumaRect->u16Y;
    init_params.xSize = pstCveLumaRect->u16Width;
    init_params.ySize = pstCveLumaRect->u16Height;
    cve_common_params_init(&luma_stat_params.comm_params, &init_params);

    luma_stat_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    luma_stat_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    luma_stat_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_DMA;
    luma_stat_params.reg_1b.bits.dma_mode_ctrl = 0x5;
    luma_stat_params.reg_1e.bits.cve_caption_stat_step = pstLumaStatArrayCtrl->enMode;
    luma_stat_params.reg_1e.bits.cve_caption_stat_wincnt = pstLumaStatArrayCtrl->u8MaxLumaRect;
    luma_stat_params.reg_1e.bits.cve_caption_stat_winidx = idx_windows;

    *cmd_line_num = dma_task_cmd_queue(&luma_stat_params, (unsigned int *)cmd_buf);

    return AML_SUCCESS;
}

static int cve_fill_alu_task(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                             CVE_DST_IMAGE_T *pstDst, cve_alu_sel_e alu_sel,
                             CVE_SUB_CTRL_T *pstSubCtrl, CVE_ADD_CTRL_T *pstAddCtrl, char *cmd_buf,
                             unsigned int *cmd_line_num)
{
    cve_op_alu_params_t alu_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&alu_params, 0, sizeof(cve_op_alu_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage1, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_src_image(&init_params, pstSrcImage2, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDst, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage1->u32Width;
    init_params.src_height = pstSrcImage1->u32Height;
    init_params.dst_width = pstDst->u32Width;
    init_params.dst_height = pstDst->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&alu_params.comm_params, &init_params);

    alu_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0x2;
    alu_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0x2;
    alu_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0x2;
    alu_params.comm_params.reg_02.bits.src_image_type = pstSrcImage1->enType;
    alu_params.comm_params.reg_02.bits.dst_image_type = pstDst->enType;
    alu_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_ALU;
    alu_params.comm_params.reg_02.bits.intput_mode = 0;
    alu_params.comm_params.reg_02.bits.output_mode = 0;
    alu_params.alu_31.bits.cve_alu_sel = alu_sel;
    if (alu_sel == CVE_ALU_SEL_SUB) {
        alu_params.alu_31.bits.sub_output_mode = pstSubCtrl->enMode;
        if (pstSubCtrl->enMode == CVE_SUB_MODE_THRESH) {
            alu_params.sub_99.bits.sub_thresh_ratio = pstSubCtrl->u16ThreshRatio;
        }
    } else if (alu_sel == CVE_ALU_SEL_ADD) {
        alu_params.alu_32.bits.add_u0q16x = pstAddCtrl->u0q16X;
        alu_params.alu_32.bits.add_u0q16y = pstAddCtrl->u0q16Y;
    }

    *cmd_line_num = alu_task_cmd_queue(&alu_params, (unsigned int *)cmd_buf);

    return AML_SUCCESS;
}

static int cve_fill_filter_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                CVE_FILTER_CTRL_T *pstFilterCtrl, char *cmd_buf,
                                unsigned int *cmd_line_num)
{
    cve_op_filter_params_t filter_params;
    cve_comm_init_params_t init_params;
    char uv_mask[9] = {-1, 2, 1, 2, -4, 2, 1, 2, 1};
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&filter_params, 0, sizeof(filter_params));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }
    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&filter_params.comm_params, &init_params);

    /* filter op params initialization */
    filter_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_FILTER;
    filter_params.comm_params.reg_02.bits.src_image_type = pstSrcImage->enType;
    filter_params.comm_params.reg_02.bits.dst_image_type = pstSrcImage->enType;
    if (pstSrcImage->enType == CVE_IMAGE_TYPE_U8C1) {
        filter_params.filter_1f.bits.filter_uv_en = 0;
        filter_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 2;
        filter_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 2;
    } else {
        filter_params.filter_1f.bits.filter_uv_en = 1;
    }

    filter_params.filter_1f.bits.filter_norm_uv = 4;

    memcpy(&filter_params.filter_67.reg, pstFilterCtrl->as8Mask, 24);
    filter_params.filter_6d.bits.filter_coef24 = pstFilterCtrl->as8Mask[24];
    filter_params.filter_6d.bits.filter_norm = pstFilterCtrl->u8Norm;

    memcpy(&filter_params.filter_20.reg, uv_mask, 8);
    filter_params.filter_22.bits.filter_coef_uv8 = uv_mask[8];

    *cmd_line_num = filter_task_cmd_queue(&filter_params, (unsigned int *)cmd_buf);

    return AML_SUCCESS;
}

int cve_fill_csc_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                      CVE_CSC_CTRL_T *pstCscCtrl, char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_csc_params_t csc_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;
    int csc_offset_inp0[3] = {0, -128, -128};
    int csc_matrix0[3][3] = {{256, 0, 359}, {256, -88, -183}, {256, 454, 0}};
    int csc_offset_oup0[3] = {0, 0, 0};

    int csc_offset_inp1[3] = {0, 0, 0};
    int csc_matrix1[3][3] = {{76, 150, 29}, {-43, -84, 128}, {128, -107, -21}};
    int csc_offset_oup1[3] = {0, 128, 128};

    int csc_offset_inp2[3] = {0, 0, 0};
    int csc_matrix2[3][3] = {{256, 0, 0}, {0, 256, 0}, {0, 0, 256}};
    int csc_offset_oup2[3] = {0, 0, 0};

    int *inp = NULL;
    int(*matric)[3] = NULL;
    int *oup = NULL;

    memset(&csc_params, 0, sizeof(cve_op_csc_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    /* common params initialization */
    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }
    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&csc_params.comm_params, &init_params);

    csc_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_CSC;
    csc_params.comm_params.reg_02.bits.src_image_type = pstSrcImage->enType;
    csc_params.comm_params.reg_02.bits.dst_image_type = pstDstImage->enType;
    csc_params.comm_params.reg_02.bits.yuv_image_type = 1;

    if ((pstCscCtrl->enMode == CVE_CSC_MODE_PIC_BT601_YUV2RGB) ||
        (pstCscCtrl->enMode == CVE_CSC_MODE_PIC_BT601_YUV2BGR) ||
        (pstCscCtrl->enMode == CVE_CSC_MODE_PIC_BT601_YUV2HSV) ||
        (pstCscCtrl->enMode == CVE_CSC_MODE_PIC_BT601_YUV2LAB)) {
        inp = csc_offset_inp0;
        matric = csc_matrix0;
        oup = csc_offset_oup0;
    } else if ((pstCscCtrl->enMode == CVE_CSC_MODE_PIC_BT601_RGB2YUV) ||
               (pstCscCtrl->enMode == CVE_CSC_MODE_PIC_BT601_BGR2YUV)) {
        inp = csc_offset_inp1;
        matric = csc_matrix1;
        oup = csc_offset_oup1;
    } else {
        inp = csc_offset_inp2;
        matric = csc_matrix2;
        oup = csc_offset_oup2;
    }

    if ((pstSrcImage->enType == CVE_IMAGE_TYPE_YUV420SP) ||
        (pstSrcImage->enType == CVE_IMAGE_TYPE_YUV422SP)) {
        csc_params.comm_params.reg_d8.bits.rdmif_pack_mode = 1;
        csc_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 1;
    } else if (pstSrcImage->enType == CVE_IMAGE_TYPE_U8C3_PACKAGE) {
        csc_params.comm_params.reg_d8.bits.rdmif_pack_mode = 2;
    } else if (pstSrcImage->enType == CVE_IMAGE_TYPE_U8C3_PLANAR) {
        csc_params.comm_params.reg_d8.bits.rdmif_pack_mode = 1;
        csc_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 1;
        csc_params.comm_params.reg_e8.bits.rdmif2_pack_mode = 1;
    }

    if ((pstDstImage->enType == CVE_IMAGE_TYPE_YUV420SP) ||
        (pstDstImage->enType == CVE_IMAGE_TYPE_YUV422SP)) {
        csc_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
        csc_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    } else if (pstDstImage->enType == CVE_IMAGE_TYPE_U8C3_PACKAGE) {
        csc_params.comm_params.reg_c0.bits.wdmif_pack_mode = 2;
    } else if (pstDstImage->enType == CVE_IMAGE_TYPE_U8C3_PLANAR) {
        csc_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
        csc_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
        csc_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;
    }

    csc_params.csc_24.bits.src_u6order = 0x24;
    csc_params.csc_24.bits.dst_u6order = 0x24;
    switch (pstCscCtrl->enMode) {
    case CVE_CSC_MODE_PIC_BT601_YUV2RGB: {
        csc_params.csc_23.bits.csc_mode = 0;
        csc_params.csc_24.bits.dst_u6order = 0x24;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_YUV2BGR: {
        csc_params.csc_23.bits.csc_mode = 0;
        csc_params.csc_24.bits.dst_u6order = 0x6;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_YUV2HSV: {
        csc_params.csc_23.bits.csc_mode = 1;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_YUV2LAB: {
        csc_params.csc_23.bits.csc_mode = 2;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_RGB2YUV: {
        csc_params.csc_23.bits.csc_mode = 3;
        csc_params.csc_24.bits.src_u6order = 0x24;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_BGR2YUV: {
        csc_params.csc_23.bits.csc_mode = 3;
        csc_params.csc_24.bits.src_u6order = 0x6;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_RGB2HSV: {
        csc_params.csc_23.bits.csc_mode = 4;
        csc_params.csc_24.bits.src_u6order = 0x24;
        csc_params.csc_24.bits.dst_u6order = 0x24;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_RGB2LAB: {
        csc_params.csc_23.bits.csc_mode = 5;
        csc_params.csc_24.bits.src_u6order = 0x24;
        csc_params.csc_24.bits.dst_u6order = 0x24;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_RGB2BGR: {
        csc_params.csc_23.bits.csc_mode = 0;
        csc_params.csc_24.bits.src_u6order = 0x24;
        csc_params.csc_24.bits.dst_u6order = 0x6;
        break;
    }
    case CVE_CSC_MODE_PIC_BT601_BGR2RGB: {
        csc_params.csc_23.bits.csc_mode = 0;
        csc_params.csc_24.bits.src_u6order = 0x6;
        csc_params.csc_24.bits.dst_u6order = 0x24;
        break;
    }
    default:
        CVE_ERR_TRACE("Invalid CSC mode\n");
        break;
    }

    csc_params.csc_23.bits.csc_gamma = 1;
    csc_params.csc_23.bits.csc_yuv422toyuv420_mode = 0;
    csc_params.csc_23.bits.csc_yuv444toyuv422_mode = 0;
    csc_params.csc_23.bits.csc_yuv420toyuv422_mode = 0;
    csc_params.csc_23.bits.csc_yuv422toyuv444_mode = 0;

    csc_params.csc_6e.bits.csc_offset_inp_0 = inp[0];
    csc_params.csc_6e.bits.csc_offset_inp_1 = inp[1];
    csc_params.csc_6f.bits.csc_offset_inp_2 = inp[2];

    csc_params.csc_70.bits.csc_3x3matrix_0_0 = matric[0][0];
    csc_params.csc_70.bits.csc_3x3matrix_1_0 = matric[1][0];
    csc_params.csc_70.bits.csc_3x3matrix_2_0 = matric[2][0];
    csc_params.csc_71.bits.csc_3x3matrix_0_1 = matric[0][1];
    csc_params.csc_71.bits.csc_3x3matrix_1_1 = matric[1][1];
    csc_params.csc_71.bits.csc_3x3matrix_2_1 = matric[2][1];
    csc_params.csc_72.bits.csc_3x3matrix_0_2 = matric[0][2];
    csc_params.csc_72.bits.csc_3x3matrix_1_2 = matric[1][2];
    csc_params.csc_72.bits.csc_3x3matrix_2_2 = matric[2][2];

    csc_params.csc_73.bits.csc_offset_oup_0 = oup[0];
    csc_params.csc_73.bits.csc_offset_oup_1 = oup[1];
    csc_params.csc_74.bits.csc_offset_oup_2 = oup[2];

    *cmd_line_num = csc_task_cmd_queue(&csc_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_filter_and_csc_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                        CVE_FILTER_AND_CSC_CTRL_T *pstFilterCscCtrl, char *cmd_buf,
                                        unsigned int *cmd_line_num)
{
    cve_op_filter_and_csc_params_t filter_and_csc_params;
    cve_comm_init_params_t init_params;
    int offser_inp[3] = {0, -128, -128};
    int matric[3][3] = {{256, 0, 358}, {256, -87, -182}, {256, 453, 0}};
    int offser_oup[3] = {0, 0, 0};
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&filter_and_csc_params, 0, sizeof(cve_op_filter_and_csc_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }
    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&filter_and_csc_params.comm_params, &init_params);

    /* csc op params initialization */
    filter_and_csc_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    filter_and_csc_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 1;
    filter_and_csc_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_FILTER_AND_CSC;
    filter_and_csc_params.comm_params.reg_02.bits.src_image_type = pstSrcImage->enType;
    filter_and_csc_params.comm_params.reg_02.bits.dst_image_type = pstDstImage->enType;
    /*FIX ME: yuv_image_type nv12 or nv21*/
    filter_and_csc_params.comm_params.reg_02.bits.yuv_image_type = 1;

    if (pstDstImage->enType == CVE_IMAGE_TYPE_U8C3_PACKAGE) {
        filter_and_csc_params.comm_params.reg_c0.bits.wdmif_pack_mode = 2;
    }

    if (pstDstImage->enType == CVE_IMAGE_TYPE_U8C3_PLANAR) {
        filter_and_csc_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;
        filter_and_csc_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
        filter_and_csc_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    }

    filter_and_csc_params.csc_23.bits.csc_gamma = 1;
    filter_and_csc_params.csc_23.bits.csc_yuv422toyuv420_mode = 0;
    filter_and_csc_params.csc_23.bits.csc_yuv444toyuv422_mode = 0;
    filter_and_csc_params.csc_23.bits.csc_yuv420toyuv422_mode = 0;
    filter_and_csc_params.csc_23.bits.csc_yuv422toyuv444_mode = 0;

    if (pstFilterCscCtrl->enMode == CVE_CSC_MODE_PIC_BT601_YUV2RGB) {
        filter_and_csc_params.csc_24.bits.dst_u6order = 0x24;
    } else {
        filter_and_csc_params.csc_24.bits.dst_u6order = 0x6;
    }
    filter_and_csc_params.csc_24.bits.src_u6order = 0x24;

    filter_and_csc_params.csc_6e.bits.csc_offset_inp_0 = offser_inp[0];
    filter_and_csc_params.csc_6e.bits.csc_offset_inp_1 = offser_inp[1];
    filter_and_csc_params.csc_6f.bits.csc_offset_inp_2 = offser_inp[2];

    filter_and_csc_params.csc_70.bits.csc_3x3matrix_0_0 = matric[0][0];
    filter_and_csc_params.csc_70.bits.csc_3x3matrix_1_0 = matric[1][0];
    filter_and_csc_params.csc_70.bits.csc_3x3matrix_2_0 = matric[2][0];
    filter_and_csc_params.csc_71.bits.csc_3x3matrix_0_1 = matric[0][1];
    filter_and_csc_params.csc_71.bits.csc_3x3matrix_1_1 = matric[1][1];
    filter_and_csc_params.csc_71.bits.csc_3x3matrix_2_1 = matric[2][1];
    filter_and_csc_params.csc_72.bits.csc_3x3matrix_0_2 = matric[0][2];
    filter_and_csc_params.csc_72.bits.csc_3x3matrix_1_2 = matric[1][2];
    filter_and_csc_params.csc_72.bits.csc_3x3matrix_2_2 = matric[2][2];

    filter_and_csc_params.csc_73.bits.csc_offset_oup_0 = offser_oup[0];
    filter_and_csc_params.csc_73.bits.csc_offset_oup_1 = offser_oup[1];
    filter_and_csc_params.csc_74.bits.csc_offset_oup_2 = offser_oup[2];

    memcpy(&filter_and_csc_params.filter_67.reg, pstFilterCscCtrl->as8Mask, 24);
    filter_and_csc_params.filter_6d.bits.filter_coef24 = pstFilterCscCtrl->as8Mask[24];
    filter_and_csc_params.filter_6d.bits.filter_norm = pstFilterCscCtrl->u8Norm;

    *cmd_line_num = filter_and_csc_task_cmd_queue(&filter_and_csc_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_sobel_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstH,
                               CVE_DST_IMAGE_T *pstDstV, CVE_SOBEL_CTRL_T *pstSobelCtrl,
                               char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_sobel_params_t sobel_params;
    cve_comm_init_params_t init_params;
    char mask_v[25];
    int i = 0;
    int j = 0;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&sobel_params, 0, sizeof(cve_op_sobel_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstDstH, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstDstV, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstSrcImage->u32Width;
    init_params.dst_height = pstSrcImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&sobel_params.comm_params, &init_params);

    /* sobel op params initialization */
    sobel_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    sobel_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    sobel_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_SOBEL;
    sobel_params.sobel_25.bits.sobel_output_mode = pstSobelCtrl->enOutCtrl;

    memcpy(&sobel_params.sobel_75.reg, pstSobelCtrl->as8Mask, 24);
    sobel_params.sobel_7b.bits.sobel_coef_h_24 = pstSobelCtrl->as8Mask[24];

    for (j = 0; j < 5; j++) {
        for (i = 0; i < 5; i++) {
            mask_v[j * 5 + i] = pstSobelCtrl->as8Mask[i * 5 + j];
        }
    }

    memcpy(&sobel_params.sobel_7c.reg, mask_v, 24);
    sobel_params.sobel_82.bits.sobel_coef_v_24 = mask_v[24];

    *cmd_line_num = sobel_task_cmd_queue(&sobel_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_dilate_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                CVE_DILATE_CTRL_T *pstDilateCtrl, char *cmd_buf,
                                unsigned int *cmd_line_num)
{

    cve_op_erode_dilate_params_t erode_dilate_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&erode_dilate_params, 0, sizeof(cve_op_erode_dilate_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;

    cve_common_params_init(&erode_dilate_params.comm_params, &init_params);

    erode_dilate_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    erode_dilate_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    erode_dilate_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_DILATE_AND_ERODE;
    erode_dilate_params.comm_params.reg_02.bits.output_mode = 0;
    erode_dilate_params.erode_dilate_2E.bits.erodedilate_sel = 1;

    memcpy(&erode_dilate_params.filter_67.reg, pstDilateCtrl->au8Mask, 24);
    erode_dilate_params.filter_6d.bits.filter_coef24 = pstDilateCtrl->au8Mask[24];

    *cmd_line_num = erode_dilate_task_cmd_queue(&erode_dilate_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_erode_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                               CVE_ERODE_CTRL_T *pstErodeCtrl, char *cmd_buf,
                               unsigned int *cmd_line_num)
{
    cve_op_erode_dilate_params_t erode_dilate_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&erode_dilate_params, 0, sizeof(cve_op_erode_dilate_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&erode_dilate_params.comm_params, &init_params);

    erode_dilate_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    erode_dilate_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    erode_dilate_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_DILATE_AND_ERODE;
    erode_dilate_params.comm_params.reg_02.bits.output_mode = 0;
    erode_dilate_params.erode_dilate_2E.bits.erodedilate_sel = 0;

    memcpy(&erode_dilate_params.filter_67.reg, pstErodeCtrl->au8Mask, 24);
    erode_dilate_params.filter_6d.bits.filter_coef24 = pstErodeCtrl->au8Mask[24];

    *cmd_line_num = erode_dilate_task_cmd_queue(&erode_dilate_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_thresh_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                CVE_THRESH_CTRL_T *pstThreshCtrl, char *cmd_buf,
                                unsigned int *cmd_line_num)
{
    cve_op_thresh_params_t thresh_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&thresh_params, 0, sizeof(cve_op_thresh_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&thresh_params.comm_params, &init_params);

    thresh_params.comm_params.reg_d8.bits.rdmif_pack_mode = 2;
    thresh_params.comm_params.reg_c0.bits.wdmif_pack_mode = 2;
    thresh_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_THRESH;
    thresh_params.thresh_2f.bits.thresh_u8max_val = pstThreshCtrl->u8MaxVal;
    thresh_params.thresh_2f.bits.thresh_mode = pstThreshCtrl->enMode;
    thresh_params.thresh_30.bits.thresh_u8mid_val = pstThreshCtrl->u8MidVal;
    thresh_params.thresh_30.bits.thresh_u8min_val = pstThreshCtrl->u8MinVal;
    thresh_params.thresh_30.bits.thresh_u8high_thr = pstThreshCtrl->u8HighThr;
    thresh_params.thresh_30.bits.thresh_u8low_thr = pstThreshCtrl->u8LowThr;

    *cmd_line_num = thresh_task_cmd_queue(&thresh_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_integ_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                               CVE_INTEG_CTRL_T *pstIntegCtrl, char *cmd_buf,
                               unsigned int *cmd_line_num)
{
    cve_op_integ_params_t integ_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&integ_params, 0, sizeof(cve_op_integ_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }
    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    if ((pstIntegCtrl->enOutCtrl == CVE_INTEG_OUT_CTRL_COMBINE) ||
        (pstIntegCtrl->enOutCtrl == CVE_INTEG_OUT_CTRL_TQSUM)) {
        init_params.dst_stride[1] = init_params.dst_stride[0];
    }
    cve_common_params_init(&integ_params.comm_params, &init_params);

    integ_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    integ_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    integ_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    integ_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_INTEG;
    integ_params.integ_33.bits.integ_out_ctrl = pstIntegCtrl->enOutCtrl;

    *cmd_line_num = integ_task_cmd_queue(&integ_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_hist_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_MEM_INFO_T *pstDstMem,
                              char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_hist_params_t hist_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    int ret = 0;

    memset(&hist_params, 0, sizeof(cve_op_hist_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&hist_params.comm_params, &init_params);
    hist_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    hist_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_HIST;
    hist_params.integ_33.bits.eqhist_curv_mode = 1;
    hist_params.eqhist_34.bits.eqhist_norm = (0xffffffff / init_params.xSize) / init_params.ySize;

    *cmd_line_num = hist_task_cmd_queue(&hist_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_mag_and_ang_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstMag,
                                     CVE_DST_IMAGE_T *pstDstAng,
                                     CVE_MAG_AND_ANG_CTRL_T *pstMagAndAngCtrl, char *cmd_buf,
                                     unsigned int *cmd_line_num)
{
    cve_op_mag_and_ang_params_t mag_and_ang_params;
    cve_comm_init_params_t init_params;
    char mask_v[25];
    int i = 0;
    int j = 0;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&mag_and_ang_params, 0, sizeof(cve_op_mag_and_ang_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstMag, &dst_off);
    if (ret != 0) {
        return ret;
    }
    if ((pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_MAG_AND_ANG) ||
        (pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_HOG)) {
        ret = fill_dst_image(&init_params, pstDstAng, &dst_off);
        if (ret != 0) {
            return ret;
        }
    }
    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstSrcImage->u32Width;
    init_params.dst_height = pstSrcImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&mag_and_ang_params.comm_params, &init_params);
    if (pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_MAG_AND_ANG) {
        mag_and_ang_params.comm_params.reg_14.bits.dst_stride_1 =
            mag_and_ang_params.comm_params.reg_14.bits.dst_stride_1 >> 1;
    }

    mag_and_ang_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    mag_and_ang_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    mag_and_ang_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    mag_and_ang_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_MAG_AND_ANG;
    mag_and_ang_params.mag_and_ang_26.bits.magandang_output_ctrl = pstMagAndAngCtrl->enOutCtrl;
    mag_and_ang_params.mag_and_ang_26.bits.magandang_u16thr = pstMagAndAngCtrl->u16Thr;
    mag_and_ang_params.mag_and_ang_26.bits.magandang_mode = 1;

    if (pstMagAndAngCtrl->enOutCtrl == CVE_MAG_AND_ANG_OUT_CTRL_MAG_AND_ANG) {
        mag_and_ang_params.comm_params.reg_14.bits.dst_stride_1 =
            ((init_params.dst_stride[1] >> 4) + 1) >> 1;
    }
    memcpy(&mag_and_ang_params.sobel_75.reg, pstMagAndAngCtrl->as8Mask, 24);
    mag_and_ang_params.sobel_7b.bits.sobel_coef_h_24 = pstMagAndAngCtrl->as8Mask[24];

    for (j = 0; j < 5; j++) {
        for (i = 0; i < 5; i++) {
            mask_v[j * 5 + i] = pstMagAndAngCtrl->as8Mask[i * 5 + j];
        }
    }

    memcpy(&mag_and_ang_params.sobel_7c.reg, mask_v, 24);
    mag_and_ang_params.sobel_82.bits.sobel_coef_v_24 = mask_v[24];

    *cmd_line_num = mag_and_ang_task_cmd_queue(&mag_and_ang_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_match_bg_model_task(CVE_SRC_IMAGE_T *pstCurImg, CVE_SRC_IMAGE_T *pstPreImg,
                                        CVE_MEM_INFO_T *pstBgModel, CVE_DST_IMAGE_T *pstFg,
                                        CVE_DST_IMAGE_T *pstBg, CVE_DST_IMAGE_T *pstCurDiffBg,
                                        CVE_DST_IMAGE_T *pstFrmDiff,
                                        CVE_MATCH_BG_MODEL_CTRL_T *pstMatchBgModelCtrl,
                                        char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_match_bg_model_params_t match_bg_model_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    unsigned int dst4_stride = 0;
    int ret = 0;

    memset(&match_bg_model_params, 0, sizeof(cve_op_match_bg_model_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstCurImg, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_image(&init_params, pstPreImg, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstFg, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstBg, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstCurDiffBg, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstFrmDiff, &dst_off);
    if (ret != 0) {
        return ret;
    }
    dst4_stride = pstCurImg->au32Stride[0] >> pstMatchBgModelCtrl->enDownScaleMode;
    ret = fill_dst_mem(&init_params, pstBgModel, dst4_stride * 16, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstCurImg->u32Width;
    init_params.src_height = pstCurImg->u32Height;
    init_params.dst_width = pstFg->u32Width;
    init_params.dst_height = pstFg->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&match_bg_model_params.comm_params, &init_params);

    match_bg_model_params.comm_params.reg_02.bits.output_mode = 0;

    match_bg_model_params.comm_params.reg_e8.bits.rdmif2_pack_mode = 0;
    match_bg_model_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    match_bg_model_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_MATCH_BG_MODEL;

    if (pstMatchBgModelCtrl->enDownScaleMode == CVE_MATCH_BG_MODEL_DOWN_SCALE_MODE_2X2) {
        match_bg_model_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 1;
        match_bg_model_params.comm_params.reg_d8.bits.rdmif_pack_mode = 1;
    } else if (pstMatchBgModelCtrl->enDownScaleMode == CVE_MATCH_BG_MODEL_DOWN_SCALE_MODE_4X4) {
        match_bg_model_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 2;
        match_bg_model_params.comm_params.reg_d8.bits.rdmif_pack_mode = 2;
    } else {
        match_bg_model_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
        match_bg_model_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    }

    match_bg_model_params.bg_mode_27.bits.bgmodel_ds_mode = pstMatchBgModelCtrl->enDownScaleMode;
    match_bg_model_params.bg_mode_27.bits.bgmodel_output_ctrl = pstMatchBgModelCtrl->enOutputMode;
    match_bg_model_params.bg_mode_27.bits.bgmodel_u8gray_thr = pstMatchBgModelCtrl->u8GrayThr;
    match_bg_model_params.bg_mode_27.bits.bgmodel_u8q4dist_thr = pstMatchBgModelCtrl->u8q4DistThr;

    *cmd_line_num = match_bg_model_task_cmd_queue(&match_bg_model_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_update_bg_model_task(CVE_SRC_IMAGE_T *pstCurImg, CVE_MEM_INFO_T *pstBgModel1,
                                         CVE_MEM_INFO_T *pstBgModel2,
                                         CVE_UPDATE_BG_MODEL_CTRL_T *pstUpdateBgModelCtrl,
                                         char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_update_bg_model_params_t update_bg_model_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    unsigned int dst_stride = 0;
    int ret = 0;

    memset(&update_bg_model_params, 0, sizeof(cve_op_update_bg_model_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstCurImg, &src_off);
    if (ret != 0) {
        return ret;
    }

    dst_stride = pstCurImg->au32Stride[0] >> pstUpdateBgModelCtrl->enDownScaleMode;
    ret = fill_dst_mem(&init_params, pstBgModel1, dst_stride * 16, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstBgModel2, dst_stride * 16, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstCurImg->u32Width;
    init_params.src_height = pstCurImg->u32Height;
    init_params.dst_width = pstCurImg->u32Width >> pstUpdateBgModelCtrl->enDownScaleMode;
    init_params.dst_height = pstCurImg->u32Height >> pstUpdateBgModelCtrl->enDownScaleMode;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&update_bg_model_params.comm_params, &init_params);

    update_bg_model_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    update_bg_model_params.comm_params.reg_e8.bits.rdmif2_pack_mode = 0;
    update_bg_model_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
    update_bg_model_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_UPDATE_BG_MODEL;

    if (pstUpdateBgModelCtrl->enDownScaleMode == CVE_UPDATE_BG_MODEL_DOWN_SCALE_MODE_2X2) {
        update_bg_model_params.comm_params.reg_d8.bits.rdmif_pack_mode = 1;
    } else if (pstUpdateBgModelCtrl->enDownScaleMode == CVE_UPDATE_BG_MODEL_DOWN_SCALE_MODE_4X4) {
        update_bg_model_params.comm_params.reg_d8.bits.rdmif_pack_mode = 2;
    } else {
        update_bg_model_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    }
    update_bg_model_params.update_bg_mode_2a.bits.updatebgmodel_u16del_thr =
        pstUpdateBgModelCtrl->u16DelThr;
    update_bg_model_params.update_bg_mode_2a.bits.updatebgmodel_u16frq_thr =
        pstUpdateBgModelCtrl->u16FrqThr;
    update_bg_model_params.update_bg_mode_2b.bits.updatebgmodel_u0q16lr =
        pstUpdateBgModelCtrl->u0q16LearnRate;
    update_bg_model_params.update_bg_mode_2b.bits.updatebgmodel_u16interval_thr =
        pstUpdateBgModelCtrl->u16IntervalThr;

    update_bg_model_params.bg_mode_27.bits.bgmodel_ds_mode = pstUpdateBgModelCtrl->enDownScaleMode;
    *cmd_line_num =
        update_bg_model_task_cmd_queue(&update_bg_model_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_equalize_hist_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                       CVE_EQUALIZE_HIST_CTRL_T *pstEqualizeHistCtrl, char *cmd_buf,
                                       unsigned int *cmd_line_num)
{
    cve_op_hist_params_t equalize_hist_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&equalize_hist_params, 0, sizeof(cve_op_hist_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&equalize_hist_params.comm_params, &init_params);
    equalize_hist_params.comm_params.reg_d8.bits.rdmif_pack_mode = 2;
    equalize_hist_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    equalize_hist_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_EQUALIZE_HIST;
    equalize_hist_params.integ_33.bits.eqhist_curv_mode = 1;
    equalize_hist_params.eqhist_34.bits.eqhist_norm =
        (0xffffffff / init_params.xSize) / init_params.ySize;

    *cmd_line_num = hist_task_cmd_queue(&equalize_hist_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_thresh_s16_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                    CVE_THRESH_S16_CTRL_T *pstThreshS16Ctrl, char *cmd_buf,
                                    unsigned int *cmd_line_num)
{
    cve_op_thresh_s16_params_t thresh_s16_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&thresh_s16_params, 0, sizeof(cve_op_thresh_s16_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&thresh_s16_params.comm_params, &init_params);

    thresh_s16_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    thresh_s16_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    thresh_s16_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_THRESH_S16;
    thresh_s16_params.comm_params.reg_02.bits.dst_image_type = pstDstImage->enType;
    thresh_s16_params.comm_params.reg_02.bits.src_image_type = pstSrcImage->enType;
    thresh_s16_params.thresh_s16_35.bits.thresh_max_val = pstThreshS16Ctrl->un8MaxVal.u8Val;
    thresh_s16_params.thresh_s16_35.bits.thresh_s16tos8oru8_mode = pstThreshS16Ctrl->enMode;
    thresh_s16_params.thresh_s16_35.bits.thresh_mid_val = pstThreshS16Ctrl->un8MidVal.u8Val;
    thresh_s16_params.thresh_s16_35.bits.thresh_min_val = pstThreshS16Ctrl->un8MinVal.u8Val;
    thresh_s16_params.thresh_s16_36.bits.thresh_s16highthr = pstThreshS16Ctrl->s16HighThr;
    thresh_s16_params.thresh_s16_36.bits.thresh_s16lowthr = pstThreshS16Ctrl->s16LowThr;

    *cmd_line_num = thresh_s16_task_cmd_queue(&thresh_s16_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_thresh_u16_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                    CVE_THRESH_U16_CTRL_T *pstThreshU16Ctrl, char *cmd_buf,
                                    unsigned int *cmd_line_num)
{
    cve_op_thresh_u16_params_t thresh_u16_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&thresh_u16_params, 0, sizeof(cve_op_thresh_u16_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&thresh_u16_params.comm_params, &init_params);

    thresh_u16_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    thresh_u16_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    thresh_u16_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_THRESH_U16;
    thresh_u16_params.comm_params.reg_02.bits.dst_image_type = pstDstImage->enType;
    thresh_u16_params.comm_params.reg_02.bits.src_image_type = pstSrcImage->enType;
    thresh_u16_params.thresh_u16_37.bits.thresh_max_val = pstThreshU16Ctrl->u8MaxVal;
    thresh_u16_params.thresh_u16_37.bits.thresh_u16tou8_mode = pstThreshU16Ctrl->enMode;
    thresh_u16_params.thresh_u16_37.bits.thresh_mid_val = pstThreshU16Ctrl->u8MidVal;
    thresh_u16_params.thresh_u16_37.bits.thresh_min_val = pstThreshU16Ctrl->u8MinVal;
    thresh_u16_params.thresh_u16_38.bits.thresh_u16_u16highthr = pstThreshU16Ctrl->u16HighThr;
    thresh_u16_params.thresh_u16_38.bits.thresh_u16_u16lowthr = pstThreshU16Ctrl->u16LowThr;

    *cmd_line_num = thresh_u16_task_cmd_queue(&thresh_u16_params, (unsigned int *)cmd_buf);

    return 0;
}

static unsigned int cve_fill_16bit_to_8bit_task(CVE_SRC_IMAGE_T *pstSrcImage,
                                                CVE_DST_IMAGE_T *pstDstImage,
                                                CVE_16BIT_TO_8BIT_CTRL_T *pst16BitTo8BitCtrl,
                                                char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_16bit_to_8bit_params_t _16bit_to_8bit_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&_16bit_to_8bit_params, 0, sizeof(cve_op_16bit_to_8bit_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&_16bit_to_8bit_params.comm_params, &init_params);

    _16bit_to_8bit_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    _16bit_to_8bit_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    _16bit_to_8bit_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_16BIT_TO_8BIT;
    _16bit_to_8bit_params.comm_params.reg_02.bits.dst_image_type = pstDstImage->enType;
    _16bit_to_8bit_params.comm_params.reg_02.bits.src_image_type = pstSrcImage->enType;
    _16bit_to_8bit_params._16bit_to_8bit_39.bits._16bitto8bit_mode = pst16BitTo8BitCtrl->enMode;
    _16bit_to_8bit_params._16bit_to_8bit_3a.bits._16bitto8bit_s8bias = pst16BitTo8BitCtrl->s8Bias;
    _16bit_to_8bit_params._16bit_to_8bit_3a.bits._16bitto8bit_u0q16norm =
        pst16BitTo8BitCtrl->u8Numerator * 65536 / pst16BitTo8BitCtrl->u16Denominator;

    *cmd_line_num = _16bit_to_8bit_task_cmd_queue(&_16bit_to_8bit_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_ord_stat_filter_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                                         CVE_ORD_STAT_FILTER_CTRL_T *pstOrdStatFltCtrl,
                                         char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_ord_stat_filter_params_t stat_filter_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&stat_filter_params, 0, sizeof(cve_op_ord_stat_filter_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&stat_filter_params.comm_params, &init_params);

    stat_filter_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    stat_filter_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    stat_filter_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_ORD_STAT_FILTER;
    stat_filter_params.stat_filter_3b.bits.stat_filter_outnum = 1;
    if (pstOrdStatFltCtrl->enMode == CVE_ORD_STAT_FILTER_MODE_MEDIAN) {
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx0 = 4;
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx1 = 4;
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx2 = 4;
    } else if (pstOrdStatFltCtrl->enMode == CVE_ORD_STAT_FILTER_MODE_MAX) {
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx0 = 8;
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx1 = 8;
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx2 = 8;
    } else {
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx0 = 0;
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx1 = 0;
        stat_filter_params.stat_filter_3b.bits.stat_filter_outidx2 = 0;
    }

    *cmd_line_num = ord_stat_filter_task_cmd_queue(&stat_filter_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_ncc_task(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                             CVE_NCC_CTRL_T *pstNccCtrl, char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_ncc_params_t ncc_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    int ret = 0;

    memset(&ncc_params, 0, sizeof(cve_op_ncc_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage1, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_src_image(&init_params, pstSrcImage2, &src_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage1->u32Width;
    init_params.src_height = pstSrcImage1->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&ncc_params.comm_params, &init_params);

    ncc_params.comm_params.reg_d8.bits.rdmif_pack_mode = 2;
    ncc_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 2;
    ncc_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_NCC;

    ncc_params.ncc_3c.bits.ncc_mode = pstNccCtrl->enMode;
    if (pstNccCtrl->enMode == CVE_NCC_MODE_SIMILAR) {
        ncc_params.ncc_3c.bits.ncc_offset1 = pstNccCtrl->u8Src1Offset;
        ncc_params.ncc_3c.bits.ncc_offset0 = pstNccCtrl->u8Src0Offset;
    }

    *cmd_line_num = ncc_task_cmd_queue(&ncc_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_canny_hys_edge_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstEdge,
                                        CVE_DST_MEM_INFO_T *pstStack,
                                        CVE_CANNY_HYS_EDGE_CTRL_T *pstCannyHysEdgeCtrl,
                                        char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_canny_edge_params_t canny_params;
    cve_comm_init_params_t init_params;
    char mask_v[25];
    int i = 0;
    int j = 0;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&canny_params, 0, sizeof(cve_op_canny_edge_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_mem(&init_params, &pstCannyHysEdgeCtrl->stMem, pstSrcImage->au32Stride[0] * 4,
                       &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstEdge, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstStack, pstEdge->au32Stride[0] * sizeof(CVE_POINT_U16_T),
                       &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, &pstCannyHysEdgeCtrl->stMem, pstEdge->au32Stride[0] * 4,
                       &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstEdge->u32Width;
    init_params.dst_height = pstEdge->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&canny_params.comm_params, &init_params);

    canny_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    canny_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    canny_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 2;
    canny_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;
    canny_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
    canny_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_CANNY_HYS_EDGE;

    memcpy(&canny_params.sobel_75.reg, pstCannyHysEdgeCtrl->as8Mask, 24);
    canny_params.sobel_7b.bits.sobel_coef_h_24 = pstCannyHysEdgeCtrl->as8Mask[24];
    for (j = 0; j < 5; j++) {
        for (i = 0; i < 5; i++) {
            mask_v[j * 5 + i] = pstCannyHysEdgeCtrl->as8Mask[i * 5 + j];
        }
    }

    memcpy(&canny_params.sobel_7c.reg, mask_v, 24);
    canny_params.sobel_82.bits.sobel_coef_v_24 = mask_v[24];

    canny_params.comm_params.reg_14.bits.dst_stride_0 =
        (canny_params.comm_params.reg_14.bits.dst_stride_0 + 3) >> 2;
    canny_params.canny_4a.bits.canny_u16highthr = pstCannyHysEdgeCtrl->u16HighThr;
    canny_params.canny_4a.bits.canny_u16lowthr = pstCannyHysEdgeCtrl->u16LowThr;
    canny_params.canny_4c.bits.canny_gauss_en = pstCannyHysEdgeCtrl->bGauss;

    *cmd_line_num = canny_edge_task_cmd_queue(&canny_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_lbp_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                             CVE_LBP_CTRL_T *pstLbpCtrl, char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_lbp_params_t lbp_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&lbp_params, 0, sizeof(cve_op_lbp_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&lbp_params.comm_params, &init_params);

    lbp_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    lbp_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    lbp_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_LBP;
    lbp_params.lbp_4d.bits.u8bitthr = pstLbpCtrl->un8BitThr.u8Val;
    lbp_params.lbp_4d.bits.cmp_mode = pstLbpCtrl->enMode;

    *cmd_line_num = lbp_task_cmd_queue(&lbp_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_norm_grad_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstH,
                                   CVE_DST_IMAGE_T *pstDstV, CVE_DST_IMAGE_T *pstDstHV,
                                   CVE_NORM_GRAD_CTRL_T *pstNormGradCtrl, char *cmd_buf,
                                   unsigned int *cmd_line_num)
{
    cve_op_norm_grad_params_t norm_grad_params;
    cve_comm_init_params_t init_params;
    char mask_v[25];
    int i = 0;
    int j = 0;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&norm_grad_params, 0, sizeof(cve_op_norm_grad_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstDstH, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstDstV, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstDstHV, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstSrcImage->u32Width;
    init_params.dst_height = pstSrcImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&norm_grad_params.comm_params, &init_params);

    norm_grad_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    norm_grad_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    norm_grad_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    norm_grad_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;

    norm_grad_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_NROM_GRAD;
    norm_grad_params.norm_grad_4e.bits.normgrad_u8norm = pstNormGradCtrl->u8Norm;
    norm_grad_params.norm_grad_4e.bits.normgrad_outmode = pstNormGradCtrl->enOutCtrl;

    memcpy(&norm_grad_params.sobel_75.reg, pstNormGradCtrl->as8Mask, 24);
    norm_grad_params.sobel_7b.bits.sobel_coef_h_24 = pstNormGradCtrl->as8Mask[24];
    for (j = 0; j < 5; j++) {
        for (i = 0; i < 5; i++) {
            mask_v[j * 5 + i] = pstNormGradCtrl->as8Mask[i * 5 + j];
        }
    }

    memcpy(&norm_grad_params.sobel_7c.reg, mask_v, 24);
    norm_grad_params.sobel_82.bits.sobel_coef_v_24 = mask_v[24];

    *cmd_line_num = norm_grad_task_cmd_queue(&norm_grad_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_st_candi_corner_task(CVE_SRC_IMAGE_T *pstSrc, CVE_DST_IMAGE_T *pstLabel,
                                         CVE_DST_IMAGE_T *pstCandiCorner,
                                         CVE_DST_MEM_INFO_T *pstCandiCornerPoint,
                                         CVE_ST_CANDI_CORNER_CTRL_T *pstStCandiCornerCtrl,
                                         char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_st_candi_corner_params_t st_candi_corner_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&st_candi_corner_params, 0, sizeof(cve_op_st_candi_corner_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrc, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_mem(&init_params, &pstStCandiCornerCtrl->stMem,
                       pstCandiCorner->au32Stride[0] * 2, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstLabel, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, &pstStCandiCornerCtrl->stMem,
                       pstCandiCorner->au32Stride[0] * 2, &dst_off);
    if (ret != 0) {
        return ret;
    }

    dst_off++;

    ret = fill_dst_image(&init_params, pstCandiCorner, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstCandiCornerPoint,
                       pstCandiCorner->au32Stride[0] * sizeof(CVE_POINT_U16_T), &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrc->u32Width;
    init_params.src_height = pstSrc->u32Height;
    init_params.dst_width = pstCandiCorner->u32Width;
    init_params.dst_height = pstCandiCorner->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&st_candi_corner_params.comm_params, &init_params);

    st_candi_corner_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    st_candi_corner_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
    st_candi_corner_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;

    st_candi_corner_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_ST_CANDI_CORNER;
    st_candi_corner_params.comm_params.reg_02.bits.output_mode =
        pstStCandiCornerCtrl->enOutputDataMode;

    *cmd_line_num =
        st_candi_corner_task_cmd_queue(&st_candi_corner_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_sad_task(CVE_SRC_IMAGE_T *pstSrcImage1, CVE_SRC_IMAGE_T *pstSrcImage2,
                             CVE_DST_IMAGE_T *pstSad, CVE_DST_IMAGE_T *pstThr,
                             CVE_SAD_CTRL_T *pstSadCtrl, char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_sad_params_t sad_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&sad_params, 0, sizeof(cve_op_sad_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage1, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_src_image(&init_params, pstSrcImage2, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstSad, &dst_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstThr, &dst_off);
    if (ret != 0) {
        return ret;
    }
    init_params.src_width = pstSrcImage1->u32Width;
    init_params.src_height = pstSrcImage1->u32Height;
    init_params.dst_width = pstSrcImage1->u32Width;
    init_params.dst_height = pstSrcImage1->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&sad_params.comm_params, &init_params);

    sad_params.comm_params.reg_d8.bits.rdmif_pack_mode = 2;
    sad_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    sad_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    sad_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 2;

    sad_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_SAD;
    sad_params.sad_64.bits.sad_u8minval = pstSadCtrl->u8MinVal;
    sad_params.sad_64.bits.sad_mode = pstSadCtrl->enMode;
    sad_params.sad_65.bits.sad_u4rightshift = 0;
    sad_params.sad_65.bits.sad_u8maxval = pstSadCtrl->u8MaxVal;
    sad_params.sad_65.bits.sad_u16thr = pstSadCtrl->u16Thr;

    *cmd_line_num = sad_task_cmd_queue(&sad_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_grad_fg_task(CVE_SRC_IMAGE_T *pstBgDiffFg, CVE_SRC_IMAGE_T *pstCurGrad,
                                 CVE_SRC_IMAGE_T *pstBgGrad, CVE_DST_IMAGE_T *pstGradFg,
                                 CVE_GRAD_FG_CTRL_T *pstGradFgCtrl, char *cmd_buf,
                                 unsigned int *cmd_line_num)
{
    cve_op_grad_fg_params_t grad_fg_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&grad_fg_params, 0, sizeof(cve_op_grad_fg_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstBgDiffFg, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_src_image(&init_params, pstCurGrad, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_src_image(&init_params, pstBgGrad, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstGradFg, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstBgDiffFg->u32Width;
    init_params.src_height = pstBgDiffFg->u32Height;
    init_params.dst_width = pstBgDiffFg->u32Width;
    init_params.dst_height = pstBgDiffFg->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    if ((pstGradFgCtrl->enMode == CVE_GRAD_FG_MODE_THR) ||
        (pstGradFgCtrl->enMode == CVE_GRAD_FG_MODE_THR_FG)) {
        init_params.src_stride[3] = init_params.src_stride[2];
        init_params.dst_stride[1] = init_params.dst_stride[0] * 2;
    }
    cve_common_params_init(&grad_fg_params.comm_params, &init_params);

    grad_fg_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    grad_fg_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    grad_fg_params.comm_params.reg_e8.bits.rdmif2_pack_mode = 0;
    grad_fg_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;

    grad_fg_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_GRAD_FG;
    grad_fg_params.grad_fg_66.bits.gradfg_mode = pstGradFgCtrl->enMode;
    grad_fg_params.grad_fg_66.bits.gradfg_u8minmagdiff = pstGradFgCtrl->u8MinMagDiff;

    *cmd_line_num = grad_fg_task_cmd_queue(&grad_fg_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_map_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_SRC_MEM_INFO_T *pstMap,
                             CVE_DST_IMAGE_T *pstDstImage, CVE_MAP_CTRL_T *pstMapCtrl,
                             char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_map_params_t map_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&map_params, 0, sizeof(cve_op_map_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }
    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.dst_width;
    init_params.ySize = init_params.dst_height;
    cve_common_params_init(&map_params.comm_params, &init_params);

    map_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    map_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    map_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_MAP;
    map_params.comm_params.reg_02.bits.src_image_type = pstSrcImage->enType;
    map_params.comm_params.reg_02.bits.dst_image_type = pstDstImage->enType;
    map_params.map_3c.bits.map_mode = pstMapCtrl->enMode;

    *cmd_line_num = map_task_cmd_queue(&map_params, (unsigned int *)cmd_buf);

    return 0;
}
static int cve_fill_build_lk_optical_flow_pyr_task(
    CVE_SRC_IMAGE_T *pstSrcPyr, CVE_SRC_IMAGE_T astDstPyr[],
    CVE_BUILD_LK_OPTICAL_FLOW_PYR_CTRL_T *pstBuildLkOptiFlowPyrCtrl, char *cmd_buf,
    unsigned int *cmd_line_num)
{
    cve_op_build_lk_optical_flow_pyr_params_t build_lk_optical_flow_pyr_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;
    int i = 0;

    memset(&build_lk_optical_flow_pyr_params, 0, sizeof(cve_op_build_lk_optical_flow_pyr_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcPyr, &src_off);
    if (ret != 0) {
        return ret;
    }

    for (i = 0; i <= pstBuildLkOptiFlowPyrCtrl->u8MaxLevel; i++) {
        ret = fill_dst_image(&init_params, &astDstPyr[i], &dst_off);
        if (ret != 0) {
            return ret;
        }
    }

    init_params.src_width = pstSrcPyr->u32Width;
    init_params.src_height = pstSrcPyr->u32Height;
    init_params.dst_width = pstSrcPyr->u32Width;
    init_params.dst_height = pstSrcPyr->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&build_lk_optical_flow_pyr_params.comm_params, &init_params);

    build_lk_optical_flow_pyr_params.comm_params.reg_d8.bits.rdmif_pack_mode = 1;
    build_lk_optical_flow_pyr_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    build_lk_optical_flow_pyr_params.comm_params.reg_02.bits.cve_op_type =
        CVE_OP_TYPE_BULID_LK_OPTICAL_FLOW_PYR;

    if (pstBuildLkOptiFlowPyrCtrl->u8MaxLevel == 2) {
        build_lk_optical_flow_pyr_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    }

    if (pstBuildLkOptiFlowPyrCtrl->u8MaxLevel == 3) {
        build_lk_optical_flow_pyr_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;
    }

    build_lk_optical_flow_pyr_params.bdlk_4f.bits.bdlk_maxLevel =
        pstBuildLkOptiFlowPyrCtrl->u8MaxLevel;

    *cmd_line_num = build_lk_optical_flow_pyr_task_cmd_queue(&build_lk_optical_flow_pyr_params,
                                                             (unsigned int *)cmd_buf);
    return 0;
}

static int cve_fill_lk_optical_flow_pyr_task(CVE_SRC_IMAGE_T astSrcPrevPyr[],
                                             CVE_SRC_IMAGE_T astSrcNextPyr[],
                                             CVE_SRC_MEM_INFO_T *pstPrevPts,
                                             CVE_MEM_INFO_T *pstNextPts, CVE_DST_MEM_INFO_T *pstErr,
                                             CVE_LK_OPTICAL_FLOW_PYR_CTRL_T *pstLkOptiFlowPyrCtrl,
                                             char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_lk_optical_flow_pyr_params_t lk_optical_flow_pyr_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;
    int i = 0;

    memset(&lk_optical_flow_pyr_params, 0, sizeof(cve_op_lk_optical_flow_pyr_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    for (i = 0; i <= 3; i++) {
        ret = fill_src_image(&init_params, &astSrcPrevPyr[i], &src_off);
        if (ret != 0) {
            return ret;
        }
    }
    for (i = 0; i <= 3; i++) {
        ret = fill_src_image(&init_params, &astSrcNextPyr[i], &src_off);
        if (ret != 0) {
            return ret;
        }
    }

    ret = fill_dst_mem(&init_params, pstPrevPts, sizeof(CVE_POINT_S12Q7_T) * CVE_ST_MAX_CORNER_NUM,
                       &dst_off);

    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstNextPts, sizeof(CVE_POINT_S12Q7_T) * CVE_ST_MAX_CORNER_NUM,
                       &dst_off);
    if (ret != 0) {
        return ret;
    }

    if ((pstLkOptiFlowPyrCtrl->enOutMode == CVE_LK_OPTICAL_FLOW_PYR_OUT_MODE_ERR) ||
        (pstLkOptiFlowPyrCtrl->enOutMode == CVE_LK_OPTICAL_FLOW_PYR_OUT_MODE_BOTH)) {
        ret = fill_dst_mem(&init_params, pstErr, CVE_ST_MAX_CORNER_NUM, &dst_off);
        if (ret != 0) {
            return ret;
        }
    }

    init_params.src_width = astSrcPrevPyr[0].u32Width;
    init_params.src_height = astSrcPrevPyr[0].u32Height;
    init_params.dst_width = astSrcPrevPyr[0].u32Width;
    init_params.dst_height = astSrcPrevPyr[0].u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    init_params.dst_stride[3] = init_params.dst_stride[2];
    cve_common_params_init(&lk_optical_flow_pyr_params.comm_params, &init_params);

    lk_optical_flow_pyr_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    lk_optical_flow_pyr_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
    lk_optical_flow_pyr_params.comm_params.reg_e8.bits.rdmif2_pack_mode = 0;
    lk_optical_flow_pyr_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    lk_optical_flow_pyr_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    lk_optical_flow_pyr_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;
    lk_optical_flow_pyr_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_LK_OPTIAL_FLOW_PRY;

    lk_optical_flow_pyr_params.lk_50.bits.lk_u10pstnum = pstLkOptiFlowPyrCtrl->u16PtsNum;
    lk_optical_flow_pyr_params.lk_50.bits.lk_buseinitflow = pstLkOptiFlowPyrCtrl->bUseInitFlow;
    lk_optical_flow_pyr_params.lk_50.bits.lk_mode = pstLkOptiFlowPyrCtrl->enOutMode;
    lk_optical_flow_pyr_params.lk_51.bits.u0q8eps = pstLkOptiFlowPyrCtrl->u0q8Eps;
    lk_optical_flow_pyr_params.lk_51.bits.lk_u8itercnt = pstLkOptiFlowPyrCtrl->u8IterCnt;
    lk_optical_flow_pyr_params.lk_51.bits.lk_u2maxlevel = pstLkOptiFlowPyrCtrl->u8MaxLevel;

    *cmd_line_num =
        lk_optical_flow_pyr_task_cmd_queue(&lk_optical_flow_pyr_params, (unsigned int *)cmd_buf);
    return 0;
}
static int cve_fill_ccl_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_DST_IMAGE_T *pstDstImage,
                             CVE_DST_MEM_INFO_T *pstBlob, CVE_CCL_CTRL_T *pstCclCtrl, char *cmd_buf,
                             unsigned int *cmd_line_num)
{
    cve_op_ccl_params_t ccl_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    int ret = 0;

    memset(&ccl_params, 0, sizeof(cve_op_ccl_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_image(&init_params, pstDstImage, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstBlob, sizeof(CVE_REGION_T) * CVE_MAX_REGION_NUM, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstDstImage, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstDstImage->u32Width;
    init_params.dst_height = pstDstImage->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&ccl_params.comm_params, &init_params);

    ccl_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    ccl_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    ccl_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;
    ccl_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
    ccl_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_CCL;
    ccl_params.comm_params.reg_02.bits.intput_mode = pstCclCtrl->enInputDataMode;

    *cmd_line_num = ccl_task_cmd_queue(&ccl_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_fill_gmm_task(CVE_SRC_IMAGE_T *pstSrcImage, CVE_SRC_IMAGE_T *pstFactor,
                             CVE_DST_IMAGE_T *pstFg, CVE_DST_IMAGE_T *pstBg,
                             CVE_MEM_INFO_T *pstModel, CVE_GMM_CTRL_T *pstGmmCtrl, char *cmd_buf,
                             unsigned int *cmd_line_num)
{
    cve_op_gmm_params_t gmm_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    unsigned int channel_num = 0;
    unsigned int model_len = 0;
    int ret = 0;

    if (pstSrcImage->enType == CVE_IMAGE_TYPE_U8C3_PACKAGE) {
        channel_num = 3;
    } else {
        channel_num = 1;
    }

    model_len = CVE_ALIGN_UP(
                    pstFg->u32Width * (8 + pstGmmCtrl->u8ModelNum * (32 + 16 * channel_num)), 128) /
                8;

    memset(&gmm_params, 0, sizeof(cve_op_ccl_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_image(&init_params, pstSrcImage, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_image(&init_params, pstFactor, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_mem(&init_params, pstModel, model_len, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstFg, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_image(&init_params, pstBg, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstModel, model_len, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcImage->u32Width;
    init_params.src_height = pstSrcImage->u32Height;
    init_params.dst_width = pstFg->u32Width;
    init_params.dst_height = pstFg->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    cve_common_params_init(&gmm_params.comm_params, &init_params);

    gmm_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    gmm_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    gmm_params.comm_params.reg_e8.bits.rdmif2_pack_mode = 0;
    gmm_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
    gmm_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_GMM;
    gmm_params.comm_params.reg_02.bits.output_mode = pstGmmCtrl->enOutputDataMode;

    gmm_params.gmm_44.bits.gmm_u0q16initweight = pstGmmCtrl->u0q16InitWeight;
    gmm_params.gmm_44.bits.gmm_u0q16learnrate = pstGmmCtrl->u0q16LearnRate;
    gmm_params.gmm_45.bits.gmm_u10q6sigma_init = pstGmmCtrl->u10q6NoiseVar;
    gmm_params.gmm_45.bits.gmm_ds_mode = pstGmmCtrl->enDownScaleMode;
    gmm_params.gmm_45.bits.gmm_output_bg_en = pstGmmCtrl->enOutputMode;
    gmm_params.gmm_46.bits.gmm_u10q6sigma_max = pstGmmCtrl->u10q6MaxVar;
    gmm_params.gmm_46.bits.gmm_u2nchannels = channel_num;

    gmm_params.gmm_46.bits.gmm_u3modelnum = pstGmmCtrl->u8ModelNum;

    gmm_params.gmm_47.bits.gmm_modellen_in128b =
        CVE_ALIGN_UP(pstFg->u32Width * (8 + pstGmmCtrl->u8ModelNum *
                                                (32 + 16 * gmm_params.gmm_46.bits.gmm_u2nchannels)),
                     128) /
        128;

    gmm_params.gmm_47.bits.gmm_u10q6sigma_min = pstGmmCtrl->u10q6MinVar;
    gmm_params.gmm_48.bits.gmm_u3q7sigma_scale = pstGmmCtrl->u3q7SigmaScale;
    gmm_params.gmm_48.bits.gmm_u0q16weight_sum_thr = pstGmmCtrl->u0q16WeightThr;

    gmm_params.gmm_49.bits.gmm_piclen_in128b = pstSrcImage->u32Width * channel_num / 16;
    gmm_params.gmm_49.bits.gmm_update_factor_mode_en = pstGmmCtrl->enDurationUpdateFactorMode;
    gmm_params.gmm_49.bits.gmm_acc_lr_en = pstGmmCtrl->enFastLearn;
    gmm_params.gmm_49.bits.gmm_sns_factor_mode_en = pstGmmCtrl->enSnsFactorMode;

    *cmd_line_num = gmm_task_cmd_queue(&gmm_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_check_op_proc_completed(const void *data)
{
    cve_task_info_t *info = &cve_context.task_info;
    unsigned long flags;
    CVE_HANDLE cveHandle;

    if (data == NULL) {
        panic("\nASSERT at:\n	>Function : %s\n  >Line No. : %d\n	>Condition: %s\n",
              __FUNCTION__, __LINE__, "data != NULL");
    }
    cveHandle = *(CVE_HANDLE *)data;

    spin_lock_irqsave(&cve_spinlock, flags);

    if ((cveHandle < info->cmd_finish_cnt &&
         info->cmd_finish_cnt - cveHandle <= cve_node_num * 2) ||
        (cveHandle > info->cmd_finish_cnt && cveHandle - info->cmd_finish_cnt > cve_node_num * 2)) {
        spin_unlock_irqrestore(&cve_spinlock, flags);
        return true;
    }
    spin_unlock_irqrestore(&cve_spinlock, flags);

    return false;
}

static int cve_fill_tof_task(CVE_SRC_RAW_T *pstSrcRaw, CVE_SRC_RAW_T *pstSrcFpn,
                             CVE_SRC_MEM_INFO_T *pstSrcCoef, CVE_DST_MEM_INFO_T *pstDstStatus,
                             CVE_DST_MEM_INFO_T *pstDstIR, CVE_DST_MEM_INFO_T *pstDstData,
                             CVE_TOF_CTRL_T *pstTofCtrl, char *cmd_buf, unsigned int *cmd_line_num)
{
    cve_op_tof_params_t tof_params;
    cve_comm_init_params_t init_params;
    unsigned int src_off = 0;
    unsigned int dst_off = 0;
    unsigned char spa_mask[15] = {0, 1, 2, 1, 0, 0, 2, 4, 2, 0, 0, 1, 2, 1, 0};
    int ret = 0;

    memset(&tof_params, 0, sizeof(cve_op_update_bg_model_params_t));
    memset(&init_params, 0, sizeof(cve_comm_init_params_t));

    ret = fill_src_raw(&init_params, pstSrcRaw, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_raw(&init_params, pstSrcFpn, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_src_mem(&init_params, pstSrcCoef, pstSrcRaw->u32Stride * 8, &src_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstDstStatus, pstSrcRaw->u32Stride * 2, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstDstIR, pstSrcRaw->u32Stride * 2, &dst_off);
    if (ret != 0) {
        return ret;
    }

    ret = fill_dst_mem(&init_params, pstDstData, pstSrcRaw->u32Stride * 2, &dst_off);
    if (ret != 0) {
        return ret;
    }

    init_params.src_width = pstSrcRaw->u32Width;
    init_params.src_height = pstSrcRaw->u32Height;
    init_params.dst_width = pstSrcRaw->u32Width;
    init_params.dst_height = pstSrcRaw->u32Height;
    init_params.xstart = 0;
    init_params.ystart = 0;
    init_params.xSize = init_params.src_width;
    init_params.ySize = init_params.src_height;
    init_params.src_stride[3] = init_params.src_stride[2];
    init_params.dst_stride[3] = init_params.dst_stride[2];
    cve_common_params_init(&tof_params.comm_params, &init_params);

    tof_params.comm_params.reg_c0.bits.wdmif_pack_mode = 0;
    tof_params.comm_params.reg_c8.bits.wdmif1_pack_mode = 0;
    tof_params.comm_params.reg_d0.bits.wdmif2_pack_mode = 0;
    tof_params.comm_params.reg_d8.bits.rdmif_pack_mode = 0;
    tof_params.comm_params.reg_e8.bits.rdmif2_pack_mode = 0;
    tof_params.comm_params.reg_e0.bits.rdmif1_pack_mode = 0;
    tof_params.comm_params.reg_02.bits.cve_op_type = CVE_OP_TYPE_TOF;

    tof_params.tof_83.bits.cve_tof_hist_x_start = pstTofCtrl->u16HistXstart;
    tof_params.tof_83.bits.cve_tof_hist_y_start = pstTofCtrl->u16HistYstart;
    tof_params.tof_84.bits.cve_tof_hist_x_end = pstTofCtrl->u16HistXend;
    tof_params.tof_84.bits.cve_tof_hist_y_end = pstTofCtrl->u16HistYend;

    tof_params.tof_85.bits.cve_tof_p_coef_0 = pstTofCtrl->as32PCoef[0];
    tof_params.tof_86.bits.cve_tof_p_coef_1 = pstTofCtrl->as32PCoef[1];
    tof_params.tof_87.bits.cve_tof_p_coef_2 = pstTofCtrl->as32PCoef[2];
    tof_params.tof_88.bits.cve_tof_p_coef_3 = pstTofCtrl->as32PCoef[3];
    tof_params.tof_89.bits.cve_tof_p_coef_4 = pstTofCtrl->as32PCoef[4];

    tof_params.tof_8a.bits.cve_tof_t_coef1_0 = pstTofCtrl->as16TCoef1[0];
    tof_params.tof_8a.bits.cve_tof_t_coef1_1 = pstTofCtrl->as16TCoef1[1];
    tof_params.tof_8b.bits.cve_tof_t_coef1_2 = pstTofCtrl->as16TCoef1[2];
    tof_params.tof_8b.bits.cve_tof_t_coef1_3 = pstTofCtrl->as16TCoef1[3];

    tof_params.tof_8c.bits.cve_tof_spa_mask_0 = spa_mask[0];
    tof_params.tof_8c.bits.cve_tof_spa_mask_1 = spa_mask[1];
    tof_params.tof_8c.bits.cve_tof_spa_mask_2 = spa_mask[2];
    tof_params.tof_8d.bits.cve_tof_spa_mask_3 = spa_mask[3];
    tof_params.tof_8d.bits.cve_tof_spa_mask_4 = spa_mask[4];
    tof_params.tof_8d.bits.cve_tof_spa_mask_5 = spa_mask[5];
    tof_params.tof_8e.bits.cve_tof_spa_mask_6 = spa_mask[6];
    tof_params.tof_8e.bits.cve_tof_spa_mask_7 = spa_mask[7];
    tof_params.tof_8e.bits.cve_tof_spa_mask_8 = spa_mask[8];
    tof_params.tof_8f.bits.cve_tof_spa_mask_9 = spa_mask[9];
    tof_params.tof_8f.bits.cve_tof_spa_mask_10 = spa_mask[10];
    tof_params.tof_8f.bits.cve_tof_spa_mask_11 = spa_mask[11];
    tof_params.tof_90.bits.cve_tof_spa_mask_12 = spa_mask[12];
    tof_params.tof_90.bits.cve_tof_spa_mask_13 = spa_mask[13];
    tof_params.tof_90.bits.cve_tof_spa_mask_14 = spa_mask[14];

    tof_params.tof_91.bits.cve_tof_raw_mode = pstTofCtrl->enRawMode;
    tof_params.tof_91.bits.cve_tof_bp_num = pstTofCtrl->u8BadPointNum;
    tof_params.tof_91.bits.cve_tof_spa_norm = pstTofCtrl->u8SpaNorm;
    tof_params.tof_91.bits.cve_tof_raw_shift_12bit_en = pstTofCtrl->bRawShift;
    tof_params.tof_92.bits.cve_tof_temperature_int = pstTofCtrl->s8IntTemp;
    tof_params.tof_92.bits.cve_tof_temperature_ext = pstTofCtrl->s8ExtTemp;
    tof_params.tof_93.bits.cve_tof_q1_high_thr = pstTofCtrl->u16Q1HighThr;
    tof_params.tof_93.bits.cve_tof_q23_high_thr = pstTofCtrl->u16Q23HighThr;
    tof_params.tof_94.bits.cve_tof_ir_high_thr = pstTofCtrl->u16IRHighThr;
    tof_params.tof_94.bits.cve_tof_ir_low_thr = pstTofCtrl->u16IRLowThr;
    tof_params.tof_95.bits.cve_tof_dis_max = pstTofCtrl->u16DepthMax;
    tof_params.tof_95.bits.cve_tof_dis_min = pstTofCtrl->u16DepthMin;
    tof_params.tof_96.bits.cve_tof_bypass_en = pstTofCtrl->bBypass;
    tof_params.tof_96.bits.cve_tof_fpn_cali_mode = pstTofCtrl->enFpnMode;
    tof_params.tof_96.bits.cve_tof_spa1_en = pstTofCtrl->bSpa1En;
    tof_params.tof_96.bits.cve_tof_spa2_en = pstTofCtrl->bSpa2En;

    *cmd_line_num = tof_task_cmd_queue(&tof_params, (unsigned int *)cmd_buf);

    return 0;
}

static int cve_query_task(CVE_HANDLE cveHandle, AML_BOOL_E bBlock, AML_BOOL_E *pbFinish)
{
    int ret;

    if (bBlock != AML_TRUE && bBlock != AML_FALSE) {
        CVE_ERR_TRACE("bBlock (%d) must be AML_TRUE or AML_FALSE!\n", bBlock);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }
    if (cveHandle >= CMD_HANDLE_MAX) {
        CVE_ERR_TRACE("Error,CveHandle(%d) must be (%d,%d)\n", cveHandle, AML_INVALID_HANDLE,
                      CMD_HANDLE_MAX);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }

    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    if (cve_check_op_proc_completed(&cveHandle)) {
        goto check_cve_timeout;
    }

    if (!bBlock) {
        *pbFinish = AML_FALSE;
        up(&cve_context.cve_sema);
        return 0;
    }
    ret = wait_event_interruptible_timeout(
        cve_context.cve_wait, cve_check_op_proc_completed(&cveHandle), CVE_QUERY_TIMEOUT);
    if (ret < 0 && ret != -ERESTARTSYS) {
        CVE_ERR_TRACE("CVE query parameter invalid!\n");
        ret = AML_ERR_CVE_ILLEGAL_PARAM;
        goto query_error;
    } else if (ret == -ERESTARTSYS) {
        CVE_ERR_TRACE("CVE query failed!\n");
        goto query_error;
    } else if (!ret) {
        CVE_ERR_TRACE("CVE query time out!\n");
        cve_context.run_time_info.query_timeout_cnt++;
        ret = AML_ERR_CVE_QUERY_TIMEOUT;
        goto query_error;
    }

    if (cve_state) {
        CVE_ERR_TRACE("CVE is unexist!\n");
        ret = AML_ERR_CVE_UNEXIST;
        goto query_error;
    }

check_cve_timeout:
    if (cve_timeout_flag) {
        cve_timeout_flag = false;
        CVE_ERR_TRACE("CVE process timeout!\n");
        ret = AML_ERR_CVE_SYS_TIMEOUT;
        goto query_error;
    }

    *pbFinish = AML_TRUE;
    up(&cve_context.cve_sema);

    return 0;

query_error:
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_dma(CVE_OP_DMA_T *pstOpDMA)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }
    if (pstOpDMA == NULL) {
        CVE_ERR_TRACE("dma arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }

    set_data_phy_addr(&pstOpDMA->stSrcDATA);
    set_data_phy_addr(&pstOpDMA->stDstDATA);
    ret = cve_check_dma_param(&pstOpDMA->stSrcDATA, &pstOpDMA->stDstDATA, &pstOpDMA->stDmaCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check DMA parameters failed!\n", ret);
        return ret;
    }

    cve_fill_dma_task(&pstOpDMA->stSrcDATA, &pstOpDMA->stDstDATA, &pstOpDMA->stDmaCtrl, cmd_buf,
                      &cmd_line_num);

    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }
    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpDMA->cveHandle, pstOpDMA->bInstant, NULL);

    cve_context.invoke_count.dma++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_luma_stat(CVE_OP_LUAM_STAT_ARRAY_T *pstOpLuamStat)
{
    unsigned int cmd_line_num_total = 0;
    unsigned long flags;
    cve_cmd_desc_t *cmd_descs;
    cve_cmd_desc_t *cmd_desc_tmp;
    int ret = 0;
    int i = 0;

    if (pstOpLuamStat == NULL) {
        CVE_ERR_TRACE("luamStat arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpLuamStat->stSrcImage);
    set_mem_phy_addr(&pstOpLuamStat->stDstMem);
    ret = cve_check_luamStat_param(&pstOpLuamStat->stSrcImage, &pstOpLuamStat->stDstMem,
                                   pstOpLuamStat->astCveLumaRect,
                                   &pstOpLuamStat->stLumaStatArrayCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check luamStat parameters failed!\n", ret);
        return ret;
    }

    for (i = 0; i < pstOpLuamStat->stLumaStatArrayCtrl.u8MaxLumaRect; i++) {
        multi_cmd_buf[i] = cmd_buf_get();
        if (multi_cmd_buf[i] == NULL) {
            return AML_ERR_CVE_NOBUF;
        }
        cve_fill_luma_stat_task(
            &pstOpLuamStat->stSrcImage, &pstOpLuamStat->stDstMem, &pstOpLuamStat->astCveLumaRect[i],
            &pstOpLuamStat->stLumaStatArrayCtrl, i, multi_cmd_buf[i], &multi_cmd_line_num[i]);
        cmd_line_num_total += multi_cmd_line_num[i];
    }

    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    spin_lock_irqsave(&cve_spinlock, flags);
    ret = request_op_cmd(&cmd_descs, pstOpLuamStat->stLumaStatArrayCtrl.u8MaxLumaRect);
    if (ret) {
        spin_unlock_irqrestore(&cve_spinlock, flags);
        up(&cve_context.cve_sema);
        return ret;
    }

    cmd_descs->instant = pstOpLuamStat->bInstant;
    cmd_descs->cmd_line_num = cmd_line_num_total;
    INIT_LIST_HEAD(&cmd_descs->list);

    cmd_descs->io_info.op_type = CVE_OP_TYPE_DMA;
    cmd_descs->io_info.inp_flags = 0;
    cmd_descs->io_info.inp_phys_addr = 0;
    cmd_descs->io_info.outp_flags = 0;
    cmd_descs->io_info.outp_phys_addr = 0;

    ret = cve_create_task_multi_cmd(cmd_descs, multi_cmd_buf, multi_cmd_line_num,
                                    pstOpLuamStat->stLumaStatArrayCtrl.u8MaxLumaRect);
    if (ret) {
        CVE_ERR_TRACE("creat task failed!\n");
        spin_unlock_irqrestore(&cve_spinlock, flags);
        up(&cve_context.cve_sema);
        return ret;
    }
    cve_manage_handle(&pstOpLuamStat->cveHandle, pstOpLuamStat->stLumaStatArrayCtrl.u8MaxLumaRect);
    cmd_descs->cveHandle = pstOpLuamStat->cveHandle;
    if (cve_context.queue_busy == CVE_STATUS_IDLE) {
        cve_context.queue_busy = cve_context.queue_wait;
        cve_context.queue_wait = CVE_STATUS_CQ1 - cve_context.queue_wait;
        if (cmd_descs->task_desc->bInput) {
            list_for_each_entry(cmd_desc_tmp, &cmd_descs->task_desc->cmd_list, list)
            {
                if (cmd_desc_tmp->io_info.inp_flags != 0 &&
                    cmd_desc_tmp->io_info.inp_phys_addr != 0) {
                    spin_unlock_irqrestore(&cve_spinlock, flags);
                    cve_input_process(&cmd_desc_tmp->io_info);
                    spin_lock_irqsave(&cve_spinlock, flags);
                }
            }
        }
        cve_start_task(cmd_descs->task_desc);
    }
    spin_unlock_irqrestore(&cve_spinlock, flags);

    cve_context.invoke_count.luma_stat++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_and(CVE_OP_AND_T *pstOpAnd)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpAnd == NULL) {
        CVE_ERR_TRACE("and arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpAnd->stSrcImage1);
    set_image_phy_addr(&pstOpAnd->stSrcImage2);
    set_image_phy_addr(&pstOpAnd->stDst);
    ret = cve_check_and_param(&pstOpAnd->stSrcImage1, &pstOpAnd->stSrcImage2, &pstOpAnd->stDst);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check AND parameters failed!\n", ret);
        return ret;
    }

    cve_fill_alu_task(&pstOpAnd->stSrcImage1, &pstOpAnd->stSrcImage2, &pstOpAnd->stDst,
                      CVE_ALU_SEL_AND, NULL, NULL, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpAnd->cveHandle, pstOpAnd->bInstant, NULL);

    cve_context.invoke_count.and ++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_or(CVE_OP_OR_T *pstOpOr)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpOr == NULL) {
        CVE_ERR_TRACE("or arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpOr->stSrcImage1);
    set_image_phy_addr(&pstOpOr->stSrcImage2);
    set_image_phy_addr(&pstOpOr->stDst);
    ret = cve_check_or_param(&pstOpOr->stSrcImage1, &pstOpOr->stSrcImage2, &pstOpOr->stDst);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check OR parameters failed!\n", ret);
        return ret;
    }

    cve_fill_alu_task(&pstOpOr->stSrcImage1, &pstOpOr->stSrcImage2, &pstOpOr->stDst, CVE_ALU_SEL_OR,
                      NULL, NULL, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpOr->cveHandle, pstOpOr->bInstant, NULL);

    cve_context.invoke_count.or ++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_xor(CVE_OP_XOR_T *pstOpXor)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpXor == NULL) {
        CVE_ERR_TRACE("xor arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpXor->stSrcImage1);
    set_image_phy_addr(&pstOpXor->stSrcImage2);
    set_image_phy_addr(&pstOpXor->stDst);

    ret = cve_check_xor_param(&pstOpXor->stSrcImage1, &pstOpXor->stSrcImage2, &pstOpXor->stDst);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check XOR parameters failed!\n", ret);
        return ret;
    }

    cve_fill_alu_task(&pstOpXor->stSrcImage1, &pstOpXor->stSrcImage2, &pstOpXor->stDst,
                      CVE_ALU_SEL_XOR, NULL, NULL, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpXor->cveHandle, pstOpXor->bInstant, NULL);

    cve_context.invoke_count.xor ++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_sub(CVE_OP_SUB_T *pstOpSub)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }
    if (pstOpSub == NULL) {
        CVE_ERR_TRACE("sub arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpSub->stSrcImage1);
    set_image_phy_addr(&pstOpSub->stSrcImage2);
    set_image_phy_addr(&pstOpSub->stDst);
    ret = cve_check_sub_param(&pstOpSub->stSrcImage1, &pstOpSub->stSrcImage2, &pstOpSub->stDst,
                              &pstOpSub->stSubCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check SUB parameters failed!\n", ret);
        return ret;
    }

    cve_fill_alu_task(&pstOpSub->stSrcImage1, &pstOpSub->stSrcImage2, &pstOpSub->stDst,
                      CVE_ALU_SEL_SUB, &pstOpSub->stSubCtrl, NULL, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpSub->cveHandle, pstOpSub->bInstant, NULL);

    cve_context.invoke_count.sub++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_add(CVE_OP_ADD_T *pstOpAdd)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpAdd == NULL) {
        CVE_ERR_TRACE("add arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpAdd->stSrcImage1);
    set_image_phy_addr(&pstOpAdd->stSrcImage2);
    set_image_phy_addr(&pstOpAdd->stDst);
    ret = cve_check_add_param(&pstOpAdd->stSrcImage1, &pstOpAdd->stSrcImage2, &pstOpAdd->stDst,
                              &pstOpAdd->stAddCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check ADD parameters failed!\n", ret);
        return ret;
    }

    cve_fill_alu_task(&pstOpAdd->stSrcImage1, &pstOpAdd->stSrcImage2, &pstOpAdd->stDst,
                      CVE_ALU_SEL_ADD, NULL, &pstOpAdd->stAddCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpAdd->cveHandle, pstOpAdd->bInstant, NULL);

    cve_context.invoke_count.add++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_equalize_hist(CVE_OP_EQUALIZE_HIST_T *pstOpEqualizeHist)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpEqualizeHist == NULL) {
        CVE_ERR_TRACE("equalize hist arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpEqualizeHist->stSrcImage);
    set_image_phy_addr(&pstOpEqualizeHist->stDstImage);
    set_mem_phy_addr(&pstOpEqualizeHist->stEqualizeHistCtrl.stMem);
    ret = cve_check_equalize_hist_param(&pstOpEqualizeHist->stSrcImage,
                                        &pstOpEqualizeHist->stDstImage,
                                        &pstOpEqualizeHist->stEqualizeHistCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check equalize_hist parameters failed!\n", ret);
        return ret;
    }

    cve_fill_equalize_hist_task(&pstOpEqualizeHist->stSrcImage, &pstOpEqualizeHist->stDstImage,
                                &pstOpEqualizeHist->stEqualizeHistCtrl, cmd_buf, &cmd_line_num);

    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_EQUALIZE_HIST;
    io_info.inp_flags = EQUALIZE_HIST_INPUT_PROCESS_LUT;
    io_info.inp_size = pstOpEqualizeHist->stEqualizeHistCtrl.stMem.u32Size;
    io_info.inp_phys_addr = pstOpEqualizeHist->stEqualizeHistCtrl.stMem.u64PhyAddr;

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpEqualizeHist->cveHandle,
                           pstOpEqualizeHist->bInstant, &io_info);

    cve_context.invoke_count.equalize_hist++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_filter(CVE_OP_FILTER_T *pstOpFilter)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpFilter == NULL) {
        CVE_ERR_TRACE("filter arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpFilter->stSrcImage);
    set_image_phy_addr(&pstOpFilter->stDstImage);
    ret = cve_check_filter_param(&pstOpFilter->stSrcImage, &pstOpFilter->stDstImage,
                                 &pstOpFilter->stFilterCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check Filter parameters failed!\n", ret);
        return ret;
    }

    cve_fill_filter_task(&pstOpFilter->stSrcImage, &pstOpFilter->stDstImage,
                         &pstOpFilter->stFilterCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.filter++;
    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpFilter->cveHandle, pstOpFilter->bInstant,
                           NULL);
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_csc(CVE_OP_CSC_T *pstOpCsc)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpCsc == NULL) {
        CVE_ERR_TRACE("csc arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpCsc->stSrcImage);
    set_image_phy_addr(&pstOpCsc->stDstImage);
    ret = cve_check_csc_param(&pstOpCsc->stSrcImage, &pstOpCsc->stDstImage, &pstOpCsc->stCscCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check csc parameters failed!\n", ret);
        return ret;
    }

    cve_fill_csc_task(&pstOpCsc->stSrcImage, &pstOpCsc->stDstImage, &pstOpCsc->stCscCtrl, cmd_buf,
                      &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.csc++;
    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpCsc->cveHandle, pstOpCsc->bInstant, NULL);
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_filter_and_csc(CVE_OP_FILTER_AND_CSC_T *pstOpFilterCsc)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpFilterCsc == NULL) {
        CVE_ERR_TRACE("filter_csc arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpFilterCsc->stSrcImage);
    set_image_phy_addr(&pstOpFilterCsc->stDstImage);
    ret = cve_check_filter_csc_param(&pstOpFilterCsc->stSrcImage, &pstOpFilterCsc->stDstImage,
                                     &pstOpFilterCsc->stFilterCscCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check filter_csc parameters failed!\n", ret);
        return ret;
    }

    cve_fill_filter_and_csc_task(&pstOpFilterCsc->stSrcImage, &pstOpFilterCsc->stDstImage,
                                 &pstOpFilterCsc->stFilterCscCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.filter_and_csc++;
    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpFilterCsc->cveHandle,
                           pstOpFilterCsc->bInstant, NULL);
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_sobel(CVE_OP_SOBEL_T *pstOpSobel)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpSobel == NULL) {
        CVE_ERR_TRACE("sobel arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpSobel->stSrcImage);
    set_image_phy_addr(&pstOpSobel->stDstH);
    set_image_phy_addr(&pstOpSobel->stDstV);
    ret = cve_check_sobel_param(&pstOpSobel->stSrcImage, &pstOpSobel->stDstH, &pstOpSobel->stDstV,
                                &pstOpSobel->stSobelCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check sobel parameters failed!\n", ret);
        return ret;
    }

    cve_fill_sobel_task(&pstOpSobel->stSrcImage, &pstOpSobel->stDstH, &pstOpSobel->stDstV,
                        &pstOpSobel->stSobelCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.sobel++;
    ret =
        cve_post_process(cmd_buf, cmd_line_num, &pstOpSobel->cveHandle, pstOpSobel->bInstant, NULL);
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_dilate(CVE_OP_DILATE_T *stOpDilate)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (stOpDilate == NULL) {
        CVE_ERR_TRACE("dilate arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }

    set_image_phy_addr(&stOpDilate->stSrcImage);
    set_image_phy_addr(&stOpDilate->stDstImage);
    ret = cve_check_dilate_param(&stOpDilate->stSrcImage, &stOpDilate->stDstImage,
                                 &stOpDilate->stDilateCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check Dilate parameters failed!\n", ret);
        return ret;
    }

    cve_fill_dilate_task(&stOpDilate->stSrcImage, &stOpDilate->stDstImage,
                         &stOpDilate->stDilateCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.dilate++;
    ret =
        cve_post_process(cmd_buf, cmd_line_num, &stOpDilate->cveHandle, stOpDilate->bInstant, NULL);
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_erode(CVE_OP_ERODE_T *stOpErode)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (stOpErode == NULL) {
        CVE_ERR_TRACE("erode arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&stOpErode->stSrcImage);
    set_image_phy_addr(&stOpErode->stDstImage);
    ret = cve_check_erode_param(&stOpErode->stSrcImage, &stOpErode->stDstImage,
                                &stOpErode->stErodeCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check erode parameters failed!\n", ret);
        return ret;
    }

    cve_fill_erode_task(&stOpErode->stSrcImage, &stOpErode->stDstImage, &stOpErode->stErodeCtrl,
                        cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.erode++;
    ret = cve_post_process(cmd_buf, cmd_line_num, &stOpErode->cveHandle, stOpErode->bInstant, NULL);
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_thresh(CVE_OP_THRESH_T *pstOpThresh)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpThresh == NULL) {
        CVE_ERR_TRACE("thresh arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpThresh->stSrcImage);
    set_image_phy_addr(&pstOpThresh->stDstImage);
    ret = cve_check_thresh(&pstOpThresh->stSrcImage, &pstOpThresh->stDstImage,
                           &pstOpThresh->stThreshCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check thresh parameters failed!\n", ret);
        return ret;
    }

    cve_fill_thresh_task(&pstOpThresh->stSrcImage, &pstOpThresh->stDstImage,
                         &pstOpThresh->stThreshCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.thresh++;
    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpThresh->cveHandle, pstOpThresh->bInstant,
                           NULL);
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_query(CVE_OP_QUERY_T *pstOpQuery)
{
    int ret = 0;

    if (pstOpQuery == NULL) {
        CVE_ERR_TRACE("query arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }

    ret = cve_query_task(pstOpQuery->cveHandle, pstOpQuery->bBlock, &pstOpQuery->bFinish);

    return ret;
}

static int cve_integ(CVE_OP_INTEG_T *pstOpInteg)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpInteg == NULL) {
        CVE_ERR_TRACE("integ arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpInteg->stSrcImage);
    set_image_phy_addr(&pstOpInteg->stDstImage);
    ret = cve_check_integ_param(&pstOpInteg->stSrcImage, &pstOpInteg->stDstImage,
                                &pstOpInteg->stIntegCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check integ parameters failed!\n", ret);
        return ret;
    }

    cve_fill_integ_task(&pstOpInteg->stSrcImage, &pstOpInteg->stDstImage, &pstOpInteg->stIntegCtrl,
                        cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    cve_context.invoke_count.integ++;
    ret =
        cve_post_process(cmd_buf, cmd_line_num, &pstOpInteg->cveHandle, pstOpInteg->bInstant, NULL);
    up(&cve_context.cve_sema);

    return ret;
}
static int cve_hist(CVE_OP_HIST_T *pstOpHist)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpHist == NULL) {
        CVE_ERR_TRACE("hist arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpHist->stSrcImage);
    set_mem_phy_addr(&pstOpHist->stDstMem);
    ret = cve_check_hist_param(&pstOpHist->stSrcImage, &pstOpHist->stDstMem);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check hist parameters failed!\n", ret);
        return ret;
    }

    cve_fill_hist_task(&pstOpHist->stSrcImage, &pstOpHist->stDstMem, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_HIST;
    io_info.outp_flags = HIST_OUTPUT_PROCESS_LUT;
    io_info.outp_size = pstOpHist->stDstMem.u32Size;
    io_info.outp_phys_addr = pstOpHist->stDstMem.u64PhyAddr;

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpHist->cveHandle, pstOpHist->bInstant,
                           &io_info);

    cve_context.invoke_count.hist++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_ncc(CVE_OP_NCC_T *pstOpNcc)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpNcc == NULL) {
        CVE_ERR_TRACE("ncc arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpNcc->stSrcImage1);
    set_image_phy_addr(&pstOpNcc->stSrcImage2);
    set_mem_phy_addr(&pstOpNcc->stDstmem);
    ret = cve_check_ncc_param(&pstOpNcc->stSrcImage1, &pstOpNcc->stSrcImage2, &pstOpNcc->stDstmem,
                              &pstOpNcc->stNccCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check ncc parameters failed!\n", ret);
        return ret;
    }

    cve_fill_ncc_task(&pstOpNcc->stSrcImage1, &pstOpNcc->stSrcImage2, &pstOpNcc->stNccCtrl, cmd_buf,
                      &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_NCC;
    io_info.outp_flags = NCC_OUTPUT_PROCESS_REG;
    io_info.outp_size = pstOpNcc->stDstmem.u32Size;
    io_info.outp_phys_addr = pstOpNcc->stDstmem.u64PhyAddr;

    ret =
        cve_post_process(cmd_buf, cmd_line_num, &pstOpNcc->cveHandle, pstOpNcc->bInstant, &io_info);

    cve_context.invoke_count.ncc++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_norm_grad(CVE_OP_NORM_GRAD_T *pstOpNormGrad)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpNormGrad == NULL) {
        CVE_ERR_TRACE("norm grad arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpNormGrad->stSrcImage);
    set_image_phy_addr(&pstOpNormGrad->stDstH);
    set_image_phy_addr(&pstOpNormGrad->stDstV);
    set_image_phy_addr(&pstOpNormGrad->stDstHV);
    ret = cve_check_norm_grad_param(&pstOpNormGrad->stSrcImage, &pstOpNormGrad->stDstH,
                                    &pstOpNormGrad->stDstV, &pstOpNormGrad->stDstHV,
                                    &pstOpNormGrad->stNormGradCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check norm_grad parameters failed!\n", ret);
        return ret;
    }

    cve_fill_norm_grad_task(&pstOpNormGrad->stSrcImage, &pstOpNormGrad->stDstH,
                            &pstOpNormGrad->stDstV, &pstOpNormGrad->stDstHV,
                            &pstOpNormGrad->stNormGradCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpNormGrad->cveHandle,
                           pstOpNormGrad->bInstant, NULL);
    cve_context.invoke_count.nrom_grad++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_lbp(CVE_OP_LBP_T *pstOpLbp)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpLbp == NULL) {
        CVE_ERR_TRACE("lbp arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpLbp->stSrcImage);
    set_image_phy_addr(&pstOpLbp->stDstImage);
    ret = cve_check_lbp_param(&pstOpLbp->stSrcImage, &pstOpLbp->stDstImage, &pstOpLbp->stLbpCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check lbp parameters failed!\n", ret);
        return ret;
    }

    cve_fill_lbp_task(&pstOpLbp->stSrcImage, &pstOpLbp->stDstImage, &pstOpLbp->stLbpCtrl, cmd_buf,
                      &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpLbp->cveHandle, pstOpLbp->bInstant, NULL);

    cve_context.invoke_count.lbp++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_ccl(CVE_OP_CCL_T *pstOpCcl)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpCcl == NULL) {
        CVE_ERR_TRACE("ccl arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpCcl->stSrcImage);
    set_image_phy_addr(&pstOpCcl->stDstImage);
    set_mem_phy_addr(&pstOpCcl->stBlob);
    ret = cve_check_ccl_param(&pstOpCcl->stSrcImage, &pstOpCcl->stDstImage, &pstOpCcl->stBlob,
                              &pstOpCcl->stCclCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check ccl parameters failed!\n", ret);
        return ret;
    }

    cve_fill_ccl_task(&pstOpCcl->stSrcImage, &pstOpCcl->stDstImage, &pstOpCcl->stBlob,
                      &pstOpCcl->stCclCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_CCL;
    io_info.outp_flags = CCL_OUTPUT_PROCESS_REG;
    io_info.outp_size = pstOpCcl->stBlob.u32Size;
    io_info.outp_phys_addr = pstOpCcl->stBlob.u64PhyAddr;

    ret =
        cve_post_process(cmd_buf, cmd_line_num, &pstOpCcl->cveHandle, pstOpCcl->bInstant, &io_info);

    cve_context.invoke_count.ccl++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_gmm(CVE_OP_GMM_T *pstOpGmm)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpGmm == NULL) {
        CVE_ERR_TRACE("ccl arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpGmm->stSrcImage);
    set_image_phy_addr(&pstOpGmm->stFactor);
    set_image_phy_addr(&pstOpGmm->stFg);
    set_image_phy_addr(&pstOpGmm->stBg);
    set_mem_phy_addr(&pstOpGmm->stModel);
    ret = cve_check_gmm_param(&pstOpGmm->stSrcImage, &pstOpGmm->stFactor, &pstOpGmm->stFg,
                              &pstOpGmm->stBg, &pstOpGmm->stModel, &pstOpGmm->stGmmCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check gmm parameters failed!\n", ret);
        return ret;
    }

    cve_fill_gmm_task(&pstOpGmm->stSrcImage, &pstOpGmm->stFactor, &pstOpGmm->stFg, &pstOpGmm->stBg,
                      &pstOpGmm->stModel, &pstOpGmm->stGmmCtrl, cmd_buf, &cmd_line_num);

    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpGmm->cveHandle, pstOpGmm->bInstant, NULL);

    cve_context.invoke_count.gmm++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_map(CVE_OP_MAP_T *pstOpMap)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpMap == NULL) {
        CVE_ERR_TRACE("map arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpMap->stSrcImage);
    set_mem_phy_addr(&pstOpMap->stMap);
    set_image_phy_addr(&pstOpMap->stDstImage);
    ret = cve_check_map_param(&pstOpMap->stSrcImage, &pstOpMap->stMap, &pstOpMap->stDstImage,
                              &pstOpMap->stMapCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check map parameters failed!\n", ret);
        return ret;
    }

    cve_fill_map_task(&pstOpMap->stSrcImage, &pstOpMap->stMap, &pstOpMap->stDstImage,
                      &pstOpMap->stMapCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_MAP;
    io_info.op_mode = pstOpMap->stMapCtrl.enMode;
    io_info.inp_flags = MAP_INPUT_PROCESS_LUT;
    io_info.inp_size = pstOpMap->stMap.u32Size;
    io_info.inp_phys_addr = pstOpMap->stMap.u64PhyAddr;

    ret =
        cve_post_process(cmd_buf, cmd_line_num, &pstOpMap->cveHandle, pstOpMap->bInstant, &io_info);

    cve_context.invoke_count.map++;
    up(&cve_context.cve_sema);

    return ret;
}
static int cve_thresh_s16(CVE_OP_THRESH_S16_T *pstOpThreshS16)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpThreshS16 == NULL) {
        CVE_ERR_TRACE("thresh_s16 arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpThreshS16->stSrcImage);
    set_image_phy_addr(&pstOpThreshS16->stDstImage);
    ret = cve_check_thresh_s16_param(&pstOpThreshS16->stSrcImage, &pstOpThreshS16->stDstImage,
                                     &pstOpThreshS16->stThreshS16Ctrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check thresh_s16 parameters failed!\n", ret);
        return ret;
    }

    cve_fill_thresh_s16_task(&pstOpThreshS16->stSrcImage, &pstOpThreshS16->stDstImage,
                             &pstOpThreshS16->stThreshS16Ctrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpThreshS16->cveHandle,
                           pstOpThreshS16->bInstant, NULL);

    cve_context.invoke_count.thresh_s16++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_thresh_u16(CVE_OP_THRESH_U16_T *pstOpThreshU16)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpThreshU16 == NULL) {
        CVE_ERR_TRACE("thresh_u16 arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpThreshU16->stSrcImage);
    set_image_phy_addr(&pstOpThreshU16->stDstImage);
    ret = cve_check_thresh_u16_param(&pstOpThreshU16->stSrcImage, &pstOpThreshU16->stDstImage,
                                     &pstOpThreshU16->stThreshU16Ctrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check thresh_u16 parameters failed!\n", ret);
        return ret;
    }

    cve_fill_thresh_u16_task(&pstOpThreshU16->stSrcImage, &pstOpThreshU16->stDstImage,
                             &pstOpThreshU16->stThreshU16Ctrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpThreshU16->cveHandle,
                           pstOpThreshU16->bInstant, NULL);

    cve_context.invoke_count.thresh_u16++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_ord_stat_filter(CVE_OP_ORD_STAT_FILTER_T *pstOpOrdStatFilter)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpOrdStatFilter == NULL) {
        CVE_ERR_TRACE("ord_stat_filter arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpOrdStatFilter->stSrcImage);
    set_image_phy_addr(&pstOpOrdStatFilter->stDstImage);
    ret = cve_check_ord_stat_filter_param(&pstOpOrdStatFilter->stSrcImage,
                                          &pstOpOrdStatFilter->stDstImage,
                                          &pstOpOrdStatFilter->stOrdStatFltCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check ord_stat_filter parameters failed!\n", ret);
        return ret;
    }

    cve_fill_ord_stat_filter_task(&pstOpOrdStatFilter->stSrcImage, &pstOpOrdStatFilter->stDstImage,
                                  &pstOpOrdStatFilter->stOrdStatFltCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpOrdStatFilter->cveHandle,
                           pstOpOrdStatFilter->bInstant, NULL);

    cve_context.invoke_count.ord_stat_filter++;
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_16_to_8(CVE_OP_16BIT_TO_8BIT_T *pstOp16bitTo8bit)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOp16bitTo8bit == NULL) {
        CVE_ERR_TRACE("16bit_to_8bit arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOp16bitTo8bit->stSrcImage);
    set_image_phy_addr(&pstOp16bitTo8bit->stDstImage);
    ret =
        cve_check_16bit_to_8bit_param(&pstOp16bitTo8bit->stSrcImage, &pstOp16bitTo8bit->stDstImage,
                                      &pstOp16bitTo8bit->st16BitTo8BitCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check 16bit_to_8bit parameters failed!\n", ret);
        return ret;
    }

    cve_fill_16bit_to_8bit_task(&pstOp16bitTo8bit->stSrcImage, &pstOp16bitTo8bit->stDstImage,
                                &pstOp16bitTo8bit->st16BitTo8BitCtrl, cmd_buf, &cmd_line_num);

    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOp16bitTo8bit->cveHandle,
                           pstOp16bitTo8bit->bInstant, NULL);

    cve_context.invoke_count._16bit_to_8bit++;
    up(&cve_context.cve_sema);

    return ret;
}
static int cve_sad(CVE_OP_SAD_T *stOpSAD)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (stOpSAD == NULL) {
        CVE_ERR_TRACE("sad arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&stOpSAD->stSrcImage1);
    set_image_phy_addr(&stOpSAD->stSrcImage2);
    set_image_phy_addr(&stOpSAD->stSad);
    set_image_phy_addr(&stOpSAD->stThr);
    ret = cve_check_sad_param(&stOpSAD->stSrcImage1, &stOpSAD->stSrcImage2, &stOpSAD->stSad,
                              &stOpSAD->stThr, &stOpSAD->stSadCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check sad parameters failed!\n", ret);
        return ret;
    }

    cve_fill_sad_task(&stOpSAD->stSrcImage1, &stOpSAD->stSrcImage2, &stOpSAD->stSad,
                      &stOpSAD->stThr, &stOpSAD->stSadCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &stOpSAD->cveHandle, stOpSAD->bInstant, NULL);

    cve_context.invoke_count.sad++;
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_grad_fg(CVE_OP_GRAD_FG_T *stOpGradFg)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (stOpGradFg == NULL) {
        CVE_ERR_TRACE("gradFg arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&stOpGradFg->stFg);
    set_image_phy_addr(&stOpGradFg->stCurGrad);
    set_image_phy_addr(&stOpGradFg->stBgGrad);
    set_image_phy_addr(&stOpGradFg->stGradFg);
    ret = cve_check_grad_fg_param(&stOpGradFg->stFg, &stOpGradFg->stCurGrad, &stOpGradFg->stBgGrad,
                                  &stOpGradFg->stGradFg, &stOpGradFg->stGradFgCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check grad_fg parameters failed!\n", ret);
        return ret;
    }

    cve_fill_grad_fg_task(&stOpGradFg->stFg, &stOpGradFg->stCurGrad, &stOpGradFg->stBgGrad,
                          &stOpGradFg->stGradFg, &stOpGradFg->stGradFgCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret =
        cve_post_process(cmd_buf, cmd_line_num, &stOpGradFg->cveHandle, stOpGradFg->bInstant, NULL);

    cve_context.invoke_count.grad_fg++;
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_mag_and_ang(CVE_OP_MAG_AND_ANG_T *pstOpMagAndAng)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpMagAndAng == NULL) {
        CVE_ERR_TRACE("mag_and_ang arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpMagAndAng->stSrcImage);
    set_image_phy_addr(&pstOpMagAndAng->stDstMag);
    set_image_phy_addr(&pstOpMagAndAng->stDstAng);
    ret = cve_check_mag_and_ang_param(&pstOpMagAndAng->stSrcImage, &pstOpMagAndAng->stDstMag,
                                      &pstOpMagAndAng->stDstAng, &pstOpMagAndAng->stMagAndAngCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check mag_and_ang parameters failed!\n", ret);
        return ret;
    }

    cve_fill_mag_and_ang_task(&pstOpMagAndAng->stSrcImage, &pstOpMagAndAng->stDstMag,
                              &pstOpMagAndAng->stDstAng, &pstOpMagAndAng->stMagAndAngCtrl, cmd_buf,
                              &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpMagAndAng->cveHandle,
                           pstOpMagAndAng->bInstant, NULL);

    cve_context.invoke_count.mag_and_ang++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_match_bg_model(CVE_OP_MATCH_BG_MODEL_T *pstOpMatchBgModel)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpMatchBgModel == NULL) {
        CVE_ERR_TRACE("match_bg_model arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpMatchBgModel->stCurImg);
    set_image_phy_addr(&pstOpMatchBgModel->stPreImg);
    set_mem_phy_addr(&pstOpMatchBgModel->stBgModel);
    set_image_phy_addr(&pstOpMatchBgModel->stFg);
    set_image_phy_addr(&pstOpMatchBgModel->stBg);
    set_image_phy_addr(&pstOpMatchBgModel->stCurDiffBg);
    set_image_phy_addr(&pstOpMatchBgModel->stFrmDiff);
    set_mem_phy_addr(&pstOpMatchBgModel->stStatData);
    ret = cve_check_match_bg_model_param(
        &pstOpMatchBgModel->stCurImg, &pstOpMatchBgModel->stPreImg, &pstOpMatchBgModel->stBgModel,
        &pstOpMatchBgModel->stFg, &pstOpMatchBgModel->stBg, &pstOpMatchBgModel->stCurDiffBg,
        &pstOpMatchBgModel->stFrmDiff, &pstOpMatchBgModel->stStatData,
        &pstOpMatchBgModel->stMatchBgModelCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check match_bg_model parameters failed!\n", ret);
        return ret;
    }

    cve_fill_match_bg_model_task(&pstOpMatchBgModel->stCurImg, &pstOpMatchBgModel->stPreImg,
                                 &pstOpMatchBgModel->stBgModel, &pstOpMatchBgModel->stFg,
                                 &pstOpMatchBgModel->stBg, &pstOpMatchBgModel->stCurDiffBg,
                                 &pstOpMatchBgModel->stFrmDiff,
                                 &pstOpMatchBgModel->stMatchBgModelCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_MATCH_BG_MODEL;
    io_info.outp_flags = MATCH_BG_MODEL_OUTPUT_PROCESS_REG;
    io_info.outp_size = pstOpMatchBgModel->stStatData.u32Size;
    io_info.outp_phys_addr = pstOpMatchBgModel->stStatData.u64PhyAddr;

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpMatchBgModel->cveHandle,
                           pstOpMatchBgModel->bInstant, &io_info);

    cve_context.invoke_count.match_bg_model++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_update_bg_model(CVE_OP_UPDATE_BG_MODEL_T *pstOpUpdateBgModel)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpUpdateBgModel == NULL) {
        CVE_ERR_TRACE("update_bg_model arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpUpdateBgModel->stCurImg);
    set_mem_phy_addr(&pstOpUpdateBgModel->stBgModel1);
    set_mem_phy_addr(&pstOpUpdateBgModel->stBgModel2);
    set_mem_phy_addr(&pstOpUpdateBgModel->stStatData);
    ret = cve_check_update_bg_model_param(
        &pstOpUpdateBgModel->stCurImg, &pstOpUpdateBgModel->stBgModel1,
        &pstOpUpdateBgModel->stBgModel2, &pstOpUpdateBgModel->stStatData,
        &pstOpUpdateBgModel->stUpdateBgModelCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check update_bg_model parameters failed!\n", ret);
        return ret;
    }

    cve_fill_update_bg_model_task(&pstOpUpdateBgModel->stCurImg, &pstOpUpdateBgModel->stBgModel1,
                                  &pstOpUpdateBgModel->stBgModel2,
                                  &pstOpUpdateBgModel->stUpdateBgModelCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_UPDATE_BG_MODEL;
    io_info.outp_flags = UPDATE_BG_MODEL_OUTPUT_PROCESS_REG;
    io_info.outp_size = pstOpUpdateBgModel->stStatData.u32Size;
    io_info.outp_phys_addr = pstOpUpdateBgModel->stStatData.u64PhyAddr;

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpUpdateBgModel->cveHandle,
                           pstOpUpdateBgModel->bInstant, &io_info);

    cve_context.invoke_count.update_bg_model++;
    up(&cve_context.cve_sema);

    return ret;
}

static int
cve_build_lk_optical_flow_pyr(CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR_T *pstOpBuildLkOpticalFlowPyr)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    int ret = 0;
    int i = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpBuildLkOpticalFlowPyr == NULL) {
        CVE_ERR_TRACE("build_lk_optical_flow_pyr arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpBuildLkOpticalFlowPyr->stSrcPyr);
    for (i = 0; i < pstOpBuildLkOpticalFlowPyr->stLkBuildOptiFlowPyrCtrl.u8MaxLevel; i++) {
        set_image_phy_addr(&pstOpBuildLkOpticalFlowPyr->astDstPyr[i]);
    };
    ret = cve_check_build_lk_optical_flow_pyr_param(
        &pstOpBuildLkOpticalFlowPyr->stSrcPyr, pstOpBuildLkOpticalFlowPyr->astDstPyr,
        &pstOpBuildLkOpticalFlowPyr->stLkBuildOptiFlowPyrCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check build_lk_optical_flow_pyr parameters failed!\n", ret);
        return ret;
    }

    cve_fill_build_lk_optical_flow_pyr_task(
        &pstOpBuildLkOpticalFlowPyr->stSrcPyr, pstOpBuildLkOpticalFlowPyr->astDstPyr,
        &pstOpBuildLkOpticalFlowPyr->stLkBuildOptiFlowPyrCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpBuildLkOpticalFlowPyr->cveHandle,
                           pstOpBuildLkOpticalFlowPyr->bInstant, NULL);

    cve_context.invoke_count.bulid_lk_optical_flow_pyr++;
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_lk_optical_flow_pyr(CVE_OP_LK_OPTICAL_FLOW_PYR_T *pstOpLkOpticalFlowPyr)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;
    int i = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpLkOpticalFlowPyr == NULL) {
        CVE_ERR_TRACE("lk_optical_flow_pyr arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    for (i = 0; i < pstOpLkOpticalFlowPyr->stLkOptiFlowPyrCtrl.u8MaxLevel; i++) {
        set_image_phy_addr(&pstOpLkOpticalFlowPyr->astSrcPrevPyr[i]);
        set_image_phy_addr(&pstOpLkOpticalFlowPyr->astSrcNextPyr[i]);
    }
    set_mem_phy_addr(&pstOpLkOpticalFlowPyr->stPrevPts);
    set_mem_phy_addr(&pstOpLkOpticalFlowPyr->stNextPts);
    set_mem_phy_addr(&pstOpLkOpticalFlowPyr->stStatus);
    set_mem_phy_addr(&pstOpLkOpticalFlowPyr->stErr);
    ret = cve_check_lk_optical_flow_pyr_param(
        pstOpLkOpticalFlowPyr->astSrcPrevPyr, pstOpLkOpticalFlowPyr->astSrcNextPyr,
        &pstOpLkOpticalFlowPyr->stPrevPts, &pstOpLkOpticalFlowPyr->stNextPts,
        &pstOpLkOpticalFlowPyr->stStatus, &pstOpLkOpticalFlowPyr->stErr,
        &pstOpLkOpticalFlowPyr->stLkOptiFlowPyrCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check lk_optical_flow_pyr parameters failed!\n", ret);
        return ret;
    }

    cve_fill_lk_optical_flow_pyr_task(
        pstOpLkOpticalFlowPyr->astSrcPrevPyr, pstOpLkOpticalFlowPyr->astSrcNextPyr,
        &pstOpLkOpticalFlowPyr->stPrevPts, &pstOpLkOpticalFlowPyr->stNextPts,
        &pstOpLkOpticalFlowPyr->stErr, &pstOpLkOpticalFlowPyr->stLkOptiFlowPyrCtrl, cmd_buf,
        &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_LK_OPTIAL_FLOW_PRY;
    io_info.outp_flags = LK_OPTICAL_FLOWPYR_OUTPUT_PROCESS_REG;
    io_info.outp_size = pstOpLkOpticalFlowPyr->stStatus.u32Size;
    io_info.outp_phys_addr = pstOpLkOpticalFlowPyr->stStatus.u64PhyAddr;

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpLkOpticalFlowPyr->cveHandle,
                           pstOpLkOpticalFlowPyr->bInstant, &io_info);

    cve_context.invoke_count.lk_optial_flow_pry++;
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_st_candi_corner(CVE_OP_ST_CANDI_CORNER_T *pstOpSTCandiCorner)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpSTCandiCorner == NULL) {
        CVE_ERR_TRACE("st_candi_corner arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpSTCandiCorner->stSrc);
    set_image_phy_addr(&pstOpSTCandiCorner->stLabel);
    set_image_phy_addr(&pstOpSTCandiCorner->stCandiCorner);
    set_mem_phy_addr(&pstOpSTCandiCorner->stCandiCornerPoint);
    ret = cve_check_st_candi_corner_param(&pstOpSTCandiCorner->stSrc, &pstOpSTCandiCorner->stLabel,
                                          &pstOpSTCandiCorner->stCandiCorner,
                                          &pstOpSTCandiCorner->stCandiCornerPoint,
                                          &pstOpSTCandiCorner->stStCandiCornerCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check st_candi_corner parameters failed!\n", ret);
        return ret;
    }

    cve_fill_st_candi_corner_task(&pstOpSTCandiCorner->stSrc, &pstOpSTCandiCorner->stLabel,
                                  &pstOpSTCandiCorner->stCandiCorner,
                                  &pstOpSTCandiCorner->stCandiCornerPoint,
                                  &pstOpSTCandiCorner->stStCandiCornerCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_ST_CANDI_CORNER;
    io_info.outp_flags = STCORNER_OUTPUT_PROCESS_REG;
    io_info.outp_size = sizeof(AML_U32);
    io_info.outp_phys_addr = pstOpSTCandiCorner->stCandiCornerPoint.u64PhyAddr +
                             pstOpSTCandiCorner->stCandiCornerPoint.u32Size - sizeof(AML_U32);

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpSTCandiCorner->cveHandle,
                           pstOpSTCandiCorner->bInstant, &io_info);

    cve_context.invoke_count.st_candi_corner++;
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_canny_hys_edge(CVE_OP_CANNY_HYS_EDGE_T *pstOpCannyHysEdge)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpCannyHysEdge == NULL) {
        CVE_ERR_TRACE("canny_hys_edge arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_image_phy_addr(&pstOpCannyHysEdge->stSrcImage);
    set_image_phy_addr(&pstOpCannyHysEdge->stEdge);
    set_mem_phy_addr(&pstOpCannyHysEdge->stStack);
    ret = cve_check_canny_hys_edge_param(&pstOpCannyHysEdge->stSrcImage, &pstOpCannyHysEdge->stEdge,
                                         &pstOpCannyHysEdge->stStack,
                                         &pstOpCannyHysEdge->stCannyHysEdgeCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check canny_hys_edge parameters failed!\n", ret);
        return ret;
    }

    cve_fill_canny_hys_edge_task(&pstOpCannyHysEdge->stSrcImage, &pstOpCannyHysEdge->stEdge,
                                 &pstOpCannyHysEdge->stStack,
                                 &pstOpCannyHysEdge->stCannyHysEdgeCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_CANNY_HYS_EDGE;
    io_info.outp_size = sizeof(AML_U32);
    io_info.outp_flags = CANNY_OUTPUT_PROCESS_REG;
    io_info.outp_phys_addr = pstOpCannyHysEdge->stStack.u64PhyAddr +
                             pstOpCannyHysEdge->stStack.u32Size - sizeof(AML_U32);

    ret = cve_post_process(cmd_buf, cmd_line_num, &pstOpCannyHysEdge->cveHandle,
                           pstOpCannyHysEdge->bInstant, &io_info);

    cve_context.invoke_count.canny_edge++;
    up(&cve_context.cve_sema);
    return ret;
}

static int cve_tof(CVE_OP_TOF_T *pstOpTof)
{
    char *cmd_buf = cmd_buf_get();
    unsigned int cmd_line_num = 0;
    cve_op_io_info_t io_info;
    int ret = 0;

    if (cmd_buf == NULL) {
        return AML_ERR_CVE_NOBUF;
    }

    if (pstOpTof == NULL) {
        CVE_ERR_TRACE("tof arg is NULL\n");
        return AML_ERR_CVE_NULL_PTR;
    }
    set_raw_phy_addr(&pstOpTof->stSrcRaw);
    set_raw_phy_addr(&pstOpTof->stSrcFpn);
    set_mem_phy_addr(&pstOpTof->stSrcCoef);
    set_mem_phy_addr(&pstOpTof->stBpc);
    set_mem_phy_addr(&pstOpTof->stDtsStatus);
    set_mem_phy_addr(&pstOpTof->stDtsIR);
    set_mem_phy_addr(&pstOpTof->stDtsData);
    set_mem_phy_addr(&pstOpTof->stDstHist);

    ret = cve_check_tof_param(&pstOpTof->stSrcRaw, &pstOpTof->stSrcFpn, &pstOpTof->stSrcCoef,
                              &pstOpTof->stBpc, &pstOpTof->stDtsStatus, &pstOpTof->stDtsIR,
                              &pstOpTof->stDtsData, &pstOpTof->stDstHist, &pstOpTof->stTofCtrl);
    if (ret) {
        CVE_ERR_TRACE("ERRID(%x): check tof parameters failed!\n", ret);
        return ret;
    }

    cve_fill_tof_task(&pstOpTof->stSrcRaw, &pstOpTof->stSrcFpn, &pstOpTof->stSrcCoef,
                      &pstOpTof->stDtsStatus, &pstOpTof->stDtsIR, &pstOpTof->stDtsData,
                      &pstOpTof->stTofCtrl, cmd_buf, &cmd_line_num);
    if (down_interruptible(&cve_context.cve_sema)) {
        return -ERESTARTSYS;
    }

    memset((void *)&io_info, 0, sizeof(cve_op_io_info_t));
    io_info.op_type = CVE_OP_TYPE_TOF;
    io_info.inp_flags = TOF_INPUT_PROCESS_LUT;
    io_info.inp_size = pstOpTof->stBpc.u32Size;
    io_info.inp_phys_addr = pstOpTof->stBpc.u64PhyAddr;
    io_info.outp_flags = TOF_OUTPUT_PROCESS_LUT;
    io_info.outp_size = pstOpTof->stDstHist.u32Size;
    io_info.outp_phys_addr = pstOpTof->stDstHist.u64PhyAddr;

    ret =
        cve_post_process(cmd_buf, cmd_line_num, &pstOpTof->cveHandle, pstOpTof->bInstant, &io_info);

    cve_context.invoke_count.tof++;
    up(&cve_context.cve_sema);

    return ret;
}

static int cve_close(struct inode *inode, struct file *file)
{
    return 0;
}

static int cve_open(struct inode *inode, struct file *file)
{
    // cve_runtime_set_power(1, cve_pdev);
    cve_context.run_time_info.cve_cycle.cve_ute_stat_en = true;
    if (cve_context.run_time_info.cve_cycle.cve_ute_stat_en == true) {
        cve_ute_stat_enable(666000000);
    }
    return 0;
}

static long cve_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;

    if (cve_state) {
        CVE_ERR_TRACE("cve is not ready\n");
        return AML_ERR_CVE_NOTREADY;
    }

    atomic_inc_return(&cve_user_ref);

    switch (cmd) {
    case CVE_OP_DILATE: {
        CVE_OP_DILATE_T stOpDilate;
        ret = copy_from_user(&stOpDilate, (void *)arg, sizeof(CVE_OP_DILATE_T));
        ret = cve_dilate(&stOpDilate);
        break;
    }
    case CVE_OP_SAD: {
        CVE_OP_SAD_T stOpSAD;

        ret = copy_from_user(&stOpSAD, (void *)arg, sizeof(CVE_OP_SAD_T));
        ret = cve_sad(&stOpSAD);
        break;
    }
    case CVE_OP_GRAD_FG: {
        CVE_OP_GRAD_FG_T stOpGradFg;

        ret = copy_from_user(&stOpGradFg, (void *)arg, sizeof(CVE_OP_GRAD_FG_T));
        ret = cve_grad_fg(&stOpGradFg);
        break;
    }
    case CVE_OP_CANNY_HYS_EDGE: {
        CVE_OP_CANNY_HYS_EDGE_T stOpCannyHysEdge;

        ret = copy_from_user(&stOpCannyHysEdge, (void *)arg, sizeof(CVE_OP_CANNY_HYS_EDGE_T));
        ret = cve_canny_hys_edge(&stOpCannyHysEdge);
        ret = copy_to_user((void *)arg, &stOpCannyHysEdge, sizeof(CVE_OP_CANNY_HYS_EDGE_T));
        break;
    }
    case CVE_OP_SOBEL: {
        CVE_OP_SOBEL_T stOpSobel;

        ret = copy_from_user(&stOpSobel, (void *)arg, sizeof(CVE_OP_SOBEL_T));
        ret = cve_sobel(&stOpSobel);
        break;
    }
    case CVE_OP_LK_OPTICAL_FLOW_PYR: {
        CVE_OP_LK_OPTICAL_FLOW_PYR_T stOpLkOpticalFlowPyr;

        ret = copy_from_user(&stOpLkOpticalFlowPyr, (void *)arg,
                             sizeof(CVE_OP_LK_OPTICAL_FLOW_PYR_T));
        ret = cve_lk_optical_flow_pyr(&stOpLkOpticalFlowPyr);
        ret =
            copy_to_user((void *)arg, &stOpLkOpticalFlowPyr, sizeof(CVE_OP_LK_OPTICAL_FLOW_PYR_T));
        break;
    }
    case CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR: {
        CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR_T stOpBuildLkOpticalFlowPyr;

        ret = copy_from_user(&stOpBuildLkOpticalFlowPyr, (void *)arg,
                             sizeof(CVE_OP_BUILD_LK_OPTICAL_FLOW_PYR_T));
        ret = cve_build_lk_optical_flow_pyr(&stOpBuildLkOpticalFlowPyr);
        break;
    }
    case CVE_OP_NORM_GRAD: {
        CVE_OP_NORM_GRAD_T stOpNormGrad;

        ret = copy_from_user(&stOpNormGrad, (void *)arg, sizeof(CVE_OP_NORM_GRAD_T));
        ret = cve_norm_grad(&stOpNormGrad);
        break;
    }
    case CVE_OP_ST_CANDI_CORNER: {
        CVE_OP_ST_CANDI_CORNER_T stOpSTCandiCorner;

        ret = copy_from_user(&stOpSTCandiCorner, (void *)arg, sizeof(CVE_OP_ST_CANDI_CORNER_T));
        ret = cve_st_candi_corner(&stOpSTCandiCorner);
        ret = copy_to_user((void *)arg, &stOpSTCandiCorner, sizeof(CVE_OP_ST_CANDI_CORNER_T));
        break;
    }
    case CVE_OP_MAP: {
        CVE_OP_MAP_T stOpMap;

        ret = copy_from_user(&stOpMap, (void *)arg, sizeof(CVE_OP_MAP_T));
        ret = cve_map(&stOpMap);
        break;
    }
    case CVE_OP_ERODE: {
        CVE_OP_ERODE_T stOpErode;

        ret = copy_from_user(&stOpErode, (void *)arg, sizeof(CVE_OP_ERODE_T));
        ret = cve_erode(&stOpErode);
        break;
    }
    case CVE_OP_NCC: {
        CVE_OP_NCC_T stOpNcc;

        ret = copy_from_user(&stOpNcc, (void *)arg, sizeof(CVE_OP_NCC_T));
        ret = cve_ncc(&stOpNcc);
        break;
    }
    case CVE_OP_EQUALIZE_HIST: {
        CVE_OP_EQUALIZE_HIST_T stOpEqualizeHist;

        ret = copy_from_user(&stOpEqualizeHist, (void *)arg, sizeof(CVE_OP_EQUALIZE_HIST_T));
        ret = cve_equalize_hist(&stOpEqualizeHist);
        break;
    }
    case CVE_OP_CCL: {
        CVE_OP_CCL_T stOpCcl;

        ret = copy_from_user(&stOpCcl, (void *)arg, sizeof(CVE_OP_CCL_T));
        ret = cve_ccl(&stOpCcl);
        break;
    }
    case CVE_OP_GMM: {
        CVE_OP_GMM_T stOpGmm;

        ret = copy_from_user(&stOpGmm, (void *)arg, sizeof(CVE_OP_GMM_T));
        ret = cve_gmm(&stOpGmm);
        break;
    }
    case CVE_OP_THRESH_S16: {
        CVE_OP_THRESH_S16_T stOpThreshS16;

        ret = copy_from_user(&stOpThreshS16, (void *)arg, sizeof(CVE_OP_THRESH_S16_T));
        ret = cve_thresh_s16(&stOpThreshS16);
        break;
    }
    case CVE_OP_16BIT_TO_8BIT: {
        CVE_OP_16BIT_TO_8BIT_T stOp16bitTo8bit;

        ret = copy_from_user(&stOp16bitTo8bit, (void *)arg, sizeof(CVE_OP_16BIT_TO_8BIT_T));
        ret = cve_16_to_8(&stOp16bitTo8bit);
        break;
    }
    case CVE_OP_THRESH_U16: {
        CVE_OP_THRESH_U16_T stOpThreshU16;

        ret = copy_from_user(&stOpThreshU16, (void *)arg, sizeof(CVE_OP_THRESH_U16_T));
        ret = cve_thresh_u16(&stOpThreshU16);
        break;
    }
    case CVE_OP_LBP: {
        CVE_OP_LBP_T stOpLbp;

        ret = copy_from_user(&stOpLbp, (void *)arg, sizeof(CVE_OP_LBP_T));
        ret = cve_lbp(&stOpLbp);
        break;
    }
    case CVE_OP_FILTER: {
        CVE_OP_FILTER_T stOpFilter;

        ret = copy_from_user(&stOpFilter, (void *)arg, sizeof(CVE_OP_FILTER_T));
        ret = cve_filter(&stOpFilter);
        break;
    }
    case CVE_OP_INTEG: {
        CVE_OP_INTEG_T stOpInteg;

        ret = copy_from_user(&stOpInteg, (void *)arg, sizeof(CVE_OP_INTEG_T));
        ret = cve_integ(&stOpInteg);
        break;
    }
    case CVE_OP_CSC: {
        CVE_OP_CSC_T stOpCsc;

        ret = copy_from_user(&stOpCsc, (void *)arg, sizeof(CVE_OP_CSC_T));
        ret = cve_csc(&stOpCsc);
        break;
    }
    case CVE_OP_FILTER_AND_CSC: {
        CVE_OP_FILTER_AND_CSC_T stOpFilterCsc;

        ret = copy_from_user(&stOpFilterCsc, (void *)arg, sizeof(CVE_OP_FILTER_AND_CSC_T));
        ret = cve_filter_and_csc(&stOpFilterCsc);
        break;
    }
    case CVE_OP_ORD_STAT_FILTER: {
        CVE_OP_ORD_STAT_FILTER_T stOpOrdStatFilter;

        ret = copy_from_user(&stOpOrdStatFilter, (void *)arg, sizeof(CVE_OP_ORD_STAT_FILTER_T));
        ret = cve_ord_stat_filter(&stOpOrdStatFilter);
        break;
    }
    case CVE_OP_THRESH: {
        CVE_OP_THRESH_T stOpThresh;

        ret = copy_from_user(&stOpThresh, (void *)arg, sizeof(CVE_OP_THRESH_T));
        ret = cve_thresh(&stOpThresh);
        break;
    }
    case CVE_OP_DMA: {
        CVE_OP_DMA_T stOpDMA;
        ret = copy_from_user(&stOpDMA, (void *)arg, sizeof(CVE_OP_DMA_T));
        ret = cve_dma(&stOpDMA);
        break;
    }
    case CVE_OP_LUMA_STAT: {
        CVE_OP_LUAM_STAT_ARRAY_T stOpLuamStat;

        ret = copy_from_user(&stOpLuamStat, (void *)arg, sizeof(CVE_OP_LUAM_STAT_ARRAY_T));
        ret = cve_luma_stat(&stOpLuamStat);
        break;
    }
    case CVE_OP_AND: {
        CVE_OP_AND_T stOpAnd;

        ret = copy_from_user(&stOpAnd, (void *)arg, sizeof(CVE_OP_AND_T));
        ret = cve_and(&stOpAnd);
        break;
    }
    case CVE_OP_OR: {
        CVE_OP_OR_T stOpOr;

        ret = copy_from_user(&stOpOr, (void *)arg, sizeof(CVE_OP_OR_T));
        ret = cve_or(&stOpOr);
        break;
    }
    case CVE_OP_XOR: {
        CVE_OP_XOR_T stOpXor;

        ret = copy_from_user(&stOpXor, (void *)arg, sizeof(CVE_OP_XOR_T));
        ret = cve_xor(&stOpXor);
        break;
    }
    case CVE_OP_SUB: {
        CVE_OP_SUB_T stOpSub;

        ret = copy_from_user(&stOpSub, (void *)arg, sizeof(CVE_OP_SUB_T));
        ret = cve_sub(&stOpSub);
        break;
    }
    case CVE_OP_ADD: {
        CVE_OP_ADD_T stOpAdd;

        ret = copy_from_user(&stOpAdd, (void *)arg, sizeof(CVE_OP_ADD_T));
        ret = cve_add(&stOpAdd);
        break;
    }
    case CVE_OP_HIST: {
        CVE_OP_HIST_T stOpHist;

        ret = copy_from_user(&stOpHist, (void *)arg, sizeof(CVE_OP_HIST_T));
        ret = cve_hist(&stOpHist);
        break;
    }
    case CVE_OP_MAG_AND_ANG: {
        CVE_OP_MAG_AND_ANG_T stOpMagAndAng;

        ret = copy_from_user(&stOpMagAndAng, (void *)arg, sizeof(CVE_OP_MAG_AND_ANG_T));
        ret = cve_mag_and_ang(&stOpMagAndAng);
        break;
    }
    case CVE_OP_MATCH_BG_MODEL: {
        CVE_OP_MATCH_BG_MODEL_T stOpMatchBgModel;

        ret = copy_from_user(&stOpMatchBgModel, (void *)arg, sizeof(CVE_OP_MATCH_BG_MODEL_T));
        ret = cve_match_bg_model(&stOpMatchBgModel);
        ret = copy_to_user((void *)arg, &stOpMatchBgModel, sizeof(CVE_OP_MATCH_BG_MODEL_T));
        break;
    }
    case CVE_OP_UPDATE_BG_MODEL: {
        CVE_OP_UPDATE_BG_MODEL_T stOpUpdateBgModel;

        ret = copy_from_user(&stOpUpdateBgModel, (void *)arg, sizeof(CVE_OP_UPDATE_BG_MODEL_T));
        ret = cve_update_bg_model(&stOpUpdateBgModel);
        ret = copy_to_user((void *)arg, &stOpUpdateBgModel, sizeof(CVE_OP_UPDATE_BG_MODEL_T));
        break;
    }
    case CVE_OP_TOF: {
        CVE_OP_TOF_T stOpTof;

        ret = copy_from_user(&stOpTof, (void *)arg, sizeof(CVE_OP_TOF_T));
        ret = cve_tof(&stOpTof);
        ret = copy_to_user((void *)arg, &stOpTof, sizeof(CVE_OP_TOF_T));
        break;
    }
    case CVE_OP_QUERY: {
        CVE_OP_QUERY_T stOpQuery;

        ret = copy_from_user(&stOpQuery, (void *)arg, sizeof(CVE_OP_QUERY_T));
        ret = cve_query(&stOpQuery);
        break;
    }
    default: {
        CVE_ERR_TRACE("cve can't support the op %08X\n", cmd);
        return AML_ERR_CVE_NOT_SURPPORT;
    }
    }

    atomic_dec_return(&cve_user_ref);

    return ret;
}

#ifdef CONFIG_COMPAT
static long cve_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    return cve_ioctl(file, cmd, arg);
}
#endif

static struct file_operations g_cve_fops = {
    .owner = THIS_MODULE,
    .open = cve_open,
    .release = cve_close,
    .unlocked_ioctl = cve_ioctl,
#ifdef CONFIG_COMPAT
    .compat_ioctl = cve_compat_ioctl,
#endif
};

static ssize_t cve_debug_write(struct file *file, const char __user *buffer, size_t count,
                               loff_t *ppos)
{
    int ok = 0;
    char *buf;

    buf = kmalloc(count + 1, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    memset(buf, 0, count + 1);
    if (copy_from_user(buf, buffer, count))
        goto exit;

exit:
    kfree(buf);
    if (ok)
        return count;
    else
        return -EINVAL;
}

static int cve_debug_open(struct inode *inode, struct file *file)
{
    return single_open(file, cve_proc_show, NULL);
}
static const struct file_operations cve_proc_file_ops = {
    .open = cve_debug_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .write = cve_debug_write,
    .release = single_release,
};

static void cve_input_process(cve_op_io_info_t *info)
{
    void *pvKvirt = cve_vmap(info->inp_phys_addr, info->inp_size);

    if (pvKvirt == NULL) {
        CVE_ERR_TRACE("rmmap failed\n");
    }

    switch (info->op_type) {

    case CVE_OP_TYPE_EQUALIZE_HIST: {
        unsigned char *pau8curv;
        int i = 0;

        pau8curv = (unsigned char *)pvKvirt;
        cve_reg_write(CVE_LUT_REG0, CVE_EQHIST_AU8CURV_LUT);
        for (i = 0; i < CVE_HIST_NUM; i++) {
            cve_reg_write(CVE_LUT_REG1, pau8curv[i]);
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);

        break;
    }
    case CVE_OP_TYPE_MAP: {
        unsigned char *pau8map;
        unsigned short *pau16map;
        short *pas16map;

        CVE_MAP_MODE_E enMode = (CVE_MAP_MODE_E)info->op_mode;
        int i = 0;

        cve_reg_write(CVE_LUT_REG0, CVE_MAP_AU16MAP_LUT);
        if (enMode == CVE_MAP_MODE_U8) {
            pau8map = (unsigned char *)pvKvirt;
            for (i = 0; i < CVE_MAP_NUM; i++) {
                cve_reg_write(CVE_LUT_REG1, pau8map[i]);
            }
        } else if (enMode == CVE_MAP_MODE_U16) {
            pau16map = (unsigned short *)pvKvirt;
            for (i = 0; i < CVE_MAP_NUM; i++) {
                cve_reg_write(CVE_LUT_REG1, pau16map[i]);
            }
        } else {
            pas16map = (short *)pvKvirt;
            for (i = 0; i < CVE_MAP_NUM; i++) {
                cve_reg_write(CVE_LUT_REG1, pas16map[i]);
            }
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);

        break;
    }
    case CVE_OP_TYPE_TOF: {
        unsigned int *phist;
        int i = 0;

        cve_reg_write(CVE_LUT_REG0, CVE_TOF_BPC_LUT);
        phist = (unsigned int *)pvKvirt;
        for (i = 0; i < CVE_MAP_NUM; i++) {
            cve_reg_write(CVE_LUT_REG1, phist[i]);
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);

        break;
    }
    default:
        break;
    }

    cve_vunmap(pvKvirt);

    return;
}

static void cve_output_process(cve_op_io_info_t *info)
{

    void *pvKvirt = cve_vmap(info->outp_phys_addr, info->outp_size);

    if (pvKvirt == NULL) {
        CVE_ERR_TRACE("rmmap failed\n");
    }

    switch (info->op_type) {

    case CVE_OP_TYPE_HIST: {
        unsigned int *pau21hist;
        unsigned char *pau8curv;
        int i = 0;

        pau21hist = (unsigned int *)pvKvirt;
        cve_reg_write(CVE_LUT_REG0, CVE_HIST_LUT);
        for (i = 0; i < CVE_HIST_NUM; i++) {
            pau21hist[i] = cve_reg_read(CVE_LUT_REG1);
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);

        pau8curv = (unsigned char *)(pau21hist + CVE_HIST_NUM);
        cve_reg_write(CVE_LUT_REG0, CVE_EQHIST_AU8CURV_LUT);
        for (i = 0; i < CVE_HIST_NUM; i++) {
            pau8curv[i] = (unsigned char)cve_reg_read(CVE_LUT_REG1);
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);
        break;
    }
    case CVE_OP_TYPE_NCC: {
        CVE_NCC_DST_MEM_T *pstNccMem = (CVE_NCC_DST_MEM_T *)pvKvirt;
        pstNccMem->u64Numerator = cve_reg_read(CVE_NCC_U64NUM_REG0);
        pstNccMem->u64Numerator += (AML_U64)cve_reg_read(CVE_NCC_U64NUM_REG1) << 32;
        pstNccMem->u64QuadSum1 = cve_reg_read(CVE_NCC_U64QUADSUM1_REG0);
        pstNccMem->u64QuadSum1 += (AML_U64)cve_reg_read(CVE_NCC_U64QUADSUM2_REG0) << 32;
        pstNccMem->u64QuadSum2 = cve_reg_read(CVE_NCC_U64QUADSUM2_REG0);
        pstNccMem->u64QuadSum2 += (AML_U64)cve_reg_read(CVE_NCC_U64QUADSUM2_REG1) << 32;
        break;
    }
    case CVE_OP_TYPE_CCL: {
        CVE_CCBLOB_T *pstBlob = (CVE_CCBLOB_T *)pvKvirt;

        pstBlob->u16RegionNum = (unsigned short)cve_reg_read(CVE_CCL_REG0);
        break;
    }
    case CVE_OP_TYPE_MATCH_BG_MODEL: {
        CVE_BG_STAT_DATA_T *pstBgStatData = (CVE_BG_STAT_DATA_T *)pvKvirt;

        pstBgStatData->u32PixNum = cve_reg_read(CVE_BGMODE_REG1);
        pstBgStatData->u32SumLum = cve_reg_read(CVE_BGMODE_REG2);
        break;
    }
    case CVE_OP_TYPE_UPDATE_BG_MODEL: {
        CVE_BG_STAT_DATA_T *pstBgStatData = (CVE_BG_STAT_DATA_T *)pvKvirt;

        pstBgStatData->u32PixNum = cve_reg_read(CVE_UPDATEBGMODE_REG2);
        pstBgStatData->u32SumLum = cve_reg_read(CVE_UPDATEBGMODE_REG3);
        break;
    }
    case CVE_OP_TYPE_LK_OPTIAL_FLOW_PRY: {
        unsigned int *lk_status;
        int i = 0;
        lk_status = (unsigned int *)pvKvirt;
        for (i = 0; i < 17; i++) {
            lk_status[i] = cve_reg_read(CVE_LK_REG2_0 + i);
        }
        break;
    }
    case CVE_OP_TYPE_ST_CANDI_CORNER: {
        unsigned int *cn = (unsigned int *)pvKvirt;
        *cn = cve_reg_read(CVE_STCORNER_REG0);
        break;
    }
    case CVE_OP_TYPE_CANNY_HYS_EDGE: {
        unsigned int *cc = (unsigned int *)pvKvirt;
        *cc = cve_reg_read(CVE_CANNY_REG1);
        break;
    }
    case CVE_OP_TYPE_TOF: {
        unsigned int *phist;
        unsigned int v32;
        int i = 0;

        phist = (unsigned int *)pvKvirt;
        cve_reg_write(CVE_LUT_REG0, CVE_TOF_IR_HIST_LUT);
        for (i = 0; i < CVE_HIST_NUM; i++) {
            v32 = cve_reg_read(CVE_LUT_REG1);
            phist[i] = v32;
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);
        phist = (unsigned int *)(phist + CVE_HIST_NUM);
        cve_reg_write(CVE_LUT_REG0, CVE_TOF_Q2_HIST_LUT);
        for (i = 0; i < CVE_HIST_NUM; i++) {
            v32 = cve_reg_read(CVE_LUT_REG1);
            phist[i] = v32;
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);
        phist = (unsigned int *)(phist + CVE_HIST_NUM * 2);
        cve_reg_write(CVE_LUT_REG0, CVE_TOF_Q3_HIST_LUT);
        for (i = 0; i < CVE_HIST_NUM; i++) {
            v32 = cve_reg_read(CVE_LUT_REG1);
            phist[i] = v32;
        }
        cve_reg_write(CVE_LUT_REG0, CVE_LUT_DONE);
    }
    default:
        break;
    }
    cve_vunmap(pvKvirt);

    return;
}
static void cve_output_process_work(struct work_struct *work)
{
    cve_task_desc_t *task_desc;
    cve_cmd_desc_t *cmd_desc;
    cve_cq_desc_t *cq_desc_busy;
    cve_task_info_t *task_info;
    task_desc = cve_context.task_desc_outp;
    task_info = &cve_context.task_info;

    list_for_each_entry(cmd_desc, &task_desc->cmd_list, list)
    {
        if (cmd_desc->io_info.outp_flags != 0 && cmd_desc->io_info.outp_phys_addr != 0) {
            cve_output_process(&cmd_desc->io_info);
        }
    }
    INIT_LIST_HEAD(&task_desc->cmd_list);
    task_info->cmd_finish_cnt += task_desc->total_cmd_num;
    task_info->cur_finish_cmd_id += task_desc->total_cmd_num;

    if (cve_context.queue_busy != CVE_STATUS_IDLE) {
        cq_desc_busy = &cve_context.cq_desc[cve_context.queue_busy];
        task_desc = &cq_desc_busy->task_descs[cq_desc_busy->task_descs_invoke_index];
        if (task_desc->bInput) {
            list_for_each_entry(cmd_desc, &task_desc->cmd_list, list)
            {
                if (cmd_desc->io_info.inp_flags != 0 && cmd_desc->io_info.inp_phys_addr != 0) {
                    cve_input_process(&cmd_desc->io_info);
                }
            }
        }
        cve_start_task(task_desc);
    }
    wake_up(&cve_context.cve_wait);
    return;
}

static int cve_create_cmd_desc(cve_cq_desc_t *cq_desc)
{
    unsigned int cnt = 0;

    cq_desc->cmd_descs =
        (cve_cmd_desc_t *)kmalloc(sizeof(cve_cmd_desc_t) * cq_desc->total_cmd_max, GFP_KERNEL);
    if (cq_desc->cmd_descs == NULL) {
        CVE_ERR_TRACE("cmd descriptor alloc failed\n");
        return AML_ERR_CVE_NOMEM;
    }
    memset(cq_desc->cmd_descs, 0, sizeof(cve_cmd_desc_t) * cq_desc->total_cmd_max);

    for (cnt = 0; cnt < cq_desc->total_cmd_max; cnt++) {
        cq_desc->cmd_descs[cnt].cveHandle = -1;
        INIT_LIST_HEAD(&cq_desc->cmd_descs[cnt].list);
    }

    return 0;
}

static int cve_create_task_desc(cve_cq_desc_t *cq_desc)
{
    unsigned int cnt = 0;

    cq_desc->task_descs =
        (cve_task_desc_t *)kmalloc(sizeof(cve_task_desc_t) * cq_desc->task_max, GFP_KERNEL);
    if (cq_desc->task_descs == NULL) {
        CVE_ERR_TRACE("task descriptor alloc failed\n");
        return AML_ERR_CVE_NOMEM;
    }
    memset(cq_desc->task_descs, 0, sizeof(cve_task_desc_t) * cq_desc->task_max);
    for (cnt = 0; cnt < cq_desc->task_max; cnt++) {
        cq_desc->task_descs[cnt].task_id = cnt;
        task_descs_init(&cq_desc->task_descs[cnt]);
    }

    return 0;
}

static int cve_cmd_queue_init(cve_context_t *cve_context)
{
    int i = 0;
    int ret = 0;

    cve_context->cve_cq_buffer.size = 2 * CVE_OP_NODE_CMD_SIZE_MAX * cve_node_num;
    ret = cve_ion_alloc_buffer(&cve_context->cve_cq_buffer);
    if (ret) {
        printk("cve alloc cmd queue buffer error\n");
    }
    cve_context->cq_desc[0].phys_start = cve_context->cve_cq_buffer.phys_start;
    cve_context->cq_desc[0].virt_start = cve_context->cve_cq_buffer.virt_start;
    cve_context->cq_desc[1].phys_start =
        cve_context->cq_desc[0].phys_start + CVE_OP_NODE_CMD_SIZE_MAX * cve_node_num;
    cve_context->cq_desc[1].virt_start =
        cve_context->cq_desc[0].virt_start + CVE_OP_NODE_CMD_SIZE_MAX * cve_node_num;
    for (i = 0; i < 2; i++) {
        cve_context->cq_desc[i].length = CVE_OP_NODE_CMD_SIZE_MAX * cve_node_num;
        cve_context->cq_desc[i].total_cmd_max = cve_node_num;
        cve_context->cq_desc[i].task_max = cve_node_num;
        cve_context->cq_desc[i].task_phys_offset = 0;
        cve_context->cq_desc[i].task_virt_offset = 0;
        cve_context->cq_desc[i].cur_cmd_id = 0;
        cve_context->cq_desc[i].end_cmd_id = 0;
        cve_context->cq_desc[i].task_instant = 0;
        cve_create_task_desc(&cve_context->cq_desc[i]);
        cve_context->cq_desc[i].task_descs_create_index = 0;
        cve_context->cq_desc[i].task_descs_invoke_index = 0;
        cve_create_cmd_desc(&cve_context->cq_desc[i]);
        cve_context->cq_desc[i].cmd_descs_index = 0;
    }
    memset((void *)cve_context->cq_desc[0].virt_start, 0,
           2 * CVE_OP_NODE_CMD_SIZE_MAX * cve_node_num);

    return 0;
}

static int cve_cmd_queue_deinit(cve_context_t *cve_context)
{
    int i = 0;

    if (cve_context) {
        for (i = 0; i < 2; i++) {
            if (cve_context->cq_desc[i].task_descs) {
                kfree(cve_context->cq_desc[i].task_descs);
                cve_context->cq_desc[i].task_descs = NULL;
            }
            if (cve_context->cq_desc[i].cmd_descs) {
                kfree(cve_context->cq_desc[i].cmd_descs);
                cve_context->cq_desc[i].cmd_descs = NULL;
            }
            if (cve_context->cq_desc[i].phys_start && cve_context->cq_desc[i].virt_start) {
                cve_context->cq_desc[i].phys_start = 0;
                cve_context->cq_desc[i].virt_start = 0;
            }
        }
    }

    return 0;
}

static int cve_init()
{
    int ret = 0;

    cve_context.queue_wait = CVE_STATUS_CQ0;
    cve_context.queue_busy = CVE_STATUS_IDLE;

    init_waitqueue_head(&cve_context.cve_wait);
    INIT_WORK(&cve_context.cve_wrok, cve_output_process_work);

    ret = cve_cmd_queue_init(&cve_context);
    if (ret) {
        CVE_ERR_TRACE("cve init cmd queue failed!\n");
        goto ERR_WORK_DESTORY;
    }

    cve_context.cmd_bufs.length = CVE_OP_NODE_CMD_SIZE_MAX * cve_node_num;

    cve_context.cve_cmd_buffer.size = cve_context.cmd_bufs.length;
    ret = cve_ion_alloc_buffer(&cve_context.cve_cmd_buffer);
    if (ret) {
        CVE_ERR_TRACE("cve failed malloc mem!\n");
        ret = AML_ERR_CVE_NOMEM;
        goto ERR_CMD_QUEUE_DEINIT;
    }
    cve_context.cmd_bufs.virt_start = cve_context.cve_cmd_buffer.virt_start;
    cve_context.cmd_bufs.phys_start = cve_context.cve_cmd_buffer.phys_start;
    cve_context.cmd_bufs.cur_index = 0;
    cve_context.cmd_bufs.cmd_size = CVE_OP_NODE_CMD_SIZE_MAX;
    cve_context.cmd_bufs.index_max = cve_node_num;

    sema_init(&cve_context.cmd_bufs.cmd_buf_sema, 1);
    ret = request_irq(cve_irq, (irq_handler_t)cve_irq_handler, 0, "CVE", cve_pdev);
    if (ret) {
        CVE_ERR_TRACE("CVE failed request irq!\n");
        goto ERR_FREE_CMD_BUF;
    }

    return 0;

ERR_FREE_CMD_BUF:
    cve_ion_free_buffer(&cve_context.cve_cmd_buffer);
    cve_context.cmd_bufs.phys_start = 0;
    cve_context.cmd_bufs.virt_start = 0;

ERR_CMD_QUEUE_DEINIT:
    cve_cmd_queue_deinit(&cve_context);
ERR_WORK_DESTORY:
    flush_work(&cve_context.cve_wrok); //

    return ret;
}

static char *cmd_buf_get()
{
    cve_cmd_buf_t *cmd_bufs = &cve_context.cmd_bufs;
    char *cmd_buf;

    if (cmd_bufs == NULL) {
        CVE_ERR_TRACE("cmd_bufs is NULL\n");
        return NULL;
    }

    if (down_interruptible(&cmd_bufs->cmd_buf_sema)) {
        return NULL;
    }
    if ((cmd_bufs->cur_index + 1 > cve_node_num)) {
        cmd_bufs->cur_index = 0;
    }
    cmd_buf = (char *)cmd_bufs->virt_start + cmd_bufs->cmd_size * cmd_bufs->cur_index;
    cmd_bufs->cur_index++;

    up(&cmd_bufs->cmd_buf_sema);

    return cmd_buf;
}

static int request_op_cmd(cve_cmd_desc_t **cmd_desc, unsigned int cmd_num)
{
    cve_cq_desc_t *cq_desc_wait;

    if (cmd_desc == NULL) {
        CVE_ERR_TRACE("cmd_desc is NULL\n");
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }

    cq_desc_wait = &cve_context.cq_desc[cve_context.queue_wait];

    if (cq_desc_wait->cmd_descs_index + cmd_num > cve_node_num) {
        CVE_ERR_TRACE("Waiting queue is full of task! end_cmd_id = %d,max task = %d\n",
                      cq_desc_wait->cmd_descs_index + cmd_num, cve_node_num);
        return AML_ERR_CVE_BUF_FULL;
    }

    *cmd_desc = &cq_desc_wait->cmd_descs[cq_desc_wait->cmd_descs_index];
    cq_desc_wait->cmd_descs_index += cmd_num;

    return 0;
}

static int cve_check_mem(unsigned long long phy_addr, unsigned long addr_len, unsigned int aligned)
{
    int ret = 0;

    if (aligned != 1 && (unsigned int)phy_addr & (aligned - 1)) {
        CVE_ERR_TRACE("phy_addr(0x%llx) must be %d byte align!\n", phy_addr, aligned);
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }

    return ret;
}

static int cve_check_image_mem(CVE_IMAGE_T *pstImg, unsigned int aligned)
{
    unsigned int mem_size = 0;

    if (NULL == pstImg) {
        CVE_ERR_TRACE("pstImg is null\n");
        return AML_FAILURE;
    }

    switch (pstImg->enType) {
    case CVE_IMAGE_TYPE_U8C1:
    case CVE_IMAGE_TYPE_S8C1:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height;
        break;
    case CVE_IMAGE_TYPE_YUV420SP:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * 3 / 2;
        break;
    case CVE_IMAGE_TYPE_YUV422SP:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * 2;
        break;
    case CVE_IMAGE_TYPE_YUV420P:
        mem_size =
            pstImg->au32Stride[0] * pstImg->u32Height + pstImg->au32Stride[1] * pstImg->u32Height;
        break;
    case CVE_IMAGE_TYPE_YUV422P:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height +
                   pstImg->au32Stride[1] * pstImg->u32Height * 2;

        break;
    case CVE_IMAGE_TYPE_S8C2_PACKAGE:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * 2;
        break;
    case CVE_IMAGE_TYPE_S8C2_PLANAR:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * 2;
        break;
    case CVE_IMAGE_TYPE_S16C1:
    case CVE_IMAGE_TYPE_U16C1:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * sizeof(AML_U16);
        break;
    case CVE_IMAGE_TYPE_U8C3_PACKAGE:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * 3;
        break;
    case CVE_IMAGE_TYPE_U8C3_PLANAR:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * 3;
        break;
    case CVE_IMAGE_TYPE_S32C1:
    case CVE_IMAGE_TYPE_U32C1:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * sizeof(AML_U32);
        break;
    case CVE_IMAGE_TYPE_S64C1:
    case CVE_IMAGE_TYPE_U64C1:
        mem_size = pstImg->au32Stride[0] * pstImg->u32Height * sizeof(AML_U64);
        break;
    default:
        CVE_ERR_TRACE("Unknow image type[%d]\n", pstImg->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
        break;
    }

    if (cve_check_mem(pstImg->au64PhyAddr[0], mem_size, aligned)) {
        return AML_ERR_CVE_ILLEGAL_PARAM;
    }

    return 0;
}

static int cve_check_image_stride(CVE_IMAGE_T *pstImg)
{
    unsigned int stride = 0;

    if (NULL == pstImg) {
        CVE_ERR_TRACE("pstImg is null\n");
        return AML_FAILURE;
    }

    stride = CVE_ALIGN_UP(pstImg->u32Width, CVE_ALIGN);

    switch (pstImg->enType) {
    case CVE_IMAGE_TYPE_U8C1:
    case CVE_IMAGE_TYPE_S8C1:
    case CVE_IMAGE_TYPE_S16C1:
    case CVE_IMAGE_TYPE_U16C1:
    case CVE_IMAGE_TYPE_S32C1:
    case CVE_IMAGE_TYPE_U32C1:
    case CVE_IMAGE_TYPE_S64C1:
    case CVE_IMAGE_TYPE_U64C1:
        if (pstImg->au32Stride[0] != stride) {
            CVE_ERR_TRACE("Invalid stride0 [%d], expected stride0 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[0], stride, pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_IMAGE_TYPE_YUV420SP:
    case CVE_IMAGE_TYPE_YUV422SP:
    case CVE_IMAGE_TYPE_S8C2_PACKAGE:
    case CVE_IMAGE_TYPE_S8C2_PLANAR:
        if (pstImg->au32Stride[0] != stride) {
            CVE_ERR_TRACE("Invalid stride0 [%d], expected stride0 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[0], stride, pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        if (pstImg->au32Stride[1] != stride) {
            CVE_ERR_TRACE("Invalid stride1 [%d], expected stride1 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[1], stride, pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_IMAGE_TYPE_YUV420P:
    case CVE_IMAGE_TYPE_YUV422P:
        if (pstImg->au32Stride[0] != stride) {
            CVE_ERR_TRACE("Invalid stride0 [%d], expected stride0 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[0], stride, pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        if (pstImg->au32Stride[1] != CVE_ALIGN_UP(pstImg->u32Width, CVE_ALIGN)) {
            CVE_ERR_TRACE("Invalid stride1 [%d], expected stride1 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[1], CVE_ALIGN_UP(pstImg->u32Width, CVE_ALIGN),
                          pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        if (pstImg->au32Stride[2] != pstImg->au32Stride[1]) {
            CVE_ERR_TRACE("Invalid stride2 [%d], expected stride2 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[2], pstImg->au32Stride[1], pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    case CVE_IMAGE_TYPE_U8C3_PACKAGE:
    case CVE_IMAGE_TYPE_U8C3_PLANAR:
        if (pstImg->au32Stride[0] != stride) {
            CVE_ERR_TRACE("Invalid stride0 [%d], expected stride0 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[0], stride, pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        if (pstImg->au32Stride[1] != stride) {
            CVE_ERR_TRACE("Invalid stride1 [%d], expected stride1 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[1], stride, pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        if (pstImg->au32Stride[2] != stride) {
            CVE_ERR_TRACE("Invalid stride2 [%d], expected stride2 is equal to %d, width [%d]\n",
                          pstImg->au32Stride[2], stride, pstImg->u32Width);
            return AML_ERR_CVE_ILLEGAL_PARAM;
        }
        break;
    default:
        CVE_ERR_TRACE("Unknow image type[%d]\n", pstImg->enType);
        return AML_ERR_CVE_ILLEGAL_PARAM;
        break;
    }

    return 0;
}

static bool check_cve_reg_map(void)
{

    if (cve_regs_map) {
        return true;
    }

    CVE_ERR_TRACE("cve reg not map\n");

    return false;
}

static int cve_reg_init()
{
    unsigned int base_addr;

    base_addr = CVE_REG_BASE << 2;

    if (!cve_regs_map) {
        cve_regs_map = ioremap(base_addr, CVE_REG_SIZE);
    }

    return 0;
}

static unsigned int cve_reg_read(unsigned int reg)
{
    unsigned int addr = 0;
    unsigned int val = 0;

    addr = CVE_REG_ADDR_OFFSET(reg);
    if (check_cve_reg_map()) {
        val = ioread32(cve_regs_map + addr);
    }

    return val;
}

static void cve_reg_write(unsigned int reg, unsigned int val)
{
    unsigned int addr = 0;

    addr = CVE_REG_ADDR_OFFSET(reg);
    if (check_cve_reg_map()) {
        iowrite32(val, cve_regs_map + addr);
    }

    return;
}

static inline unsigned int cve_reg_get_bits(unsigned int reg, const unsigned int start,
                                            const unsigned int len)
{
    unsigned int val;

    val = (cve_reg_read(reg) >> (start)) & ((1L << (len)) - 1);
    return val;
}

static inline void cve_reg_bits_set(unsigned int reg, const unsigned int value,
                                    const unsigned int start, const unsigned int len)
{
    cve_reg_write(reg, ((cve_reg_read(reg) & ~(((1L << (len)) - 1) << (start))) |
                        (((value) & ((1L << (len)) - 1)) << (start))));
}
#if 0
static unsigned int cve_task_id_get()
{
    cve_reg_get_bits(CVE_ID_STA_REG1, 0, 16);

    return 0;
}
#endif
static void cve_cq0_int_enable(void)
{
    cve_reg_bits_set(CVE_INTR_MASKN_REG0, 1, 0, 1);

    return;
}

static void cve_cq0_int_disable(void)
{
    cve_reg_bits_set(CVE_INTR_MASKN_REG0, 0, 0, 1);

    return;
}

static void cve_cq0_int_clear(void)
{
    cve_reg_bits_set(CVE_INTR_STAT_WRITE_REG0, 0, 0, 1);
    cve_reg_bits_set(CVE_CQ_REG2, 0, 24, 1);

    return;
}

static void cve_cq0_function_enable(void)
{
    cve_reg_bits_set(CVE_CQ_REG2, 1, 26, 1);

    return;
}

static void cve_cq0_function_disable(void)
{
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG1, 2, 0, 2);
    cve_reg_bits_set(CVE_CQ_REG2, 0, 26, 1);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG1, 0, 0, 2);

    return;
}

static void cve_op_run_time_cycle_enable(void)
{
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 1, 2, 1);
}

#if 0
static void cve_sys_timeout_enable(unsigned int timeout)
{
    cve_reg_bits_set(CVE_TIMEOUT_REG0, 1, 0, 1);
    cve_reg_bits_set(CVE_TIMEOUT_REG1, timeout, 0, 32);

    return;
}

static void cve_sys_timeout_disable(void)
{
    cve_reg_bits_set(CVE_TIMEOUT_REG0, 0, 0, 1);

    return;
}
#endif
static void cve_ute_stat_enable(unsigned int set_total_cycle)
{
    cve_reg_bits_set(CVE_UTE_REG0, set_total_cycle, 2, 30);
    cve_reg_bits_set(CVE_UTE_REG0, 1, 0, 2);
}

static unsigned int cve_get_ute_cycle_in_total(void)
{
    return cve_reg_get_bits(CVE_UTE_REG1, 0, 30);
}

static unsigned int cve_int_status_get(void)
{
    unsigned int value;

    value = cve_reg_read(CVE_STATUS_REG);

    return value;
}

static bool cve_is_timeout_int(unsigned int status)
{
    if (status) {
        return true;
    }

    return false;
}

static void cve_set_reset(void)
{
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 1, 0, 1);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 0, 1);

    return;
}

#if 0
static void cve_clk_gate_enable(void)
{
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 4, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 6, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 8, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 10, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 12, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 14, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 16, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 18, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 20, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 22, 2);
    cve_reg_bits_set(CVE_TOP_HW_CTRL_REG0, 0, 24, 2);

    return;
}
#endif

static int aml_cve_probe(struct platform_device *pf_dev)
{
    void __iomem *regs;
    struct clk *clk_gate;
    struct clk *mux_gate = NULL;
    struct clk *mux_sel = NULL;
    int ret = -1, clk_rate = 0;

    memset(&cve_context, 0, sizeof(cve_context_t));
    sprintf(cve_device.name, AML_MOD_CVE);
    cve_device.dev_num = 0;
    ret = register_chrdev(cve_device.dev_num, cve_device.name, &g_cve_fops);
    if (ret <= 0) {
        dev_err(&pf_dev->dev, "register cve device error\n");
        goto module_init_error;
    }
    cve_device.major = ret;
    cve_device.cve_class = class_create(THIS_MODULE, cve_device.name);
    if (IS_ERR(cve_device.cve_class)) {
        dev_err(&pf_dev->dev, "create cve class error\n");
        goto destroy_cve_chrdev;
    }

    cve_device.cve_pdev = device_create(cve_device.cve_class, NULL, MKDEV(cve_device.major, 0),
                                        NULL, cve_device.name);
    if (IS_ERR_OR_NULL(cve_device.cve_pdev)) {
        dev_err(&pf_dev->dev, "create cve device error\n");
        ret = AML_ERR_CVE_NOMEM;
        goto destroy_cve_class;
    }
    cve_irq = platform_get_irq_byname(pf_dev, "cve");
    if (cve_irq <= 0) {
        dev_err(&pf_dev->dev, "cannot find cve IRQ\n");
    }

    cve_pdev = &pf_dev->dev;
    ret = cve_platform_module_init();
    if (ret) {
        dev_err(&pf_dev->dev, "init cve module error\n");
        goto destroy_cve_device;
    }

    atomic_set(&cve_user_ref, 0);

    spin_lock_init(&cve_spinlock);

    sema_init(&cve_context.cve_sema, 1);
    cve_device.proc_node_entry = proc_create_data("cve", 0644, NULL, &cve_proc_file_ops, NULL);
    if (cve_device.proc_node_entry == NULL) {
        dev_err(&pf_dev->dev, "cve create proc failed!\n");
        goto destroy_cve_device;
    }

    cve_device.mem = platform_get_resource_byname(pf_dev, IORESOURCE_MEM, "cve");
    regs = devm_ioremap_resource(&pf_dev->dev, cve_device.mem);
    if (IS_ERR(regs))
        return PTR_ERR(regs);

    cve_regs_map = regs;
    ret = of_property_read_u32(pf_dev->dev.of_node, "clk-rate", &clk_rate);
    if (ret < 0)
        clk_rate = 800000000;

    mux_gate = devm_clk_get(&pf_dev->dev, "mux_gate");
    if (IS_ERR(mux_gate))
        dev_err(&pf_dev->dev, "cannot get cve mux_gate\n");

    mux_sel = devm_clk_get(&pf_dev->dev, "mux_sel");
    if (IS_ERR(mux_gate))
        dev_err(&pf_dev->dev, "cannot get gdc mux_sel\n");

    clk_set_parent(mux_sel, mux_gate);

    clk_gate = devm_clk_get(&pf_dev->dev, "clk_gate");
    if (IS_ERR(clk_gate)) {
        dev_err(&pf_dev->dev, "cannot get cve clk_gate\n");
    } else {
        clk_set_rate(clk_gate, clk_rate);
        clk_prepare_enable(clk_gate);
        ret = clk_get_rate(clk_gate);
    }
    ret = 0;

    goto module_init_error;

destroy_cve_device:
    device_destroy(cve_device.cve_class, MKDEV(cve_device.major, 0));
destroy_cve_class:
    class_destroy(cve_device.cve_class);
destroy_cve_chrdev:
    unregister_chrdev(cve_device.major, cve_device.name);
module_init_error:

    return ret;
}

static int aml_cve_remove(struct platform_device *pf_dev)
{
    cve_platform_module_exit();
    remove_proc_entry("cve", NULL);
    device_destroy(cve_device.cve_class, MKDEV(cve_device.major, 0));
    class_destroy(cve_device.cve_class);
    unregister_chrdev(cve_device.major, cve_device.name);
    cve_regs_map = NULL;

    return 0;
}

static const struct of_device_id amlogic_cve_dt_match[] = {{
                                                               .compatible = "amlogic, cve",
                                                           },
                                                           {}};

static struct platform_driver aml_cve_driver = {.probe = aml_cve_probe,
                                                .remove = aml_cve_remove,
                                                .driver = {
                                                    .name = "aml_cve",
                                                    .owner = THIS_MODULE,
                                                    .of_match_table = amlogic_cve_dt_match,
                                                }};

static int __init aml_cve_module_init(void)
{
    int ret = -1;

    if (platform_driver_register(&aml_cve_driver) < 0) {
        printk("register platform driver error\n");
        return ret;
    }
    return 0;
}

static void aml_cve_module_exit(void)
{
    platform_driver_unregister(&aml_cve_driver);
}

module_init(aml_cve_module_init);
module_exit(aml_cve_module_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_VERSION("1.0");
MODULE_LICENSE("GPL v2");
