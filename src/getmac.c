/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2019, Technologic Systems Inc. */

#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

const char copyright[] = "Copyright (c) Technologic Systems - " __DATE__ " - "
  GITCOMMIT;

int main(int argc, char **argv) 
{
	struct ifreq ifr;
	int s;
	int ok = 0;
	unsigned char mac[6];

	if (argc != 2) {
		fprintf(stderr, "%s\n\nUsage: %s [interface]\n",
		copyright, argv[0]);
		return 1;
	}

	s = socket(AF_INET, SOCK_DGRAM, 0);
	if (s == -1) return -1;

	strncpy(ifr.ifr_name, argv[1], sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFFLAGS, &ifr) == 0) {
		if (!(ifr.ifr_flags & IFF_LOOPBACK)) {
			if (ioctl(s, SIOCGIFHWADDR, &ifr) == 0) {
				ok = 1;
			}
		}
	}
	close(s);

	if (ok) {
		memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
		printf("shortmac=%02X%02X%02X\n", mac[3], mac[4], mac[5]);
		printf("longmac=%02X%02X%02X%02X%02X%02X\n", mac[0], mac[1],
		  mac[2], mac[3], mac[4], mac[5]);
		return 0;
	}

	return 1;
}
	
