GITCOMMIT:= $(shell git describe --abbrev=12 --dirty --always)

mx28adcctl_SOURCES = mx28adcctl.c
mx28adcctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

switchctl_SOURCES = switchctl.c switchctl-ts768x.c
switchctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

tshwctl_SOURCES = tshwctl.c fpga.c
tshwctl_CPPFLAGS = -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

tsmicroctl_SOURCES = tsmicroctl.c
tsmicroctl_CPPFLAGS = -DCTL -Wall -DGITCOMMIT="\"${GITCOMMIT}\""

bin_PROGRAMS = tshwctl tsmicroctl
noinst_PROGRAMS = mx28adcctl switchctl
