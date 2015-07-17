#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Transfers saved geodat files to a remote server.
#
# Usage:         This script is placed in /etc/ppp/ip-up.d/ and therefore will be 
#                called by pppd every time the ppp connection is established.
#
# Last modified: 17/04/2015
#
##################################################################################

LOGFILE=/var/log/connection.log

# If ssh connection does not succeed within 10s, retry after 120s, max 3 times
SSH_TIMEOUT=10
RETRY_PERIOD=120
MAX_RETRY=3

# load generic configuration parameters
. /etc/geofox.conf

echo $(date +"%x %X")" send-geodat called with IFACE="$IFACE" PPP_LOCAL="$PPP_LOCAL" CONNECTION="$CONNECTION >> $LOGFILE

# some sanity checks
[[ -n "$IFACE" && "$IFACE" != "eth0" && "$IFACE" != "wlan0" ]] &&  exit 1
[[ -n "$IFACE" && "$CONNECTION" == "CABLE" && "$IFACE" != "eth0" ]] &&  exit 2
[[ -n "$IFACE" && "$CONNECTION" == "WLAN" && "$IFACE" != "wlan0" ]] &&  exit 3
#[[ "$CONNECTION" == "GPRS" && -z $PPP_LOCAL ]] &&  exit 4

# exit now if there is nothing to transfer to remote server
[ -z "$(ls $DATA_DIR | grep .tgz)" ] && exit 5

# save file names in variable
# select all archived data files present in the data directory
cd $DATA_DIR
FILES=$(ls -1 *.tgz)
#FILES=$(find . -type f -name "*.tgz" -mtime -7)

# send data to remote server if it is reachable
RETRY=0
SSH_DONE="false"

while [[ "$SSH_DONE" == "false" && $RETRY -lt $MAX_RETRY ]]
do

  # sleep before next retry
  if [ $RETRY -ne 0 ]; then
    sleep $RETRY_PERIOD
  fi

  # count number of retries
  RETRY=$(($RETRY+1))

  # We create a specific data directory on the server for every device, 
  # using its hostname so we can tell where the data comes from
  RCMD="test -d $(hostname)$PLANTID || mkdir $(hostname)$PLANTID"
  ssh -o ConnectTimeout=$SSH_TIMEOUT -p $RPORT $RLOGIN@$RSERVER $RCMD
  if [ $? != 0 ]; then 
    echo $(date +"%x %X")" send-geodat: ssh timeout when checking for existence of remote directory" >> $LOGFILE
    continue
  fi

  # Create data dir also in testdb
  RCMD="test -d testdb/$(hostname)$PLANTID || mkdir testdb/$(hostname)$PLANTID"
  ssh -o ConnectTimeout=$SSH_TIMEOUT -p $RPORT $RLOGIN@$RSERVER $RCMD
  if [ $? != 0 ]; then 
    echo $(date +"%x %X")" send-geodat: ssh timeout when checking for existence of remote directory (testdb)" >> $LOGFILE
    continue
  fi

  # transfer archived data files to remote server
  scp -o ConnectTimeout=$SSH_TIMEOUT -P $RPORT ${FILES} $RLOGIN@$RSERVER:$(hostname)$PLANTID/
  if [ $? != 0 ]; then 
    echo $(date +"%x %X")" send-geodat: ssh timeout while trying to send daily data" >> $LOGFILE
    continue
  fi

  # create hard links for transferred files on remote server (duplicate DB)
  for file in ${FILES} 
  do 
    RCMD="cd testdb/$(hostname)$PLANTID; ln ../../$(hostname)$PLANTID/${file} ${file}"
    ssh -o ConnectTimeout=$SSH_TIMEOUT -p $RPORT $RLOGIN@$RSERVER $RCMD
    if [ $? != 0 ]; then 
      echo $(date +"%x %X")" send-geodat: ssh timeout while trying to create hard links" >> $LOGFILE
      SSH_DONE="false"
      break
    else
      SSH_DONE="true"
    fi
  done
done

if [ "$SSH_DONE" == "true" ]; then
  # successful data transfer, remove archived data files from local storage
  rm ${FILES} 
else
  # data transfer failed, we gave up trying
  echo $(date +"%x %X")" send-geodat: ssh connection broken, giving up after "$RETRY" retries" >> $LOGFILE
fi

exit 0
