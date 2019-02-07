/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019, Technologic Systems Inc. */

#ifndef _CROSSBAR_TS7682_H_
#define _CROSSBAR_TS7682_H_
#include "fpga.h"

struct cbarpin ts7682_outputs[] = {
	{ 20, "DC_TXD" },
	{ 21, "CELL_TXD" },
	{ 22, "CELL_RTS" },
	{ 23, "RS_485_A_TXD" },
	{ 24, "RS_485_A_TXEN" },
	{ 25, "RS_485_B_TXD" },
	{ 26, "RS_485_B_TXEN" },
	{ 27, "BT_RXD" },
	{ 28, "BT_CTS" },
	{ 29, "UART0_RXD" },
	{ 30, "UART0_CTS" },
	{ 31, "UART1_RXD" },
	{ 32, "UART2_RXD" },
	{ 33, "UART3_RXD" },
	{ 34, "UART4_RXD" },
	{ 0, 0 },
};

struct cbarpin ts7682_inputs[] = {
	{ 0, "UNCHANGED" },
	{ 1, "DC_RXD" },
	{ 2, "CELL_RXD" },
	{ 3, "CELL_CTS" },
	{ 4, "RS_485_A_RXD" },
	{ 5, "RS_485_B_RXD" },
	{ 6, "BT_TXD" },
	{ 7, "BT_RTS" },
	{ 8, "UART0_TXD" },
	{ 9, "UART0_TXEN" },
	{ 10, "UART0_RTS" },
	{ 11, "UART1_TXD" },
	{ 12, "UART1_TXEN" },
	{ 13, "UART2_TXD" },
	{ 14, "UART2_TXEN" },
	{ 15, "UART3_TXD" },
	{ 16, "UART3_TXEN" },
	{ 17, "UART4_TXD" },
	{ 18, "UART4_TXEN" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 0, "RESERVED" },
	{ 31, "GPIO" },
	{ 32, "DIO_IN_0" },
	{ 33, "DIO_IN_1" },
	{ 34, "DIO_IN_2" },
	{ 35, "DIO_IN_3" },
	{ 36, "DIO_IN_4" },
	{ 37, "DIO_IN_5" },
	{ 0, 0 },
};

#endif //_CROSSBAR_TS7682_H_
