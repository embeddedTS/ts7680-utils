bin_SCRIPTS = tssupercapmon tsincmac tsstartupscript

# Note: This is a horrible abuse of autotools, but these services need to go to
# the /etc path to match existing setup
systemdservicedir = /etc/systemd/system/
systemdservice_SCRIPTS = tssetmac@.service tsstartup.service tssupercapmon.service tsswitch.service

systemdnetdir = /etc/systemd/network/
systemdnet_SCRIPTS = eth0.1.link eth0.1.netdev eth0.1.network eth0.2.link eth0.2.netdev eth0.2.network switch.network vlan.network
