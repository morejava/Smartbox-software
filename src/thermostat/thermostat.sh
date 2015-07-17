#! /bin/bash
##################################################################################
#
# Author:       Ondrej Wisniewski
#
# Description:  This script is part of the TeleGea smart thermostat package.
#               It implements the main algorithm for controlling a heater or
#               A/C unit.
#
# Usage:        This script is activated at system boot via init script
#               /etc/init.d/thermostat
#
# Last modified: 05/02/2015
#
# Copyright 2013-2015, DEK Italia
# 
# This file is part of the Telegea platform.
# 
# Telegea is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Telegea.  If not, see <http://www.gnu.org/licenses/>.
#
##################################################################################

VERSION="0.8.1"


############################################################
# Parameter definition
############################################################
 
# Load generic configuration parameters
. /etc/geofox.conf

# Get needed modbus slave and register addresses from register file
TEMP_MOD_ADDR=$(cat $REGISTER_LIST | grep "$ROOM_TEMP_REG_NAME" | awk '{print $2}')
TEMP_REG_ADDR=$(cat $REGISTER_LIST | grep "$ROOM_TEMP_REG_NAME" | awk '{print $3}')
# Heating/cooling control 1 (e.g. heating floor, boiler)
HEATCOOL_CTRL1_MOD_ADDR=$(cat $REGISTER_LIST | grep "$HEATCOOL_CTRL1_REG_NAME" | awk '{print $2}')
HEATCOOL_CTRL1_REG_ADDR=$(cat $REGISTER_LIST | grep "$HEATCOOL_CTRL1_REG_NAME" | awk '{print $3}')
# Heating/cooling control 2 (e.g. fan coil)
HEATCOOL_CTRL2_MOD_ADDR=$(cat $REGISTER_LIST | grep "$HEATCOOL_CTRL2_REG_NAME" | awk '{print $2}')
HEATCOOL_CTRL2_REG_ADDR=$(cat $REGISTER_LIST | grep "$HEATCOOL_CTRL2_REG_NAME" | awk '{print $3}')

# Constant definitions
HYST=0.5
CYCLE_PERIOD=30
ON="1"
OFF="2"
READ="1"
WRITE="2"

# Operating parameters
MODBUS_CLIENT_PROG="/usr/local/bin/geomon_modbustcp_client"
TARGET_TEMP_PROG="/usr/local/bin/get_target_temp.py"
SCHEDULE_FILE="/media/data/thermostat.json"
LCD_FILE="/tmp/lcd_line"
DATA_FILE="/tmp/curr_data.json"

# Timestamps for last switch on/off event
ON_TIMESTAMP=$(date +%s)
OFF_TIMESTAMP=0

LAST_RC=0
TEMP_ERR_CNT=0
CTRL_ERR_CNT=0
MAX_ERROR=5
CURRENT_TEMP=0.0
TARGET_TEMP=0.0
CTRL_STATE=$OFF
CTRL1_STATE=$OFF
CTRL2_STATE=$OFF

# If control device info is present, the devices are enabled by default
if [[ -n "$HEATCOOL_CTRL1_REG_NAME" ]]; then
   HEATCOOL_CTRL1_ENABLED=1
else
   HEATCOOL_CTRL1_ENABLED=0
fi
if [[ -n "$HEATCOOL_CTRL2_REG_NAME" ]]; then
   HEATCOOL_CTRL2_ENABLED=1
else
   HEATCOOL_CTRL2_ENABLED=0
fi


#########################################################
#
# Function:    set_control()
# Description: controls the heating or cooling device(s)
#              (switches on/off)
# Parameters:  $1 - season mode (heating/cooling)
#              $2 - control state (on/off)
# 
#########################################################
function set_control {

   MODE=$1
   STATE=$2

   # Switch on/off heating/cooling control device 1 (if enabled)
   if [ $HEATCOOL_CTRL1_ENABLED -ne 0 ]; then
      $MODBUS_CLIENT_PROG $MODBUS_DEVICE_IP 500$HEATCOOL_CTRL1_MOD_ADDR $WRITE $HEATCOOL_CTRL1_MOD_ADDR $HEATCOOL_CTRL1_REG_ADDR $STATE >/dev/null 2>&1
      CTRL1_STATE=$STATE
      $LOGCMD "Switch heating/cooling control device 1"
   fi
   # Switch on/off heating/cooling control device 2 (if enabled)
   if [ $HEATCOOL_CTRL2_ENABLED -ne 0 ]; then
      $MODBUS_CLIENT_PROG $MODBUS_DEVICE_IP 500$HEATCOOL_CTRL2_MOD_ADDR $WRITE $HEATCOOL_CTRL2_MOD_ADDR $HEATCOOL_CTRL2_REG_ADDR $STATE >/dev/null 2>&1
      CTRL2_STATE=$STATE
      $LOGCMD "Switch heating/cooling control device 2"
   fi
   CTRL_STATE=$STATE
}

#########################################################
#
# Function:    get_control()
# Description: reads the state of the heating or cooling 
#              device(s)
# Return:      control state (on/off)
# 
#########################################################
function get_control {
   
   CTRL1_STATE=$OFF
   CTRL2_STATE=$OFF

   # Read state of heating/cooling control device (if enabled)
   if [ $HEATCOOL_CTRL1_ENABLED -ne 0 ]; then
      CTRL1_STATE=$($MODBUS_CLIENT_PROG $MODBUS_DEVICE_IP 500$HEATCOOL_CTRL1_MOD_ADDR $READ $HEATCOOL_CTRL1_MOD_ADDR $HEATCOOL_CTRL1_REG_ADDR | tail -1 | cut -d " "  -f 3)
   fi
   # Read state of heating-only control device (if enabled)
   if [ $HEATCOOL_CTRL2_ENABLED -ne 0 ]; then
      CTRL2_STATE=$($MODBUS_CLIENT_PROG $MODBUS_DEVICE_IP 500$HEATCOOL_CTRL2_MOD_ADDR $READ $HEATCOOL_CTRL2_MOD_ADDR $HEATCOOL_CTRL2_REG_ADDR | tail -1 | cut -d " "  -f 3)
   fi
   
   if [[ $CTRL1_STATE -eq $ON || $CTRL2_STATE -eq $ON ]]; then
      return $ON
   else
      return $OFF
   fi
}


# TODO: define trap function for KILL signals to perform cleanup

# Log messages go to syslog
LOGCMD="logger -i -t thermostat"

$LOGCMD "Starting smart thermostat daemon, version $VERSION"


############################################################
# Sanity checks
############################################################

BC=$(which bc)
if [ -z $BC ]; then
   $LOGCMD "ERROR: bc tool not installed"
   exit 1
fi

if [ ! -x $TARGET_TEMP_PROG ]; then
   $LOGCMD "ERROR: $TARGET_TEMP_PROG script not found"
   exit 1
fi

if [[ -z $TEMP_MOD_ADDR || -z $TEMP_REG_ADDR ]]; then
   $LOGCMD "ERROR: One or more Modbus slave/register addresses missing"
   exit 1
fi

if [[ "$SEASON_MODE" != "heating" && "$SEASON_MODE" != "cooling" ]]; then
   $LOGCMD "ERROR: invalid season mode ($SEASON_MODE)"
   exit 1
fi

if [[ $HEATCOOL_CTRL1_ENABLED -eq 0 && $HEATCOOL_CTRL2_ENABLED -eq 0 ]]; then
   $LOGCMD "ERROR: no heating/cooling device enabled"
   exit 1
fi

# Create data file and set owner/permissions to make it writeble from GUI
touch $DATA_FILE
chown www-data:www-data $DATA_FILE
chmod 666 $DATA_FILE

# Set owner/permissions for schedule file to make it writeble from GUI
chown www-data:www-data $SCHEDULE_FILE
chmod 666 $SCHEDULE_FILE

$LOGCMD "temperature sensor is at slave $TEMP_MOD_ADDR, reg $TEMP_REG_ADDR"
if [ $HEATCOOL_CTRL1_ENABLED -ne 0 ]; then
   $LOGCMD "heating/cooling control device 1 is at slave $HEATCOOL_CTRL1_MOD_ADDR, reg $HEATCOOL_CTRL1_REG_ADDR"
fi
if [ $HEATCOOL_CTRL2_ENABLED -ne 0 ]; then
   $LOGCMD "heating/cooling control device 2 is at slave $HEATCOOL_CTRL2_MOD_ADDR, reg $HEATCOOL_CTRL2_REG_ADDR"
fi
$LOGCMD "season mode is $SEASON_MODE"


############################################################
# Main loop
############################################################
while [ 1 ]; do

   #########################################################
   # Write JSON file with current data for touchscreen
   #########################################################
   echo "{\"tempc\":\"$CURRENT_TEMP\",\"tempt\":\"$TARGET_TEMP\",\"state1\":\"$CTRL1_STATE\",\"state2\":\"$CTRL2_STATE\",\"mode\":\"$SEASON_MODE\"}" > $DATA_FILE
   

   #########################################################
   # Handle cycle timer
   #########################################################
   wait $SLEEP_PID
   sleep $CYCLE_PERIOD &
   SLEEP_PID=$!
   NOW_TIMESTAMP=$(date +%s)


   #########################################################
   # Measure current temperature
   #########################################################
   CURRENT_TEMP=$($MODBUS_CLIENT_PROG $MODBUS_DEVICE_IP 500$TEMP_MOD_ADDR $READ $TEMP_MOD_ADDR $TEMP_REG_ADDR | tail -1 | cut -d " "  -f 3)
   CURRENT_TEMP=$(echo "scale=1; $CURRENT_TEMP/10" | bc -l)
   #CURRENT_TEMP=$(echo "scale=1; $CURRENT_TEMP-$TEMP_OFFSET" | bc -l)

   # Check if temperature reading failed
   if [ -z $CURRENT_TEMP ]; then
      TEMP_ERR_CNT=$(($TEMP_ERR_CNT+1))
      if [ $TEMP_ERR_CNT -ge $MAX_ERROR ]; then
         # Too many consecutive failed readings, try to switch off the control unit
         $LOGCMD "Unable to read room temperature, switching off"
         set_control $SEASON_MODE $OFF
         echo "3:Emergency stop" > $LCD_FILE
      fi
      continue
   else
      TEMP_ERR_CNT=0
   fi    
   
   #$LOGCMD "DBG: Current temperature: $CURRENT_TEMP"
   echo "1:Room Temp: $CURRENT_TEMP C" > $LCD_FILE
   sleep 1
   

   #########################################################
   # Get control state (on/off)
   #########################################################
   #CTRL_STATE=$($MODBUS_CLIENT_PROG $MODBUS_DEVICE_IP 500$CTRL_MOD_ADDR $READ $CTRL_MOD_ADDR $CTRL_REG_ADDR | tail -1 | cut -d " "  -f 3)
   get_control
   CTRL_STATE=$?
   #$LOGCMD "DBG: heater state: $CTRL_STATE"
   
   # Check if control state reading failed
   if [ -z $CTRL_STATE ]; then
      CTRL_ERR_CNT=$(($CTRL_ERR_CNT+1))
      if [ $CTRL_ERR_CNT -ge $MAX_ERROR ]; then
         # Too many consecutive failed readings, try to switch off the control unit
         $LOGCMD "Unable to read control state, switching off"
         $MODBUS_CLIENT_PROG $MODBUS_DEVICE_IP 500$CTRL_MOD_ADDR $WRITE $CTRL_MOD_ADDR $CTRL_REG_ADDR $OFF >/dev/null 2>&1      
         echo "3:Emergency stop" > $LCD_FILE
      fi
      continue
   else
      CTRL_ERR_CNT=0
   fi    


   #########################################################
   # Get target temperature
   #########################################################
   TARGET_TEMP=$($TARGET_TEMP_PROG --configFile $SCHEDULE_FILE)
   RC=$?
   case "$RC" in
      0) 
         # Success, got target temp
         echo "2:Goal Temp: $TARGET_TEMP C" > $LCD_FILE
         sleep 1
         echo "3:$SEASON_MODE is $([ $CTRL_STATE -eq 1 ] && echo on || echo off)" > $LCD_FILE
         LAST_RC=$RC
         #$LOGCMD "DBG: Goal temperature: $TARGET_TEMP"
         ;;
      1)
         $LOGCMD "ERROR: Generic error in configuration file"
         LAST_RC=$RC
         TARGET_TEMP=$CURRENT_TEMP
         continue
         ;;
      2)
         $LOGCMD "ERROR: Operation mode not defined in configuration file"
         LAST_RC=$RC
         TARGET_TEMP=$CURRENT_TEMP
         continue
         ;;
      3)
         $LOGCMD "ERROR: Unknown operation mode defined in configuration file"
         LAST_RC=$RC
         TARGET_TEMP=$CURRENT_TEMP
         continue
         ;;
      4|5)
         # Switch off control if it is currently switched on,
         # but ignore this if last operating mode was "off".
         if [[ "$CTRL_STATE" == "$ON" && "$LAST_RC" != "5" ]]; then
            set_control $SEASON_MODE $OFF
            $LOGCMD "Switch off control"
         fi
         echo "2:                    " > $LCD_FILE
         sleep 1
         echo "3:Thermostat disabled " > $LCD_FILE
         LAST_RC=$RC
         TARGET_TEMP=$CURRENT_TEMP
         continue
         ;;
      *)
         $LOGCMD "ERROR: Unknown exit code from $TARGET_TEMP_PROG"
         LAST_RC=$RC
         TARGET_TEMP=$CURRENT_TEMP
         continue
         ;;
   esac   
   

   #########################################################
   # Safety checks
   #########################################################
   # Check if max temp is exceeded
   if [ $(echo "$TARGET_TEMP>$MAX_TEMP" | bc) -eq 1 ]; then
      TARGET_TEMP=$MAX_TEMP
   fi
   # Check if min temp is exceeded
   if [ $(echo "$TARGET_TEMP<$MIN_TEMP" | bc) -eq 1 ]; then
      TARGET_TEMP=$MIN_TEMP
   fi
   # Check if max operating time is exceeded
   if [ "$CTRL_STATE" == "$ON"  ]; then
      if [ $(($NOW_TIMESTAMP-$ON_TIMESTAMP)) -ge $(($MAX_OPERATING_TIME*60)) ]; then
         # Max operating time is exceeded, switch off the control unit
         set_control $SEASON_MODE $OFF
         OFF_TIMESTAMP=$(date +%s)
         $LOGCMD "Exceeded max operating time, switching off"
         continue
      fi
   fi
   

   #########################################################
   # Apply hysteresis based on plant mode and control state
   #########################################################
   if [ "$SEASON_MODE" == "heating" ]; then 
      if [ "$CTRL_STATE" == "$OFF"  ]; then 
         TARGET_TEMP_H=$(echo "scale=1; $TARGET_TEMP-$HYST" | bc -l)
      else
         TARGET_TEMP_H=$(echo "scale=1; $TARGET_TEMP+$HYST" | bc -l)
      fi
   else
      if [ "$CTRL_STATE" == "$OFF"  ]; then 
         TARGET_TEMP_H=$(echo "scale=1; $TARGET_TEMP+$HYST" | bc -l)
      else
         TARGET_TEMP_H=$(echo "scale=1; $TARGET_TEMP-$HYST" | bc -l)
      fi
   fi
   
   
   #########################################################
   # Heater/AC control
   #########################################################
   if [ $(echo "$CURRENT_TEMP<$TARGET_TEMP_H" | bc) -eq 1 ]; then
      # Current temperature is lower than target temperature.
      # Switch the heater on or AC off
      if [ "$SEASON_MODE" == "heating" ]; then
         if [ "$CTRL_STATE" == "$OFF" ]; then
            if [ $(($NOW_TIMESTAMP-$OFF_TIMESTAMP)) -ge $(($MIN_IDLE_TIME*60)) ]; then
               # Switch heater on
               set_control $SEASON_MODE $ON
               ON_TIMESTAMP=$(date +%s)
               $LOGCMD "Switch on heating at $CURRENT_TEMP C"
            else
               $LOGCMD "Cannot switch on heating, min idle time not yet reached"
            fi
         fi
      else
         if [ "$CTRL_STATE" == "$ON" ]; then
            # Switch AC off
            set_control $SEASON_MODE $OFF
            OFF_TIMESTAMP=$(date +%s)
            $LOGCMD "Switch off cooling at $CURRENT_TEMP C"
         fi
      fi
   else
      # Current temperature is higher than target temperature.
      # Switch the heater off or AC on
      if [ "$SEASON_MODE" == "heating" ]; then
         if [ "$CTRL_STATE" == "$ON" ]; then
            # Switch heater off
            set_control $SEASON_MODE $OFF
            OFF_TIMESTAMP=$(date +%s)
            $LOGCMD "Switch off heating at $CURRENT_TEMP C"
         fi
      else
         if [ "$CTRL_STATE" == "$OFF" ]; then
            if [ $(($NOW_TIMESTAMP-$OFF_TIMESTAMP)) -ge $(($MIN_IDLE_TIME*60)) ]; then
               # Switch AC on
               set_control $SEASON_MODE $ON
               ON_TIMESTAMP=$(date +%s)
               $LOGCMD "Switch on cooling at $CURRENT_TEMP C"
            else
               $LOGCMD "Cannot switch on cooling, min idle time not yet reached"
            fi
         fi
      fi
   fi

done

exit 0
