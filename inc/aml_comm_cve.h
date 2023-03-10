// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (C) 2019-2024 Amlogic, Inc. All rights reserved.
 */

#ifndef __AML_COMM_CVE_H__
#define __AML_COMM_CVE_H__

#include "aml_common.h"
#include "aml_type.h"

#define CVE_INVALID_POOL_HANDLE (-1U)

typedef unsigned char AML_U0Q8;
typedef unsigned char AML_U1Q7;
typedef unsigned char AML_U5Q3;
typedef unsigned char AML_U3Q5;

typedef unsigned short AML_U0Q16;
typedef unsigned short AML_U4Q12;
typedef unsigned short AML_U6Q10;
typedef unsigned short AML_U8Q8;
typedef unsigned short AML_U9Q7;
typedef unsigned short AML_U10Q6;
typedef unsigned short AML_U12Q4;
typedef unsigned short AML_U14Q2;
typedef unsigned short AML_U5Q11;
typedef unsigned short AML_U1Q15;
typedef unsigned short AML_U2Q14;
typedef unsigned short AML_U3Q7;
typedef unsigned short AML_U8Q4;

typedef AML_U6Q10 AML_UFP16;

typedef short AML_S9Q7;
typedef short AML_S14Q2;
typedef short AML_S1Q15;

typedef unsigned int AML_U22Q10;
typedef unsigned int AML_U25Q7;
typedef unsigned int AML_U21Q11;
typedef unsigned int AML_U14Q18;
typedef unsigned int AML_U8Q24;
typedef unsigned int AML_U4Q28;

typedef int AML_S12Q7;
typedef int AML_S16Q16;
typedef int AML_S14Q18;
typedef int AML_S20Q12;

typedef int AML_S24Q8;
typedef int AML_S21Q3;

typedef unsigned short AML_U8Q4F4;

typedef enum {
    CVE_IMAGE_TYPE_U8C1 = 0x0,
    CVE_IMAGE_TYPE_S8C1 = 0x1,

    CVE_IMAGE_TYPE_YUV420SP = 0x2,
    CVE_IMAGE_TYPE_YUV422SP = 0x3,
    CVE_IMAGE_TYPE_YUV420P = 0x4,
    CVE_IMAGE_TYPE_YUV422P = 0x5,

    CVE_IMAGE_TYPE_S8C2_PACKAGE = 0x6,
    CVE_IMAGE_TYPE_S8C2_PLANAR = 0x7,

    CVE_IMAGE_TYPE_S16C1 = 0x8,
    CVE_IMAGE_TYPE_U16C1 = 0x9,

    CVE_IMAGE_TYPE_U8C3_PACKAGE = 0xa,
    CVE_IMAGE_TYPE_U8C3_PLANAR = 0xb,

    CVE_IMAGE_TYPE_S32C1 = 0xc,
    CVE_IMAGE_TYPE_U32C1 = 0xd,

    CVE_IMAGE_TYPE_S64C1 = 0xe,
    CVE_IMAGE_TYPE_U64C1 = 0xf,

    CVE_IMAGE_TYPE_BUTT

} CVE_IMAGE_TYPE_E;

typedef struct {
    int dmafd;
    AML_U64 au64PhyAddr[3];
    AML_U64 au64VirAddr[3];
    AML_U32 au32Stride[3];
    AML_U32 u32Width;
    AML_U32 u32Height;
    CVE_IMAGE_TYPE_E enType;
} CVE_IMAGE_T;

typedef CVE_IMAGE_T CVE_SRC_IMAGE_T;
typedef CVE_IMAGE_T CVE_DST_IMAGE_T;

typedef enum {
    CVE_RAW_MODE_RAW6 = 0x0,
    CVE_RAW_MODE_RAW7 = 0x1,
    CVE_RAW_MODE_RAW8 = 0x2,
    CVE_RAW_MODE_RAW10 = 0x3,
    CVE_RAW_MODE_RAW12 = 0x4,
    CVE_RAW_MODE_RAW14 = 0x5,

    CVE_RAW_MODE_BUTT
} CVE_RAW_MODE_E;

typedef struct {
    int dmafd;
    AML_U64 u64PhyAddr;
    AML_U64 u64VirAddr;
    AML_U32 u32Stride;
    AML_U32 u32Width;
    AML_U32 u32Height;
    CVE_RAW_MODE_E enMode;
} CVE_RAW_T;

typedef CVE_RAW_T CVE_SRC_RAW_T;
typedef CVE_RAW_T CVE_DST_RAW_T;

typedef struct {
    int dmafd;
    AML_U64 u64PhyAddr;
    AML_U64 u64VirAddr;
    AML_U32 u32Size;
} CVE_MEM_INFO_T;
typedef CVE_MEM_INFO_T CVE_SRC_MEM_INFO_T;
typedef CVE_MEM_INFO_T CVE_DST_MEM_INFO_T;

typedef struct {
    int dmafd;
    AML_U64 u64PhyAddr;
    AML_U64 u64VirAddr;

    AML_U32 u32Stride;
    AML_U32 u32Width;
    AML_U32 u32Height;

    AML_U32 u32Reserved;
} CVE_DATA_T;
typedef CVE_DATA_T CVE_SRC_DATA_T;
typedef CVE_DATA_T CVE_DST_DATA_T;

typedef union {
    AML_S8 s8Val;
    AML_U8 u8Val;
} CVE_8BIT_U;

typedef struct {
    AML_U16 u16X;
    AML_U16 u16Y;
} CVE_POINT_U16_T;

typedef struct {
    AML_U16 s16X;
    AML_U16 s16Y;
} CVE_POINT_S16_T;

typedef struct {
    AML_S12Q7 s12q7X;
    AML_S12Q7 s12q7Y;
} CVE_POINT_S12Q7_T;

typedef struct {
    AML_U16 u16X;
    AML_U16 u16Y;
    AML_U16 u16Width;
    AML_U16 u16Height;
} CVE_RECT_U16_T;

typedef enum {
    ERR_CVE_SYS_TIMEOUT = 0x40,
    ERR_CVE_QUERY_TIMEOUT = 0x41,
    ERR_CVE_OPEN_FILE = 0x42,
    ERR_CVE_READ_FILE = 0x43,
    ERR_CVE_WRITE_FILE = 0x44,
    ERR_CVE_BUS_ERR = 0x45,

    ERR_CVE_BUTT
} EN_CVE_ERR_CODE_E;

#define AML_ERR_CVE_INVALID_DEVID AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_INVALID_DEVID)
#define AML_ERR_CVE_INVALID_CHNID AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_INVALID_CHNID)
#define AML_ERR_CVE_ILLEGAL_PARAM AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_ILLEGAL_PARAM)
#define AML_ERR_CVE_EXIST AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_EXIST)
#define AML_ERR_CVE_UNEXIST AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_UNEXIST)
#define AML_ERR_CVE_NULL_PTR AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_NULL_PTR)
#define AML_ERR_CVE_NOT_CONFIG AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_NOT_CONFIG)
#define AML_ERR_CVE_NOT_SURPPORT AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_NOT_SUPPORT)
#define AML_ERR_CVE_NOT_PERM AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_NOT_PERM)
#define AML_ERR_CVE_NOMEM AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_NOMEM)
#define AML_ERR_CVE_NOBUF AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_NOBUF)
#define AML_ERR_CVE_BUF_EMPTY AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_BUF_EMPTY)
#define AML_ERR_CVE_BUF_FULL AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_BUF_FULL)
#define AML_ERR_CVE_NOTREADY AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_SYS_NOTREADY)
#define AML_ERR_CVE_BADADDR AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_BADADDR)
#define AML_ERR_CVE_BUSY AML_DEF_ERR(AML_ERR_LEVEL_ERROR, AML_ERR_BUSY)
#define AML_ERR_CVE_SYS_TIMEOUT AML_DEF_ERR(AML_ERR_LEVEL_ERROR, ERR_CVE_SYS_TIMEOUT)
#define AML_ERR_CVE_QUERY_TIMEOUT AML_DEF_ERR(AML_ERR_LEVEL_ERROR, ERR_CVE_QUERY_TIMEOUT)

#endif /* __AML_COMM_CVE_H__ */
