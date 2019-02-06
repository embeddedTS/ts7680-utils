/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019, Technologic Systems Inc. */

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;

#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#include "switchctl.h"

struct vtu {
	int v, vid;
	unsigned char tags[7];
	unsigned char forceVID[7];
};                                

static int cpu_port;

#define GLOBAL_REGS_1   0x1f                                 
#define GLOBAL_REGS_2   0x17                                 
                                                             
#define VTU_OPS_REGISTER  0x05  /* offset in GLOBAL_REGS_1 */
#define VTU_OP_FLUSHALL          (1 << 12)                   
#define VTU_OP_LOAD              (3 << 12)                   
#define VTU_OP_GET_NEXT          (4 << 12)                   
#define VTU_OP_GET_CLR_VIOLATION (7 << 12)                   
                                                             
#define VTU_VID_REG     0x06  /* offset in GLOBAL_REGS_1 */  

static uint16_t vtu_readwait(unsigned long reg) {
	volatile unsigned short x;
	do {
		if(phy_read(GLOBAL_REGS_1, reg, &x) < 0) 
		  fprintf(stderr, "VTU_Read Timeout to 0x%lX\n", reg);
	} while (x & (1 << 15));

	return x;
}

static const char *MemberTagString(int tag) {
	switch(tag) {
		case 0: return "unmodified";
		case 1: return "untagged";
		case 2: return "tagged";
		default: return "unknown";
	}
}

inline void switch_errata_3_1(unsigned int n) {  
   volatile unsigned short smicmd, x;
   
   /* Write 0x0003 to PHY register 13 */
   do {
      phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
   smicmd = 0x960D | n;
	phy_write(0x17, 0x19, 0x0003);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
	
	/* Write 0x0000 to PHY register 14 */
	do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
	smicmd = 0x960E | n;
	phy_write(0x17, 0x19, 0);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
	
	/* Write 0x4003 to PHY register 13 */
	do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
   smicmd = 0x960D | n;
	phy_write(0x17, 0x19, 0x4003);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
	
	/* Write 0x0000 to PHY register 14 */
	do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
	smicmd = 0x960E | n;
	phy_write(0x17, 0x19, 0);   /* smi data */		
	phy_write(0x17, 0x18, smicmd);
}

inline void switch_enphy(unsigned long phy) {
   volatile unsigned short x;
   do {
	   phy_read(0x17, 0x18, &x); 
   } while (x & (1 << 15));
	phy_write(0x17, 0x19, 0xb300);
	phy_write(0x17, 0x18, phy);
}

inline void switch_enflood(unsigned long port) {
	volatile unsigned short data;
	phy_read(port, 0x04, &data);
	phy_write(port, 0x04, data | 0xf);
}

inline void switch_enbcastflood(unsigned long port) {
	phy_write(port, 0x04, 0x7f);
}

inline void switch_forcelink(unsigned long port) {
	volatile unsigned short data;
	phy_read(port, 0x01, &data);
	phy_write(port, 0x01, data | 0x30);
}

static int vtu_add_entry(struct vtu *entry) {
	int i, j, p;
	volatile unsigned short x, r7, r8;
	unsigned short r6[7];

	while(1) {
		if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		if (! (x & (1 << 15)))  /* wait for VB=0 in Global Reg 0x05 */
		break;
	}

	phy_write(GLOBAL_REGS_1, VTU_VID_REG, (1 << 12) | entry->vid);

	r7 = entry->tags[0] | (entry->tags[1] << 4) | (entry->tags[2] << 8) |
	  (entry->tags[3] << 12);
	r8 = entry->tags[4] | (entry->tags[5] << 4) | (entry->tags[6] << 8);

	phy_write(GLOBAL_REGS_1, 0x07, r7);
	phy_write(GLOBAL_REGS_1, 0x08, r8);
	phy_write(GLOBAL_REGS_1, VTU_OPS_REGISTER, 0xb000);   /* start the load */

	/* Next, we take care of the VLAN map, which is a per-port
	bitfield at offset 6 */

	while(1) {
		if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		if (! (x & (1 << 15)))  /* wait for VB=0 in Global Reg 0x05 */
		break;
	}

	phy_write(GLOBAL_REGS_1, VTU_VID_REG, 0xFFF);

	i=j=0;
	memset(&r6, 0, sizeof(r6));

	while(1) {
		x = 0;
		phy_write(GLOBAL_REGS_1, VTU_OPS_REGISTER, (1 << 15) | VTU_OP_GET_NEXT);

		while(1) {
			if (phy_read(GLOBAL_REGS_1, VTU_OPS_REGISTER, &x) < 0) {
				fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
				return -1;
			}

			if (! (x & (1 << 15)))  /* wait for VB=0 in Global Reg 0x05 */
			break;
		}

		if (phy_read(GLOBAL_REGS_1, VTU_VID_REG, &x) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		if ((x & 0xFFF) == 0xFFF)
		break;

		j++;

		if (phy_read(GLOBAL_REGS_1, 0x07, &r7) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		if (phy_read(GLOBAL_REGS_1, 0x08, &r8) < 0) {
			fprintf(stderr, "Timeout %s %d\n", __FILE__, __LINE__);
			return -1;
		}

		for(p=0; p < 7; p++) {
			switch(p) {
			  case 0: if ((r7 & 0x3) != 0x3)  goto L1; break;
			  case 1: if ((r7 & 0x30) != 0x30)  goto L1; break;
			  case 2: if ((r7 & 0x300) != 0x300)  goto L1; break;
			  case 3: if ((r7 & 0x3000) != 0x3000)  goto L1; break;
			  case 4: if ((r8 & 0x3) != 0x3)  goto L1; break;
			  case 5: if ((r8 & 0x30) != 0x30)  goto L1; break;
			  case 6: if ((r8 & 0x300) != 0x300)  goto L1; break;
			}

			continue;

L1:
			for(i=0; i < 7; i++) {
				if (i != p) {  /* don't set "our" bit in "our" mask register */
					switch(i) {
					  case 0: if ((r7 & 0x3) != 0x3) { r6[p] |= (1 << i);  } break;
					  case 1: if ((r7 & 0x30) != 0x30) { r6[p] |= (1 << i); } break;
					  case 2: if ((r7 & 0x300) != 0x300) { r6[p] |= (1 << i); } break;
					  case 3: if ((r7 & 0x3000) != 0x3000) { r6[p] |= (1 << i);  }  break;
					  case 4: if ((r8 & 0x3) != 0x3) { r6[p] |= (1 << i);  } break;
					  case 5: if ((r8 & 0x30) != 0x30) { r6[p] |= (1 << i); } break;
					  case 6: if ((r8 & 0x300) != 0x300) { r6[p] |= (1 << i); } break;
					}
				}
				else if (p != cpu_port) {
					if (entry->tags[p] != 3) {
						if (entry->forceVID[p])
						phy_write(0x18 + p, 0x7, 0x1000 | entry->vid);
						else
						phy_write(0x18 + p, 0x7, entry->vid);
					}
				}
			}
		}
	}

	for(p=0; p < 7; p++) {
		phy_write(0x18 + p, 0x6, r6[p]);

		if (entry->tags[p] != 3)
		phy_write(0x18 + p, 0x8, 0x480);
	}

	return 0;
}

static void usage(char **argv) {
	fprintf(stderr,
	  "%s\n\n"
	  "Usage: %s [OPTION] ...\n"
	  "Technologic Systems TS-76xx/TS-46xx/TS-74xx manipulation.\n"
	  "\n"
	  "General options:\n"
	  "  -P, --ethvlan           Set each switch port to its own VLAN\n"
	  "  -y, --ethswitch         Set all switch ports to switch mode\n"
	  "  -5, --ethwlan           Sets port A on VLAN, other ports to switch mode\n"
	  "  -C, --ethinfo           Retrieves info on the onboard switch\n"
	  "  -Q  --ethbus            MII management bus number (implementation dependent)\n"
	  "  -h, --help              This help\n"
	  "Additional port config options:\n"
	  "  -p, --port <port>       PHY Port to operate on [0-1]\n"
	  "  -a, --autoneg           Enable auto-negotiation of <port> (default behavior)\n"
	  "  -0, --10mb              Force 10mbit on <port>\n"
	  "  -1, --100mb             Force 100mbit on <port>\n"
	  "  -l, --half              Force half-duplex on <port>\n"
	  "  -f, --full              Force full-duplex on <port>\n",
	  copyright, argv[0]
	);
}
	
int main(int argc, char **argv) {
	int i, c;
	volatile unsigned short swmod;
	int opt_busnum = 0;
	int opt_ethvlan = 0, opt_ethswitch = 0, opt_ethinfo = 0, opt_ethwlan=0;
	int opt_port = -1, opt_autoneg = 0, opt_10mb = 0, opt_100mb = 0;
	int opt_half = 0, opt_full = 0;
	static struct option long_options[] = {
	  { "ethvlan", 0, 0, 'P'},
	  { "ethswitch", 0, 0, 'y'},
	  { "ethwlan", 0, 0, '5'},
	  { "ethinfo", 0, 0, 'C'},
	  { "ethbus", 1, 0, 'Q'},
	  { "help", 0, 0, 'h'},
	  { "port", 1, 0, 'p'},
	  { "autoneg", 0, 0, 'a'},
	  { "10mb", 0, 0, '0'},
	  { "100mb", 0, 0, '1'},
	  { "half", 0, 0, 'l'},
	  { "full", 0, 0, 'f'},
	  { 0, 0, 0, 0}
	};


	while((c = getopt_long(argc, argv,
          "Py5CQ:hp:a01lf", long_options, NULL)) != -1) {
		switch (c) {
		  case 'P':
			opt_ethvlan = 1;
			break;
		  case 'y':
			opt_ethswitch = 1;
			break;
		  case '5':
			opt_ethwlan = 1;
			break;
		  case 'C':
			opt_ethinfo = 1;
			break;
		  case 'Q':
			opt_busnum = strtoull(optarg, NULL, 0);
			break;
		  case 'p':
			opt_port = (strtoull(optarg, NULL, 0) | 0x10);
			break;
		  case 'a':
			opt_autoneg = 1;
			break;
		  case '0':
			opt_10mb = 1;
			break;
		  case '1':
			opt_100mb = 1;
			break;
		  case 'l':
			opt_half = 1;
			break;
		  case 'f':
			opt_full = 1;
			break;
		  default:
			usage(argv);
			return 1;
			break;
		}
	}

	if ((opt_ethswitch || opt_ethinfo || opt_ethvlan || opt_ethwlan)) {

		phy_init();

		if(phy_read(0x18, 0x03, &swmod) == -1) {
			printf("switch_model=none\n");
			opt_ethvlan = 0;
			opt_ethwlan = 0;
			opt_ethswitch = 0;
		} else {
			swmod &= 0xFFF0;
			if(swmod == 512){
				printf("switch_model=88E6020\n");
			}else if (swmod == 1792) {
				printf("switch_model=88E6070\n");
			} else if(swmod == 1808) {
				printf("switch_model=88E6071\n");
				fprintf(stderr, "Unsupported switch\n");
			} else if(swmod == 8704) {
				printf("switch_model=88E6220\n");
				fprintf(stderr, "Unsupported switch\n");
			} else if(swmod == 9472) {
				printf("switch_model=88E6251\n");
				fprintf(stderr, "Unsupported switch\n");
			} else {
				printf("switch_model=unknown\n");
			}
		}
	}

	if ((opt_ethswitch || opt_ethvlan || opt_ethwlan)) {
		if(swmod == 512) { // Marvell 88E6020 Network Switch
			// this chip has 2 PHYs and 2 RMII ports
			// Apply Marvell 88E60x0 Errata 3.1 for PHY 0 and 1.
			switch_errata_3_1(0);
			switch_errata_3_1(1<<5);
			// enable both PHYs
			switch_enphy(0x9600);
			switch_enphy(0x9620);

			// enable port flood on all ports
			if(opt_ethswitch) {
				switch_enflood(0x18);
				switch_enflood(0x19);
				switch_enflood(0x1d);
			} else if (opt_ethvlan || opt_ethwlan) {
				switch_enbcastflood(0x18);
				switch_enbcastflood(0x19);
				switch_enbcastflood(0x1d);
			}

			// Force link up on P5, the CPU port
			switch_forcelink(0x1d);

			// With this chip ethvlan and wlan switch are the same since
			// we only have 2 ports
			if(opt_ethvlan || opt_ethwlan) {
				struct vtu entry;
				// Configure P5
				phy_write(0x1d, 0x05, 0x0);
				phy_write(0x1d, 0x06, 0x5f);
				phy_write(0x1d, 0x07, 0x1);
				phy_write(0x1d, 0x08, 0x480);

				for(i=0; i < 7; i++) {
					entry.tags[i] = 3; // leave the port alone by default
					entry.forceVID[i] = 0;
				}

				// Set up VLAN #1 on P0 and P5
				entry.v = 1;
				entry.vid = 1;
				entry.tags[5] = 2; // TS-4712 CPU port is always egress tagged
				entry.tags[0] = 1; // External Port A egress untagged
				vtu_add_entry(&entry);
				entry.tags[0] = 3; // Do not configure port A again

				// Setup VLAN #2 on P1 and P5
				entry.vid = 2;
				entry.tags[1] = 1; // External PORT B egress untagged
				vtu_add_entry(&entry);
			}
		} else if (swmod == 1792) { // Marvell 88E6070 Network Switch
			// Used on the TS-8700
			// enable all PHYs
			
			switch_errata_3_1(0);
			switch_errata_3_1(1<<5);
			switch_errata_3_1(2<<5);
			switch_errata_3_1(3<<5);
			switch_errata_3_1(4<<5);
			
			switch_enphy(0x9600);
			switch_enphy(0x9620);
			switch_enphy(0x9640);
			switch_enphy(0x9660);
			switch_enphy(0x9680);

			switch_enflood(0x18);
			switch_enflood(0x19);
			switch_enflood(0x1a);
			switch_enflood(0x1b);
			switch_enflood(0x1c);
			if(opt_ethvlan) {
				struct vtu entry;
				for(i=0; i < 7; i++) {
					entry.tags[i] = 3; // leave the port alone by default
					entry.forceVID[i] = 0;
				}
				entry.v = 1;

				// Set up VLAN #1 on P0 and P1
				entry.vid = 1;
				entry.tags[0] = 2; // TS-4710 CPU port is always egress tagged
				entry.tags[1] = 1; // External Port A egress untagged
				vtu_add_entry(&entry);
				entry.tags[1] = 3; // Do not configure port A again

				// Setup VLAN #2 on P0 and P2
				entry.vid = 2;
				entry.tags[2] = 1; // External PORT B egress untagged
				vtu_add_entry(&entry);
				entry.tags[2] = 3; // Do not configure port B again

				// Setup VLAN #2 on P0 and P3
				entry.vid = 3;
				entry.tags[3] = 1; // External PORT C egress untagged
				vtu_add_entry(&entry);
				entry.tags[3] = 3; // Do not configure port C again

				// Setup VLAN #2 on P0 and P4
				entry.vid = 4;
				entry.tags[4] = 1; // External PORT D egress untagged
				vtu_add_entry(&entry);
			} else if (opt_ethwlan) {
				struct vtu entry;
				for(i=0; i < 7; i++) {
					entry.tags[i] = 3; // leave the port alone by default
					entry.forceVID[i] = 0;
				}
				entry.v = 1;

				// Set up VLAN #1 with P0 and P1
				entry.vid = 1;
				entry.tags[0] = 2; // TS-4710 CPU port is always egress tagged
				entry.tags[1] = 1; // External Port A egress untagged
				vtu_add_entry(&entry);
				entry.tags[1] = 3; // Do not configure port A again

				// Set up VLAN #1 with P0, P2, P3, and P4
				entry.vid = 2;
				entry.tags[0] = 2; // TS-4710 CPU port is always egress tagged
				entry.tags[2] = 1; // External Port B egress untagged
				entry.tags[3] = 1; // External Port C egress untagged
				entry.tags[4] = 1; // External Port D egress untagged
				vtu_add_entry(&entry);
			}
		} else {
			fprintf(stderr, "Switch not configured\n");
		}
	}

	if (opt_ethinfo) {
		int ports = 0, i = 0, port, vtu = 0;
		int phy[8] = {0,0,0,0,0,0,0,0};
		volatile unsigned short x, r7;
		
		if(swmod == 512){ // 4712
			ports = 2;
			phy[0] = 0x18;
			phy[1] = 0x19;
		} else if (swmod == 1792) { // 8700
			ports = 4;
			phy[0] = 0x19;
			phy[1] = 0x1a;
			phy[2] = 0x1b;
			phy[3] = 0x1c;
		} else {
			printf("Unknown switch: %d\n", swmod);
			return 1;
		}
		printf("switch_ports=\"");
		for (i = 0; i < ports; i++) {
			printf("%c", 97 + i);
			if (i != ports - 1)
			  printf(" ");
		}
		printf("\"\n");

		for (i = 0; i < ports; i++) {
			volatile unsigned short dat;
			phy_read(phy[i], 0x0, &dat);
			printf("switchport%c_link=%d\n", 97 + i,
			  dat & 0x1000 ? 1 : 0);
			printf("switchport%c_speed=", 97 + i);
			dat = (dat & 0xf00) >> 8;
			if(dat == 0x8)
			  printf("10HD\n");
			else if (dat == 0x9)
			  printf("100HD\n");
			else if (dat == 0xa)
			  printf("10FD\n");
			else if (dat == 0xb)
			  printf("100FD\n");
		}

		x = vtu_readwait(VTU_OPS_REGISTER);
		phy_write(GLOBAL_REGS_1, VTU_VID_REG, 0xFFF);
		while(1) {
			phy_write(GLOBAL_REGS_1, VTU_OPS_REGISTER, (1 << 15) | VTU_OP_GET_NEXT);
			vtu_readwait(VTU_OPS_REGISTER);
			phy_read(GLOBAL_REGS_1, VTU_VID_REG, &x);

			// No more VTU entries
			if ((x & 0xFFF) == 0xFFF) {
				printf("vtu_total=%d\n", vtu);
				break;
			}
			vtu++;
			printf("vtu%d_vid=%d\n", vtu, x & 0xfff);
			for (port = 0; port < 7; port++) {
				phy_read(0x18 + port, 7, &r7);
				if(port == 0)
				  phy_read(GLOBAL_REGS_1, 0x07, &x);
				else if (port == 4)
				  phy_read(GLOBAL_REGS_1, 0x08, &x);


				switch(port) {
				  case 0:
					if ((x & 0x0003) != 0x0003) {
						printf("vtu%d_port0=1\n"
						  "vtu%d_port0_forcevid=%c\n"
						  "vtu%d_port0_egress=%s\n"
						  "vtu%d_port0_alias=%s\n",
						  vtu,
						  vtu, r7 & 0x1000? 'Y':'N',
						  vtu, MemberTagString(x & 3),
						  vtu, !opt_busnum ? "a":"cpu");
					}
					break;
				  case 1:
					if ((x & 0x0030) != 0x0030) {
						printf("vtu%d_port1=1\n"
						  "vtu%d_port1_forcevid=%c\n"
						  "vtu%d_port1_egress=%s\n"
						  "vtu%d_port1_alias=%s\n",
						  vtu,
						  vtu, r7 & 0x1000? 'Y':'N',
						  vtu, MemberTagString((x>>4)&3),
						  vtu, !opt_busnum ? "b":"a");
					}
					break;
				  case 2:
					if ((x & 0x0300) != 0x0300) {
						printf("vtu%d_port2=1\n"
						  "vtu%d_port2_forcevid=%c\n"
						  "vtu%d_port2_egress=%s\n"
						  "vtu%d_port2_alias=%s\n",
						  vtu,
						  vtu, r7 & 0x1000? 'Y':'N',
						  vtu, MemberTagString((x>>8)&3),
						  vtu, !opt_busnum ? "unused":"b");
					}
					break;
				  case 3:
					if ((x & 0x3000) != 0x3000) {
						printf("vtu%d_port3=1\n"
						  "vtu%d_port3_forcevid=%c\n"
						  "vtu%d_port3_egress=%s\n"
						  "vtu%d_port3_alias=%s\n",
						  vtu,
						  vtu, r7 & 0x1000? 'Y':'N',
						  vtu, MemberTagString((x>>12)&3),
						  vtu, !opt_busnum ? "unused":"c");
					}
					break;
				  case 4:
					if ((x & 0x0003) != 0x0003) {
						printf("vtu%d_port4=1\n"
						  "vtu%d_port4_forcevid=%c\n"
						  "vtu%d_port4_egress=%s\n"
						  "vtu%d_port4_alias=%s\n",
						  vtu,
						  vtu, r7 & 0x1000? 'Y':'N',
						  vtu, MemberTagString(x & 3),
						  vtu, !opt_busnum ? "b":"d");
					}
					break;
				  case 5:
					if ((x & 0x0030) != 0x0030) {
						printf("vtu%d_port5=1\n"
						  "vtu%d_port5_forcevid=%c\n"
						  "vtu%d_port5_egress=%s\n"
						  "vtu%d_port5_alias=%s\n",
						  vtu,
						  vtu, r7 & 0x1000? 'Y':'N',
						  vtu, MemberTagString((x>>4)&3),
						  vtu, !opt_busnum ? "cpu":"unused");
					}
					break;
				  case 6:
					if ((x & 0x0300) != 0x0300) {
						printf("vtu%d_port6=1\n"
						  "vtu%d_port6_forcevid=%c\n"
						  "vtu%d_port6_egress=%s\n"
						  "vtu%d_port6_alias=%s\n",
						  vtu,
						  vtu, r7 & 0x1000? 'Y':'N',
						  vtu, MemberTagString(x & 3),
						  vtu, "unused");
					}
					break;
				}
			}
		}
	}

	if(opt_port >= 0x10 && opt_port <= 0x11) {
		volatile unsigned short smicmd, smidat;

		phy_init();

		/* Read from PHY port */
		do {
			phy_read(0x17, 0x18, &smicmd);
		} while (smicmd & (1 << 15));
	
		smicmd = ((0x9 << 12) | (0x2 << 10) | (opt_port << 5) | (0x0));
		phy_write(0x17, 0x18, smicmd);
		do {
			phy_read(0x17, 0x18, &smicmd);
		} while (smicmd & (1 << 15));
		phy_read(0x17, 0x19, &smidat);

		/* Set options */
		smidat &= ~(1 << 12);  //Disable auto-negotiation
		if(opt_10mb) smidat &= ~(1 << 13);
		if(opt_100mb) smidat |= (1 << 13);
		if(opt_half) smidat &= ~(1 << 8);
		if(opt_full) smidat |= (1 << 8);
		if(opt_autoneg) smidat |= ((1 << 8) | (1 << 12) | (1 << 13));

		/* Write back to phy */
		smicmd = ((0x9 << 12) | (0x1 << 10) | (opt_port << 5) | (0x0));
		phy_write(0x17, 0x19, smidat);
		phy_write(0x17, 0x18, smicmd);
		do {
			phy_read(0x17, 0x18, &smicmd);
		} while (smicmd & (1 << 15));

		/* Issue soft-reset */
		smicmd = ((0x9 << 12) | (0x1 << 10) | (opt_port << 5) | (0x0));
		smidat |= (1 << 15);
		phy_write(0x17, 0x19, smidat);
		phy_write(0x17, 0x18, smicmd);
		do {
			phy_read(0x17, 0x18, &smicmd);
		} while (smicmd & (1 << 15));
	}	

	return 0;
}
