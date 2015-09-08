#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Create the RaspberryPi SD card for Telegea Smartbox
#
# Usage:         Launch manually with required parameters:
#                createsd.sh <img_name> <sdcard_dev>
#
#                Needs the following tools:
#                - parted
#                - partprobe
#                - mkfs.vfat
#                - fsarchiver
#
# Last modified: 08/09/2015
#
##################################################################################

##################################################################################
# The SD card image of the root partition p2 can be created with the 
# following command:
#
# $ sudo fsarchiver savefs <image name>.img /dev/mmcblk0p2
#
##################################################################################

# First argument is compressed image name (remove extension)
IMGNAME=${1/%.tgz/}

# Second argument is SD card device
SDCARD=$2

PART1_FILE=$IMGNAME-p1/*
PART2_FILE=$IMGNAME-p2.img.fsa


# Sanity checks

# Check for required arguments
if [[ -z $IMGNAME || -z $SDCARD ]]; then
   echo "Usage: $0 <img_name> <sdcard_dev>"
   exit 1
fi

# Check if image file was found
if [ ! -e $IMGNAME.tgz ]; then
   echo "ERROR: Image file $IMGNAME.tgz not found"
   exit 2
fi

# Check if SD card device was found
if [ ! -b $SDCARD ]; then
   echo "ERROR: SD card device $SDCARD not found"
   exit 3
fi

# Check that we are running as root
if [ $(whoami) != "root" ]; then
   echo "This script needs to be run as root"
   exit 4
fi

# Need to add partition prefix for mmcblk devices
if [[ $SDCARD == *"mmcblk"* ]]; then 
   PARTPREFIX="p"
fi

echo "#####################################################################"
echo "WARNING: all data on "$SDCARD" will be erased, starting in 20s"
echo "Press CTRL C to aboard."
echo "#####################################################################"
sleep 20


echo "**************************************"
echo "Extracting files..."

mkdir $IMGNAME
cd $IMGNAME
tar xf ../$IMGNAME.tgz

echo "done"
echo "**************************************"
echo

echo "**************************************"
echo "Creating new partitions on card..."

# Delete existing partitions
parted -s $SDCARD mklabel msdos

# Make partitions
parted -s $SDCARD mkpart primary fat16 0%  50.0MB
parted -s $SDCARD mkpart primary ext2 50.0MB  2550MB
parted -s $SDCARD mkpart primary ext2 2550MB  100%

# Set boot flag
parted -s "$SDCARD" set 1 boot on

# tell kernel we have a new partition table
partprobe "$SDCARD"

echo "done"
echo "**************************************"
echo

#parted "$SDCARD" print
#echo

# Copy boot partition
echo "**************************************"
echo "Copying boot partition data to card..."
#fsarchiver restfs $PART1_FILE id=0,dest="$SDCARD"p1
mkfs.vfat "$SDCARD$PARTPREFIX"1
PART1_MOUNTPOINT=/tmp/sdcardp1
mkdir -p $PART1_MOUNTPOINT
mount -t vfat "$SDCARD$PARTPREFIX"1 $PART1_MOUNTPOINT
cp -r $PART1_FILE $PART1_MOUNTPOINT
sync
sleep 2
umount $PART1_MOUNTPOINT
rm -r $PART1_MOUNTPOINT
echo "done"
echo "**************************************"
echo

# Copy OS partition
echo "**************************************"
echo "Copying OS partition data to card..."
fsarchiver restfs $PART2_FILE id=0,dest="$SDCARD$PARTPREFIX"2
sync
echo "done"
echo "**************************************"
echo

# Format data partition
echo "**************************************"
echo "Formatting data partition on card..."
mkfs.ext4 "$SDCARD$PARTPREFIX"3
sync
echo "done"
echo "**************************************"
echo

echo "**************************************"
echo "Created SD card successfully"
echo "**************************************"
echo

exit 0
