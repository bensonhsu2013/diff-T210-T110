#ifndef __ASM_ARCH_SENSOR_DATA_H_
#define __ASM_ARCH_SENSOR_DATA_H_

#include <media/v4l2-chip-ident.h>

#define REG_PIDH	0x300A
#define REG_PIDM	0x300B
#define REG_PIDL	0x300C
#define REG_PIDH_HI542	0x4

#define REG_PIDH_VALUE		0x88
#define REG_PIDM_VALUE_8820	0x20
#define REG_PIDM_VALUE_8825	0x25
#define REG_PIDM_VALUE_8850	0x50
#define REG_PIDL_VALUE		0x00
#define REG_PIDH_VALUE_HI542	0xb1
#define REG_PIDH_VALUE_5647	0x56
#define REG_PIDM_VALUE_5647	0x47

#define REG_PIDH_LSI3H5 0x0002
#define REG_PIDM_LSI3H5 0x0003
#define REG_PIDL_LSI3H5 0x0010

#define REG_PIDH_VALUE_LSI3H5 0xB2
#define REG_PIDM_VALUE_LSI3H5 0x01
#define REG_PIDL_VALUE_LSI3H5 0x00

#define REG_PIDH_LSI3H7 0x0000
#define REG_PIDM_LSI3H7 0x0001
#define REG_PIDL_LSI3H7 0x0002

#define REG_PIDH_VALUE_LSI3H7 0x30
#define REG_PIDM_VALUE_LSI3H7 0x87
#define REG_PIDL_VALUE_LSI3H7 0xa2


#define REG_PIDH_S5K4E1  0x0000
#define REG_PIDM_S5K4E1  0x0001
#define REG_PIDH_VALUE_S5K4E1    0x4E
#define REG_PIDM_VALUE_S5K4E1    0x10

#define REG_PIDH_VALUE_12830			0xc8
#define REG_PIDM_VALUE_12830			0x30
#define REG_PIDL_VALUE_12830			0xb0

#define REG_NUM_1	1
#define REG_NUM_2	2
#define REG_NUM_3	3

static int chip_ident_id[] = {
	V4L2_IDENT_LSI3H5,
	V4L2_IDENT_LSI3H7,
	V4L2_IDENT_OV5647,
	V4L2_IDENT_S5K4E1,
	V4L2_IDENT_OV12830,
	V4L2_IDENT_OV8850,
	V4L2_IDENT_OV8825,
	V4L2_IDENT_OV8820,
};

#endif
