#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Reads the latest data from log file.
#
# Usage:         This script will be called remotely from the server.
#
# Last modified: 12/11/2013
#
##################################################################################


# load generic configuration parameters
. /etc/geofox.conf

# In order to make sure we read the updated data on the storage partition, it has 
# to be mounted just before reading it (only for USB data connection to NIBE).
if [ "$DATA_ACQ_MODE" ==  "NIBE" ]; then
  mount $STORAGE_PART $STORAGE_DIR
  sleep 0.5
fi

# Determine name of latest (current) log file
LATESTLOG=$(ls -t -1 $STORAGE_DIR/*.LOG | head -1)

# Read column headings (first two rows) from log file
head -2 $LATESTLOG

# Read latest data row
tail -1 $LATESTLOG 

# unmount data storage partition
if [ "$DATA_ACQ_MODE" ==  "NIBE" ]; then
  umount $STORAGE_DIR
fi

exit 0
