/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019, Technologic Systems Inc. */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <linux/types.h>
#include <math.h>

#include "fpga.h"
#include "gpiolib.h"
#include "crossbar-ts7680.h"
#include "crossbar-ts7682.h"

static int twifd;

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;

int get_model()
{
	FILE *proc;
	char mdl[256];
	char *ptr;

	proc = fopen("/proc/device-tree/model", "r");
	if (!proc) {
		perror("model");
		return 0;
	}
	fread(mdl, 256, 1, proc);
	ptr = strstr(mdl, "TS-");
	return strtoull(ptr+3, NULL, 16);
}

void autotx_bitstoclks(int bits, int baud, uint32_t *cnt1, uint32_t *cnt2)
{
	bits *= 10;
	*cnt1 = (2500000/(baud/(bits-5)));
	*cnt2 = (2500000/(baud/5));
}

void auto485_en(int uart, int baud, char *mode)
{
	int symsz, i;
	uint32_t cnt1, cnt2;

	if(mode != NULL) {
		if(mode[0] == '8') symsz = 10;
		else if(mode[0] == '7') symsz = 9;
		else if(mode[0] == '6') symsz = 8;
		else if(mode[0] == '5') symsz = 7;
		if (mode[1] == 'e' || mode[1] == 'o') symsz++;
		if (mode[2] == '2') symsz++;
		printf(
		  "Setting Auto TXEN for %d baud, %d bits per symbol (%s)\n",
		  baud, symsz, mode);
	} else {
		/* Assume 8n1 */
		symsz = 10;
		printf(
		  "Setting Auto TXEN for %d baud, 10 bits per symbol (8n1)\n",
		  baud);
	}

	autotx_bitstoclks(symsz, baud, &cnt1, &cnt2);
	i = (0x36 + (uart * 6));
	fpoke8(twifd, i++, (uint8_t)((cnt1 & 0xff0000) >> 16));
	fpoke8(twifd, i++, (uint8_t)((cnt1 & 0xff00) >> 8));
	fpoke8(twifd, i++, (uint8_t)(cnt1 & 0xff));
	fpoke8(twifd, i++, (uint8_t)((cnt2 & 0xff0000) >> 16));
	fpoke8(twifd, i++, (uint8_t)((cnt2 & 0xff00) >> 8));
	fpoke8(twifd, i++, (uint8_t)(cnt2 & 0xff));

}


void usage(char **argv) {
	fprintf(stderr,
	  "%s\n\n"
	  "Usage: %s [OPTIONS] ...\n"
	  "Technologic Systems I2C FPGA Utility\n"
	  "\n"
	  "  -i, --info             Display board info\n"
	  "  -m, --addr <address>   Sets up the address for a peek/poke\n"
	  "  -v, --poke <value>     Writes the value to the specified address\n"
	  "  -t, --peek             Reads from the specified address\n"
	  "  -o, --mode <8n1>       Used with -a, sets mode like '8n1', '7e2'\n"
	  "  -x, --baud <speed>     Used with -a, sets baud rate for auto485\n"
	  "  -a, --autotxen <uart>  Enables autotxen for supported CPU UARTs\n"
	  "                           Uses baud/mode if set or reads the\n"
	  "                           current configuration of that uart\n"
	  "  -c, --dump             Prints out the crossbar configuration\n"
	  "  -g, --get              Print crossbar for use in eval\n"
	  "  -s, --set              Read environment for crossbar changes\n"
       	  "  -q, --showall          Print all possible FPGA crossbar I/O\n"
	  "  -e, --cputemp          Print CPU internal temperature\n"
	  "  -1, --modbuspoweron    Enable VIN to MODBUS port\n"
	  "  -Z, --modbuspoweroff   Gate off VIN to MODBUS port\n"
	  "  -p, --getmac           Display ethernet MAC address\n"
	  "  -l, --setmac=MAC       Set ethernet MAC address\n"
	  "  -b, --dac0 <PWMval>    Set DAC0 output to <PWMval>\n"
	  "  -d, --dac1 <PWMval>    Set DAC1 output to <PWMval>\n"
	  "  -f, --dac2 <PWMval>    Set DAC2 output to <PWMval>\n"
	  "  -j, --dac3 <PWMval>    Set DAC3 output to <PWMval>\n"
	  "  -h, --help             This message\n"
	  "\n",
	  copyright, argv[0]
	);
}

int main(int argc, char **argv)
{
	int c, i;
	uint16_t addr = 0x0;
	int opt_addr = 0;
	int opt_poke = 0, opt_peek = 0, opt_auto485 = -1;
	int opt_set = 0, opt_get = 0, opt_dump = 0;
	int opt_info = 0, opt_setmac = 0, opt_getmac = 0;
	int opt_cputemp = 0, opt_modbuspoweron = 0, opt_modbuspoweroff = 0;
	int opt_dac0 = 0, opt_dac1 = 0, opt_dac2 = 0, opt_dac3 = 0;
	char *opt_mac = NULL;
	int baud = 0;
	int model;
	uint8_t pokeval = 0;
	char *uartmode = 0;
	struct cbarpin *cbar_inputs, *cbar_outputs;
	int cbar_size, cbar_mask;

	static struct option long_options[] = {
		{ "addr", 1, 0, 'm' },
		{ "address", 1, 0, 'm' },
		{ "poke", 1, 0, 'v' },
		{ "peek", 0, 0, 't' },
		{ "pokef", 1, 0, 'v' },
		{ "peekf", 0, 0, 't' },
		{ "baud", 1, 0, 'x' },
		{ "mode", 1, 0, 'o' },
		{ "autotxen", 1, 0, 'a' },
		{ "get", 0, 0, 'g' },
		{ "set", 0, 0, 's' },
		{ "dump", 0, 0, 'c' },
		{ "showall", 0, 0, 'q' },
		{ "getmac", 0, 0, 'p' },
		{ "setmac", 1, 0, 'l' },
		{ "cputemp", 0, 0, 'e' },
		{ "modbuspoweron", 0, 0, '1' },
		{ "modbuspoweroff", 0, 0, 'Z' },
		{ "info", 0, 0, 'i' },
		{ "dac0", 1, 0, 'b' },
		{ "dac1", 1, 0, 'd' },
		{ "dac2", 1, 0, 'f' },
		{ "dac3", 1, 0, 'j' },
		{ "help", 0, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	model = get_model();
	if(model == 0x7680) {
		cbar_inputs = ts7680_inputs;
		cbar_outputs = ts7680_outputs;
		cbar_size = 6;
		cbar_mask = 3;
	} else if(model == 0x7682) {
		cbar_inputs = ts7682_inputs;
		cbar_outputs = ts7682_outputs;
		cbar_size = 6;
		cbar_mask = 3;
	} else {
		fprintf(stderr, "Unsupported model TS-%X\n", model);
		return 1;
	}

	while((c = getopt_long(argc, argv, "+m:v:o:x:ta:cgsqhipl:e1Zb:d:f:j:",
	  long_options, NULL)) != -1) {
		switch(c) {

		case 'i':
			opt_info = 1;
			break;
		case 'e':
			opt_cputemp = 1;
			break;
		case '1':
			opt_modbuspoweron = 1;
			break;
		case 'Z':
			opt_modbuspoweroff = 1;
			break;
		case 'm':
			opt_addr = 1;
			addr = strtoull(optarg, NULL, 0);
			break;
		case 'v':
			opt_poke = 1;
			pokeval = strtoull(optarg, NULL, 0);
			break;
		case 'o':
			uartmode = strdup(optarg);
			break;
		case 'x':
			baud = atoi(optarg);
			break;
		case 't':
			opt_peek = 1;
			break;
		case 'a':
			opt_auto485 = atoi(optarg);
			break;
		case 'g':
			opt_get = 1;
			break;
		case 's':
			opt_set = 1;
			break;
		case 'c':
			opt_dump = 1;
			break;
		case 'q':
			printf("FPGA Outputs:\n");
			for (i = 0; cbar_outputs[i].name != 0; i++) {
				printf("%s\n", cbar_outputs[i].name);
			}
			printf("\nFPGA Inputs:\n");
			for (i = 0; cbar_inputs[i].name != 0; i++) {
				printf("%s\n", cbar_inputs[i].name);
			}
			break;
		case 'l':
			opt_setmac = 1;
			opt_mac = strdup(optarg);
			break;
		case 'p':
			opt_getmac = 1;
			break;
		case 'b': //LS 1 to allow setting to 0
			opt_dac0 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
			break;
		case 'd': //LS 1 to allow setting to 0
			opt_dac1 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
			break;
		case 'f': //LS 1 to allow setting to 0
			opt_dac2 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
			break;
		case 'j': //LS 1 to allow setting to 0
			opt_dac3 = ((strtoul(optarg, NULL, 0) & 0xfff)<<1)|0x1;
			break;
		default:
			usage(argv);
			return 1;
		}
	}

	twifd = fpga_init(NULL, 0);
	if(twifd == -1) {
		perror("Can't open FPGA I2C bus");
		return 1;
	}


	if(opt_info) {
		printf("model=0x%X\n", model);
		gpio_export(44);
		printf("bootmode=0x%X\n", gpio_read(44) ? 1 : 0);
		printf("fpga_revision=0x%X\n", fpeek8(twifd, 0x7F));
	}

	if(opt_get) {
		for (i = 0; cbar_outputs[i].name != 0; i++)
		{
			uint8_t mode = fpeek8(twifd,
			  (cbar_outputs[i].addr) >> (8 - cbar_size));
			printf("%s=%s\n", cbar_outputs[i].name,
			  cbar_inputs[mode].name);
		}
	}

	if(opt_set) {
		for (i = 0; cbar_outputs[i].name != 0; i++)
		{
			char *value = getenv(cbar_outputs[i].name);
			int j;
			if(value != NULL) {
				for (j = 0; cbar_inputs[j].name != 0; j++) {
					if(strcmp(cbar_inputs[j].name, value)
					  == 0) {
						int mode = cbar_inputs[j].addr;
						uint8_t val = fpeek8(twifd,
						  cbar_outputs[i].addr);
						fpoke8(twifd,
						  cbar_outputs[i].addr,
						  (mode << (8 - cbar_size)) |
						  (val & cbar_mask));
						break;
					}
				}
				if(cbar_inputs[i].name == 0) {
					fprintf(stderr,
					  "Invalid value \"%s\" for input %s\n",
					  value, cbar_outputs[i].name);
				}
			}
		}
	}

	if(opt_dump) {
		i = 0;
		printf("%13s (DIR) (VAL) FPGA Input\n", "FPGA Pad");
		for (i = 0; cbar_outputs[i].name != 0; i++)
		{
			uint8_t value = fpeek8(twifd, cbar_outputs[i].addr);
			uint8_t mode = value >> (8 - cbar_size);
			char *dir = value & 0x1 ? "out" : "in";
			int val;

			if(value & 0x1 || cbar_size == 6) {
				val = value & 0x2 ? 1 : 0;
			} else {
				val = value & 0x4 ? 1 : 0;
			}
			printf("%13s (%3s) (%3d) %s\n",	cbar_outputs[i].name,
			  dir, val, cbar_inputs[mode].name);
		}
	}

	if(opt_modbuspoweron) {
		gpio_export(45);
		gpio_export(46);
		gpio_export(47);

		gpio_write(45, 0);
		gpio_write(47, 1);

		gpio_direction(45, 1);
		gpio_direction(46, 0);
		gpio_direction(47, 1);

		gpio_write(47, 0);
		usleep(10000);

		if(gpio_read(46)) {
			gpio_write(47, 1);
			printf("modbuspoweron=0\n");
		} else {
			gpio_write(47, 1);
			gpio_write(45, 1);
			printf("modbuspoweron=1\n");
		}

	}

	if(opt_modbuspoweroff) {
		gpio_export(45);
		gpio_write(45, 0);
		gpio_direction(45, 1);
	}

	if(opt_poke) {
		fpoke8(twifd, addr, pokeval);
	}

	if(opt_peek) {
		printf("0x%X\n", fpeek8(twifd, addr));
	}

	if(opt_auto485 > -1) {
		auto485_en(opt_auto485, baud, uartmode);
	}

	if (opt_cputemp) {
		signed int temp[2] = {0,0}, x;
		volatile unsigned int *mxlradcregs;
		int devmem;

		devmem = open("/dev/mem", O_RDWR|O_SYNC);
		assert(devmem != -1);
		mxlradcregs = (unsigned int *) mmap(0, getpagesize(),
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x80050000);

		mxlradcregs[0x148/4] = 0xFF;
		mxlradcregs[0x144/4] = 0x98; //Set to temp sense mode
		mxlradcregs[0x28/4] = 0x8300; //Enable temp sense block
		mxlradcregs[0x50/4] = 0x0; //Clear ch0 reg
		mxlradcregs[0x60/4] = 0x0; //Clear ch1 reg
		temp[0] = temp[1] = 0;

		for(x = 0; x < 10; x++) {
			/* Clear interrupts
			 * Schedule readings
			 * Poll for sample completion
			 * Pull out samples*/
			mxlradcregs[0x18/4] = 0x3;
			mxlradcregs[0x4/4] = 0x3;
			while(!((mxlradcregs[0x10/4] & 0x3) == 0x3)) ;
			temp[0] += mxlradcregs[0x60/4] & 0xFFFF;
			temp[1] += mxlradcregs[0x50/4] & 0xFFFF;
		}
		temp[0] = (((temp[0] - temp[1]) * (1012/4)) - 2730000);
		printf("internal_temp=%d.%d\n",temp[0] / 10000,
		  abs(temp[0] % 10000));

		munmap((void *)mxlradcregs, getpagesize());
		close(devmem);
	}

	if (opt_setmac) {
		/* This uses one time programmable memory. */
		unsigned int a, b, c;
		int r, devmem;
		volatile unsigned int *mxocotpregs;

		devmem = open("/dev/mem", O_RDWR|O_SYNC);
		assert(devmem != -1);

		r = sscanf(opt_mac, "%*x:%*x:%*x:%x:%x:%x",  &a,&b,&c);
		assert(r == 3); /* XXX: user arg problem */

		mxocotpregs = (unsigned int *) mmap(0, getpagesize(),
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x8002C000);

		mxocotpregs[0x08/4] = 0x200;
		mxocotpregs[0x0/4] = 0x1000;
		while(mxocotpregs[0x0/4] & 0x100) ; //check busy flag
		if(mxocotpregs[0x20/4] & (0xFFFFFF)) {
			printf("MAC address previously set, cannot set\n");
		} else {
			assert(a < 0x100);
			assert(b < 0x100);
			assert(c < 0x100);
			mxocotpregs[0x0/4] = 0x3E770000;
			mxocotpregs[0x10/4] = (a<<16|b<<8|c);
		}
		mxocotpregs[0x0/4] = 0x0;

		munmap((void *)mxocotpregs, getpagesize());
		close(devmem);
	}

	if (opt_getmac) {
		unsigned char a, b, c;
		unsigned int mac;
		int devmem;
		volatile unsigned int *mxocotpregs;

		devmem = open("/dev/mem", O_RDWR|O_SYNC);
		assert(devmem != -1);
		mxocotpregs = (unsigned int *) mmap(0, getpagesize(),
		  PROT_READ | PROT_WRITE, MAP_SHARED, devmem, 0x8002C000);

		mxocotpregs[0x08/4] = 0x200;
		mxocotpregs[0x0/4] = 0x1000;
		while(mxocotpregs[0x0/4] & 0x100) ; //check busy flag
		mac = mxocotpregs[0x20/4] & 0xFFFFFF;
		if(!mac) {
			mxocotpregs[0x0/4] = 0x0; //Close the reg first
			mxocotpregs[0x08/4] = 0x200;
			mxocotpregs[0x0/4] = 0x1013;
			while(mxocotpregs[0x0/4] & 0x100) ; //check busy flag
			mac = (unsigned short) mxocotpregs[0x150/4];
			mac |= 0x4f0000;
		}
		mxocotpregs[0x0/4] = 0x0;

		a = mac >> 16;
		b = mac >> 8;
		c = mac;

		printf("mac=00:d0:69:%02x:%02x:%02x\n", a, b, c);
		printf("shortmac=%02x%02x%02x\n", a, b, c);

		munmap((void *)mxocotpregs, getpagesize());
		close(devmem);
	}

	/* On the TS-7682, these regs are reserverd and writing/reading will
	 * have no effect.
	 */
	if(opt_dac0) {
		char buf[2];
		buf[0] = ((opt_dac0 >> 9) & 0xf);
		buf[1] = ((opt_dac0 >> 1) & 0xff);
		fpoke8(twifd, 0x2E, buf[0]);
		fpoke8(twifd, 0x2F, buf[1]);
	}

	if(opt_dac1) {
		char buf[2];
		buf[0] = ((opt_dac1 >> 9) & 0xf);
		buf[1] = ((opt_dac1 >> 1) & 0xff);
		fpoke8(twifd, 0x30, buf[0]);
		fpoke8(twifd, 0x31, buf[1]);
	}

	if(opt_dac2) {
		char buf[2];
		buf[0] = ((opt_dac2 >> 9) & 0xf);
		buf[1] = ((opt_dac2 >> 1) & 0xff);
		fpoke8(twifd, 0x32, buf[0]);
		fpoke8(twifd, 0x33, buf[1]);
	}

	if(opt_dac3) {
		char buf[2];
		buf[0] = ((opt_dac3 >> 9) & 0xf);
		buf[1] = ((opt_dac3 >> 1) & 0xff);
		fpoke8(twifd, 0x34, buf[0]);
		fpoke8(twifd, 0x35, buf[1]);
	}


	close(twifd);

	return 0;
}

