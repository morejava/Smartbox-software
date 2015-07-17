#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Create useful debug information and store is in a tar file.
#
# Usage:         This script can be invoked from other scripts when a system 
#                reboot is performed.
#
# Last modified: 24/09/2014
#
##################################################################################

# constant declarations
TMPDIR="/tmp/debug"

# Perform things as superuser
#sudo -s

# load generic configuration parameters
. /etc/geofox.conf

# make sure tmpdir is recreated
[ -d $TMPDIR ] && rm -rf $TMPDIR
mkdir $TMPDIR

# collect useful system information
ifconfig -a > $TMPDIR/ifconfig
ps -ef > $TMPDIR/ps
netstat --inet > $TMPDIR/netstat
cp /var/log/syslog.1 $TMPDIR/
cp /var/log/syslog $TMPDIR/

# create a gzipped tar file with timestamp
cd $TMPDIR
tar -czf $STORAGE_DIR/debug/dbginfo-$(date +%y%m%d-%H%M%S).tgz *

exit 0
