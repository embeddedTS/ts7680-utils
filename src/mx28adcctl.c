/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019, Technologic Systems Inc. */

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>


const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;

int main(int argc, char **argv) {
	volatile unsigned int *mxlradcregs;
	volatile unsigned int *mxhsadcregs;
	volatile unsigned int *mxclkctrlregs;
	unsigned int i, x;
	unsigned long long chan[8] = {0,0,0,0,0,0,0,0};
	//signed int bivolt;
	int devmem;

	devmem = open("/dev/mem", O_RDWR|O_SYNC);
	assert(devmem != -1);

	// LRADC
	mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
	  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);

	mxlradcregs[0x148/4] = 0xfffffff; //Clear LRADC6:0 assignments
	mxlradcregs[0x144/4] = 0x6543210; //Set LRDAC6:0 to channel 6:0
	mxlradcregs[0x28/4] = 0xff000000; //Set 1.8v range
	for(x = 0; x < 7; x++)
	  mxlradcregs[(0x50+(x * 0x10))/4] = 0x0; //Clear LRADCx reg

	for(x = 0; x < 10; x++) {
		mxlradcregs[0x18/4] = 0x7f; //Clear interrupt ready
		mxlradcregs[0x4/4] = 0x7f;  //Schedule conversaion of chan 6:0
		while(!((mxlradcregs[0x10/4] & 0x7f) == 0x7f)); //Wait
		for(i = 0; i < 7; i++)
		  chan[i] += (mxlradcregs[(0x50+(i * 0x10))/4] & 0xffff);
	}

	mxhsadcregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
	  devmem, 0x80002000);
	mxclkctrlregs = mmap(0, getpagesize(), PROT_READ|PROT_WRITE, MAP_SHARED,
	  devmem, 0x80040000);

	// HSADC
	// Check to see if HSADC needs to be brought out of reset first
	if(mxhsadcregs[0x0/4] & 0xC0000000) {
		mxclkctrlregs[0x154/4] = 0x70000000;
		mxclkctrlregs[0x1c8/4] = 0x8000;
		//ENGR116296 errata workaround
		mxhsadcregs[0x8/4] = 0x80000000;
		mxhsadcregs[0x0/4] =
		  ((mxhsadcregs[0x0/4] | 0x80000000) & (~0x40000000));
		mxhsadcregs[0x4/4] = 0x40000000;
		mxhsadcregs[0x8/4] = 0x40000000;
		mxhsadcregs[0x4/4] = 0x40000000;

		usleep(10);
		mxhsadcregs[0x8/4] = 0xc0000000;
	}

	mxhsadcregs[0x28/4] = 0x2000; //Clear powerdown
	mxhsadcregs[0x24/4] = 0x31;   //Set precharge and SH bypass
	mxhsadcregs[0x30/4] = 0xa;    //Set sample num
	mxhsadcregs[0x40/4] = 0x1;    //Set seq num
	mxhsadcregs[0x04/4] = 0x40000; //12bit mode

	while(!(mxhsadcregs[0x10/4] & 0x20)) {
		mxhsadcregs[0x50/4]; //Empty FIFO
	}

	mxhsadcregs[0x50/4]; //An extra read is necessary

	mxhsadcregs[0x14/4] = 0xfc000000; //Clr interrupts
	mxhsadcregs[0x4/4] = 0x1;         //Set HS_RUN
	usleep(10);
	mxhsadcregs[0x4/4] = 0x08000000;      //Start conversion
	while(!(mxhsadcregs[0x10/4] & 0x1)) ; //Wait for interrupt

	for(i = 0; i < 5; i++) {
		x = mxhsadcregs[0x50/4];
		chan[7] += ((x & 0xfff) + ((x >> 16) & 0xfff));
	}

	/* This is where value to voltage conversions would take
	 * place.  Values below are generic and can be used as a
	 * guideline.  They were derived to be within 1% error,
	 * however differences in environments, tolerance in components,
	 * and other factors may bring that error up.  Further calibration
	 * can be done to reduce this error on a per-unit basis.
	 *
	 * The math is done to multiply everything up and divide
	 * it down to the resistor network ratio.  It is done
	 * completely using ints and avoids any floating point math
	 * which has a slower calculation speed and can add in further
	 * error. The intended output units are listed with each equation.
	 *
	 * Additionally, since very large numbers are used in the
	 * example math below, it may not be possible to implement the math
	 * as-is in some real world applications.
	 *
	 * All chan[x] values include 10 samples, this needs to be
	 * divided out to get an average.
	 *
	 * TS-7682
	 *   LRADC channels 3:0
	 *     0 - 12 V inputs, each used channel must have the En. ADX bit set
	 *       in the FPGA Syscon before the channel can operate properly.
	 *     The achievable accuracy is within 5% for each channel without
	 *       further calibration.
	 *     chan[x] mV = (((chan[x] - 52) * 10000) / 3085)
	 *
	 * TS-7680
	 *   LRADC channels 3:0
	 *     PCB [Rev C] or [Rev B with R134-R137 removed]:
	 *       0-10 V in. chan[x] mV=((((chan[x]/10)*45177)*6235)/100000000);
	 *       4-20 mA in. chan[x]:
	 *         meas_mV=((((chan[x]/10)*45177)*6235)/100000000);
	 *         uA=(((meas_mV)*1000)/240)
	 *     PCB [Rev B]:
	 *       Note: R134-R137 are intended to add bipolar input to the
	 *       existing channels.  The FETs must also be removed in order to
	 *       correctly handle a negative input voltage.  The default
	 *       Rev B PCB configuration has these resistors installed.  Either
	 *       they must be removed, or the FET on each channel.  And the
	 *       math can either use the above numbers, or a tuned equation
	 *       to handle their bipolar behavior.
	 *
	 * Other i.MX28 based SBCs
	 *   LRADC channels 4:1
	 *     chan[x] = ((((chan[x]/10)*45177)/100000)*2);
	 *   LRADC channel 6
	 *     chan[x] = ((((chan[x]/10)*45177)/1000000)*33);
	 *   HSADC
	 *     chan[7] = ((((chan[7]/10)*45177)/100000)*2));
	 */


	for(x = 0; x < 7; x++) {
		printf("LRADC_ADC%d_val=%d\n", x, (unsigned int)chan[x]/10);
	}
	printf("HSADC_val=0x%x\n", (unsigned int)chan[7]/10);

	return 0;
}
