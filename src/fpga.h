/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019-2022 Technologic Systems, Inc. dba embeddedTS */

#ifndef __FPGA_H_
#define __FPGA_H_

struct cbarpin
{
	int addr;
	char *name;
};

int fpga_init(char *path, char adr);
void fpoke8(int twifd, uint16_t addr, uint8_t value);
uint8_t fpeek8(int twifd, uint16_t addr);

#endif
