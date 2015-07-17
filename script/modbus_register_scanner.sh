#! /bin/bash
#
# this script reads all the registers of all the MODBUS peripherals 
# of a Geomonitor plant, by reading a list of registers and then calling
# the geomon_modbustcp_client program to get their values, one at a time, and
# save them in a log file
#
# Author: M. Fioretti, O.Wisniewski
#
# version: 0.8 does not write divisor line and RESERVED column
# date:    2013/02/05
#
# version: 0.9 moved parameter definition to geofox.conf file
# date:    2013/11/04
# changed by: Ondrej Wisniewski
#
# version: 0.10 adapted to new modular architecture
# date:    2013/12/18
# changed by: Ondrej Wisniewski
#
# version: 0.11 added sending register values to server via Web API
# date:    2014/04/14
# changed by: Ondrej Wisniewski
#
# version: 0.12 handling case of failed reg readings, handle NIBE40 Modbus registers
# date:    2014/10/20
# changed by: Ondrej Wisniewski
#
# version: 0.13 convert register values to signed numbers for Web API 
# date:    2015/01/08
# changed by: Ondrej Wisniewski
#
# version: 0.14 Send failed register reading with value "error" to Web API
# date:    2015/02/16
# changed by: Ondrej Wisniewski
#
# version: 0.15 Get PLANTID from config, added handling of curl exit codes
# date:    2015/04/03
# changed by: Ondrej Wisniewski
#
# version: 0.16 Minor changes for coexistance with alarm handler script
# date:    2015/07/13
# changed by: Ondrej Wisniewski

VERSION="0.16"


# load generic configuration parameters
. /etc/geofox.conf

# Settings used by Modbus TCP client
IP_ADDR=127.0.0.1
TCP_BASEPORT=5000
OPERATION=1

# Web API handling
API_MODULE="post.php"
API_ERR_CNT=0
TMPFILE=/tmp/curl_rscan.tmp
CURL=$(which curl)

# Log messages go to syslog
LOGCMD="logger -i -t modbus_register_scanner"

VALIDREGLIST="/tmp/modbus_valid_registers.txt"
PREVIOUS_DAY='0000-00-00'

#rmdir /tmp/modbus_client_lock &> /dev/null
#echo "Modbus reg scanner (ver $VERSION) starting with parameters $INFRAREAD_DELAY $INFRALOOP_DELAY $LOG_FORMAT"
$LOGCMD "Modbus reg scanner (ver $VERSION) starting with parameters INFRAREAD_DELAY:$INFRAREAD_DELAY, INFRALOOP_DELAY:$INFRALOOP_DELAY, LOG_FORMAT:$LOG_FORMAT"

# extract valid registers for this plant
awk '/SI/ ||  /AL/' $REGISTER_LIST > $VALIDREGLIST

# parse the list to extract valid registers and their parameters
IDX=0
IFS=
while read LINE; do
 #echo "Now reading $LINE"
 IDX=$(($IDX+1))
 SLAVE=`echo $LINE | cut -f2`
  ADDR=`echo $LINE | cut -f3`
  NAME=`echo $LINE | cut -f4`
   DIV=`echo $LINE | cut -f5`
  SIGN=`echo $LINE | cut -f7`

  # The Modbus TCP server only handles the address range [0 - 8200],
  # therefore we need to adjust the register addresses which come in  
  # range [40000 - 48200] (e.g. NIBE40 Modbus module)
  if [ $ADDR -gt 40000 ]; then
    ADDR=$(($ADDR-40000))
  fi
  
  SLAVES["$IDX"]=$SLAVE
  ADDRESSES["$IDX"]=$ADDR
  DIVISORS["$IDX"]=$DIV 
  REGNAMES["$IDX"]=$NAME
  NUMTYPE["$IDX"]=$SIGN
done < $VALIDREGLIST
unset IFS
rm $VALIDREGLIST


###############################################################
#
# Main loop: 
# Read registers periodically and save their values to log file
#
###############################################################
while :
  do
  TIME=`date '+%s'`
  DAY=`date '+%Y-%m-%d'`
  HOUR=`date '+%H:%M:%S'`
  DAYLOG=`date '+%y%m%d'`
  LOG="$STORAGE_DIR/$DAYLOG-1.LOG"

   # Calculate start time for next cycle
   NEXT_CYCLE_START=$(date --date="now + $INFRALOOP_DELAY seconds" +%s)

  ##############################################################
  #
  # Create new log file with headers, if needed
  #
  ############################################################
  if [ "$DAY" != "$PREVIOUS_DAY" ]
    then
    if [ ! -e "$LOG" ] # create new log file with headers 
        then
           if [ "$LOG_FORMAT" == "NIBE40" ]; then
               echo "Geomonitor_log_format: $LOG_FORMAT $INFRAREAD_DELAY $INFRALOOP_DELAY" > $LOG
           fi
           #divisors line
           #echo -e -n "divisors\t\t" >> $LOG
           #for D in "${!DIVISORS[@]}"
           #    do
	   #    echo -e -n "\t${DIVISORS[$D]}"  >> $LOG
           #    done
           #echo  >> $LOG
           
           #column names line
           echo -e -n "Date\tTime"  >> $LOG
           for N in "${!REGNAMES[@]}"
               do
               echo -e -n "\t${REGNAMES[$N]}"  >> $LOG
               done
           echo  >> $LOG
    fi
  fi 
  PREVIOUS_DAY=$DAY

  if [ ! -z $API_KEY ]; then 
    # Prepare data to send via WebAPI
    DATA="apikey="$API_KEY
    DATA=$DATA"&plant_id="$PLANTID
    DATA=$DATA"&time="$TIME
    FIRST_REG=1
  fi

  # Read one 16 bit register at a time
  for i in "${!REGNAMES[@]}"
    do
      #echo "CHECKING: $MODBUS_CLIENT $IP_ADDR $(($TCP_BASEPORT+${SLAVES[$i]})) $OPERATION ${SLAVES[$i]} ${ADDRESSES[$i]}"
      TEMPREG=`$MODBUS_CLIENT $IP_ADDR $(($TCP_BASEPORT+${SLAVES[$i]})) $OPERATION ${SLAVES[$i]} ${ADDRESSES[$i]} | grep ^${ADDRESSES[$i]} | cut -d: -f3 | tr -d ' '`
    
      #echo "RESULT: $TEMPREG"
      #$LOGCMD "RESULT: $TEMPREG"

      # Create line with register readings for log file
      REGLINE=`echo -e -n "$REGLINE\t$TEMPREG"`

      # Create line with register readings for WebAPI request.
      if [[ ! -z $API_KEY ]]; then 
        if [[ ! -z $TEMPREG ]]; then
          # Convert 16bit unsigned to signed if needed
          if [ "${NUMTYPE[$i]}" == "S" ]; then
            if [ $TEMPREG -gt $((0x8000)) ]; then
              TEMPREG=$(( $TEMPREG-$((0x10000)) ))
            fi
          fi

          # Values need to be divided if the associated divisor is not 1.
          if [ ${DIVISORS[$i]} -ne 1 ]; then
            TEMPREG=$(echo "scale=1; $TEMPREG/${DIVISORS[$i]}" | bc -l)
          fi
        else
          TEMPREG="error" 
        fi
        
        # Register names need to be converted to DB field names
        REGNAME=${REGNAMES[$i]}
        REGNAME=$(echo $REGNAME | sed 's/ /_/g')
        REGNAME=$(echo $REGNAME | sed 's/-/_/g')
        REGNAME=$(echo $REGNAME | sed 's/\.//g')
        REGNAME=$(echo $REGNAME | awk '{print tolower($0)}')

        # Data format in request is in json format: 
        # json={<regname>:<regvalue>,...}
        if [ $FIRST_REG -eq 1 ]; then
          DATA=$DATA"&json={"$REGNAME":"$TEMPREG
          FIRST_REG=0
        else  
          DATA=$DATA","$REGNAME":"$TEMPREG
        fi
      fi

      sleep $INFRAREAD_DELAY 
    done
  
  # append one WHOLE line to log file
  echo -e "$DAY\t$HOUR$REGLINE" >> $LOG
  REGLINE=''

  # Send data from latest register reading to server via WebAPI
  if [ ! -z $API_KEY ]; then 
    DATA=$DATA"}"
    #echo "Sending data: $TELEGEA_API/$API_MODULE?$DATA"
    #$LOGCMD "Sending data: $TELEGEA_API/$API_MODULE?$DATA"
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

  # wait for next update period
  TIME_TO_WAIT=$(($NEXT_CYCLE_START-$(date +%s)))
  if [ $TIME_TO_WAIT -gt 0 ]; then
    sleep $TIME_TO_WAIT
  fi

done
