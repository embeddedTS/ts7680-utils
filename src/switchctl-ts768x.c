/*  Copyright 2015, Unpublished Work of Technologic Systems
*  All Rights Reserved.
*
*  THIS WORK IS AN UNPUBLISHED WORK AND CONTAINS CONFIDENTIAL,
*  PROPRIETARY AND TRADE SECRET INFORMATION OF TECHNOLOGIC SYSTEMS.
*  ACCESS TO THIS WORK IS RESTRICTED TO (I) TECHNOLOGIC SYSTEMS EMPLOYEES
*  WHO HAVE A NEED TO KNOW TO PERFORM TASKS WITHIN THE SCOPE OF THEIR
*  ASSIGNMENTS  AND (II) ENTITIES OTHER THAN TECHNOLOGIC SYSTEMS WHO
*  HAVE ENTERED INTO  APPROPRIATE LICENSE AGREEMENTS.  NO PART OF THIS
*  WORK MAY BE USED, PRACTICED, PERFORMED, COPIED, DISTRIBUTED, REVISED,
*  MODIFIED, TRANSLATED, ABRIDGED, CONDENSED, EXPANDED, COLLECTED,
*  COMPILED,LINKED,RECAST, TRANSFORMED, ADAPTED IN ANY FORM OR BY ANY
*  MEANS,MANUAL, MECHANICAL, CHEMICAL, ELECTRICAL, ELECTRONIC, OPTICAL,
*  BIOLOGICAL, OR OTHERWISE WITHOUT THE PRIOR WRITTEN PERMISSION AND
*  CONSENT OF TECHNOLOGIC SYSTEMS . ANY USE OR EXPLOITATION OF THIS WORK
*  WITHOUT THE PRIOR WRITTEN CONSENT OF TECHNOLOGIC SYSTEMS  COULD
*  SUBJECT THE PERPETRATOR TO CRIMINAL AND CIVIL LIABILITY.
*/
/* For more information, see switchctl.c
*/

#include <assert.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "switchctl.h"

volatile unsigned int *mxgpioregs;

int phy_init(void) 
{
	int devmem;

	devmem = open("/dev/mem", O_RDWR|O_SYNC);
	assert(devmem != -1);
	mxgpioregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
	  devmem, 0x80018000);

	/* Set up MDIO/MDC as GPIO */
	mxgpioregs[0x184/4] = 0xf;

	return 0;
}

static int inline MDIO_RD(void) {return ((mxgpioregs[0x940/4] >> 1) & 0x1);}
static int inline MDC_RD(void) {return ((mxgpioregs[0x940/4] >> 0) & 0x1);}
static void inline MDIO_OUT(void) {mxgpioregs[0xb44/4] = 0x2;}
static void inline MDIO_IN(void) {mxgpioregs[0xb48/4] = 0x2;}
static void inline MDC_OUT(void) {mxgpioregs[0xb44/4] = 0x1;}
static void inline MDC_IN(void) {mxgpioregs[0xb48/4] = 0x1;}
static void inline MDIO_HI(void) {mxgpioregs[0x744/4] = 0x2;}
static void inline MDIO_LO(void) {mxgpioregs[0x748/4] = 0x2;}
static void inline MDC_HI(void) {mxgpioregs[0x744/4] = 0x1;}
static void inline MDC_LO(void) {mxgpioregs[0x748/4] = 0x1;}

int phy_write(unsigned long phy, unsigned long reg, unsigned short data) {
	int x;
	unsigned int b;

	MDIO_OUT();
	MDC_OUT();

	MDC_LO();
	MDIO_HI();

	/* preamble, toggle clock high then low */
	for(x=0; x < 32; x++) {
		MDC_HI();
		MDC_LO();
	}
	/* preamble ends with MDIO high and MDC low */

	/* start bit followed by write bit */
	for(x=0; x < 2; x++) {
		MDIO_LO();
		MDC_HI();
		MDC_LO();
		MDIO_HI();
		MDC_HI();
		MDC_LO();
	}
	/* ends with MDIO high and MDC low */

	/* send the PHY address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (phy & b)
		  {MDIO_HI();}
		else
		  {MDIO_LO();}

		MDC_HI();
		MDC_LO();

		b >>= 1;
	}
	/* ends with MDC low, MDIO indeterminate */

	/* send the register address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (reg & b)
		  {MDIO_HI();}
		else
		  {MDIO_LO();}

		MDC_HI();
		MDC_LO();

		b >>= 1;
	}

	/* ends with MDC low, MDIO indeterminate */

	/* clock a one, then clock a zero */
	MDIO_HI();
	MDC_HI();
	MDC_LO();

	MDIO_LO();
	MDC_HI();
	MDC_LO();

	/* send the data, starting with MSB */
	b = 0x8000;
	for(x=0; x < 16; x++) {
		if (data & b)
		 { MDIO_HI();}
		else
		  {MDIO_LO();}

		MDC_HI();
		MDC_LO();

		b >>= 1;
	}

	MDIO_IN();
	MDC_IN();

	return 0;
}

int phy_read(unsigned long phy, unsigned long reg,
  volatile unsigned short *data) {
	int x, d;
	unsigned int a,b;

	d = 0;

	MDC_OUT();
	MDIO_OUT();

	MDC_LO();
	MDIO_HI();

	/* preamble, toggle clock high then low */
	for(x=0; x < 32; x++) {
		MDC_HI();
		MDC_LO();
	}
	/* preamble ends with MDIO high and MDC low */

	/* start bit */
	MDIO_LO(); MDC_HI(); MDC_LO();
	MDIO_HI(); MDC_HI(); MDC_LO();
	MDC_HI();  MDC_LO();
	MDIO_LO(); MDC_HI(); MDC_LO();

	/* send the PHY address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (phy & b)
		  {MDIO_HI();}
		else
		  {MDIO_LO();}

		MDC_HI();
		MDC_LO();

		b >>= 1;
		}
		/* ends with MDC low, MDIO indeterminate */

	/* send the register address, starting with MSB */
	b = 0x10;
	for(x=0; x < 5; x++) {
		if (reg & b)
		  {MDIO_HI();}
		else
		  {MDIO_LO();}

		MDC_HI();
		MDC_LO();

		b >>= 1;
	}

	MDIO_IN();
	/* ends with MDC low, MDIO indeterminate */

	/* another clock */
	MDC_HI();
	MDC_LO();

	/* read the data, starting with MSB */
	b = 0x8000;
	for(x=0; x < 16; x++) {
		MDC_HI();
		a = MDIO_RD();

		if (a & 1)
		d |= b;

		MDC_LO();

		b >>= 1;
	}

	MDIO_IN();
	MDC_IN();

	*data = d;
	return 0;
}

