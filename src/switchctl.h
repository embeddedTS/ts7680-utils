/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019-2022 Technologic Systems, Inc. dba embeddedTS */

#ifndef __SWITCHCTL_H__
#define __SWITCHCTL_H__
int phy_init(void);
int phy_write(unsigned long phy, unsigned long reg, unsigned short data);
int phy_read(unsigned long phy, unsigned long reg,
  volatile unsigned short *data);
#endif
