#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Monitors status registers for changes and sends an alarm 
#                notification to the Telegea HTTP API
#
# Usage:         This script will be launched by a system init script and runs 
#                as a daemon application
#
# Last modified: 13/07/2015
#
##################################################################################

VERSION="0.1"

# load generic configuration parameters
. /etc/geofox.conf

# Settings used by Modbus TCP client
IP_ADDR=127.0.0.1
TCP_BASEPORT=5000
OPERATION=1

# Web API handling
API_MODULE="alarm.php"
API_ERR_CNT=0
TMPFILE=/tmp/curl_amon.tmp
CURL=$(which curl)
DEV_ALARM_NUMBER=3

# Log messages go to syslog
LOGCMD="logger -i -t alarm_scanner"

VALIDALMLIST="/tmp/valid_alarm_registers.txt"

#rmdir /tmp/modbus_client_lock &> /dev/null
echo "Alarm monitor (ver $VERSION) starting with parameters $AMON_INFRAREAD_DELAY $AMON_INFRALOOP_DELAY"
$LOGCMD "Alarm monitor (ver $VERSION) starting with parameters $AMON_INFRAREAD_DELAY $AMON_INFRALOOP_DELAY"

if [[ -z $API_KEY ]]; then 
  $LOGCMD "No API key defined, exiting"
  exit 1
fi

# extract valid alarm registers for this plant
awk '/AL/' $REGISTER_LIST > $VALIDALMLIST

# parse the list to extract valid registers and their parameters
IDX=0
IFS=
while read LINE; do
 #echo "Now reading $LINE"
 IDX=$(($IDX+1))
 SLAVE=`echo $LINE | cut -f2`
  ADDR=`echo $LINE | cut -f3`
  NAME=`echo $LINE | cut -f4`

  # The Modbus TCP server only handles the address range [0 - 8200],
  # therefore we need to adjust the register addresses which come in  
  # range [40000 - 48200] (e.g. NIBE40 Modbus module)
  if [ $ADDR -gt 40000 ]; then
    ADDR=$(($ADDR-40000))
  fi
  
  SLAVES["$IDX"]=$SLAVE
  ADDRESSES["$IDX"]=$ADDR
  REGNAMES["$IDX"]=$NAME
  OLDVALUES["$IDX"]=-1
done < $VALIDALMLIST
unset IFS
rm $VALIDALMLIST


###############################################################
#
# Main loop: 
# Read registers periodically and send alarm notification
# on changes
#
###############################################################
while :
  do
  TIME=`date '+%s'`
  
  # Calculate start time for next cycle
  NEXT_CYCLE_START=$(date --date="now + $AMON_INFRALOOP_DELAY seconds" +%s)

  # Read one register at a time
  for i in "${!REGNAMES[@]}"
    do
      #echo "reading ${REGNAMES[$i]}"
      TEMPREG=`$MODBUS_CLIENT $IP_ADDR $(($TCP_BASEPORT+${SLAVES[$i]})) $OPERATION ${SLAVES[$i]} ${ADDRESSES[$i]} | grep ^${ADDRESSES[$i]} | cut -d: -f3 | tr -d ' '`
      #echo "RESULT: $TEMPREG"
      
      # Create and send WebAPI request
      if [[ ! -z $TEMPREG ]]; then
      
        if [[ ${OLDVALUES[$i]} != -1 && ${OLDVALUES[$i]} != $TEMPREG  ]]; then
        
          # Register names need to be converted to DB field names
          REGNAME=${REGNAMES[$i]}
          REGNAME=$(echo $REGNAME | sed 's/ /_/g')
          REGNAME=$(echo $REGNAME | sed 's/-/_/g')
          REGNAME=$(echo $REGNAME | sed 's/\.//g')
          REGNAME=$(echo $REGNAME | awk '{print tolower($0)}')

          # Create HTTP request body
          DATA="apikey=$API_KEY&plant_id=$PLANTID&time=$TIME&alarm=$DEV_ALARM_NUMBER&json={$REGNAME:$TEMPREG}"
          #echo "DATA: $DATA"

          # Send HTTP request
          $CURL -m 30 -k -s -o $TMPFILE "$TELEGEA_API/$API_MODULE?$DATA"

          # Check curl exit code
          EC=$?
          if [ $EC -eq 0 ]; then
      
            # Check for transmission or API errors
            if [ $(cat $TMPFILE | wc |  awk '{print $2}') -ne 1 ]; then
              API_ERR_CNT=$(($API_ERR_CNT+1))
              $LOGCMD "Error sending data to WebAPI! Error count: $API_ERR_CNT"
            fi
          else
            $LOGCMD "Error $EC reported by curl"
          fi
        fi
        
        # Save last value
        OLDVALUES[$i]=$TEMPREG
      fi 
      
      sleep $AMON_INFRAREAD_DELAY 
    done
  
  # wait for next update period
  TIME_TO_WAIT=$(($NEXT_CYCLE_START-$(date +%s)))
  if [ $TIME_TO_WAIT -gt 0 ]; then
    sleep $TIME_TO_WAIT
  fi

done
