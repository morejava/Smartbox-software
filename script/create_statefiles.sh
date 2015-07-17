#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Create state/control files on writeable SDcard partition
#                and symlinks to them
#
# Usage:         This script will be run once during initial setup.
#                Should be conveniently placed in /etc/rc.local
#
# Last modified: 03/04/2015
#
##################################################################################

# Perform things as superuser
#sudo -s

# load generic configuration parameters
. /etc/geofox.conf

# Run this script only once
[ -f $STORAGE_DIR/.initdone ] && exit 1

mount -o remount,rw /

# Create sensors dir and enter it
[ -d /sensors ] || mkdir /sensors
cd /sensors

# Create links to temperature sensor sysfs files
# from /sys/bus/w1/devices/
# TODO


# Create state/control files on writeable SDcard partition
# and create symlinks in sensors directory if not already there

echo 0 > $STORAGE_DIR/clima_mode_switch
[ -h state1 ] || ln -s $STORAGE_DIR/clima_mode_switch state1

echo 0 > $STORAGE_DIR/clima_zone1_switch
[ -h state2 ] || ln -s $STORAGE_DIR/clima_zone1_switch state2

echo 0 > $STORAGE_DIR/clima_zone2_switch
[ -h state3 ] || ln -s $STORAGE_DIR/clima_zone2_switch state3

echo 0 > $STORAGE_DIR/clima_zone3_switch
[ -h state4 ] || ln -s $STORAGE_DIR/clima_zone3_switch state4

echo 0 > $STORAGE_DIR/clima_zone4_switch
[ -h state5 ] || ln -s $STORAGE_DIR/clima_zone4_switch state5

echo 0 > $STORAGE_DIR/extra_control_switch1
[ -h state6 ] || ln -s $STORAGE_DIR/extra_control_switch1 state6

echo 0 > $STORAGE_DIR/extra_control_switch2
[ -h state7 ] || ln -s $STORAGE_DIR/extra_control_switch2 state7

mount -o remount,ro /

# Create default thermostat configuration file
echo "{\"operation_mode\":\"off\",\"immediate\":{\"temp\":\"20\"}}" > $STORAGE_DIR/thermostat.json

# Create directory for debug information
mkdir $STORAGE_DIR/debug

# Set flag to mark that this script has already run
touch $STORAGE_DIR/.initdone

exit 0
