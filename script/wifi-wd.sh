#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Checks that the Wifi connection to the local router is working
#                and restarts the Wifi interface if the connection is down for 
#                too long.
#                If the connection cannot be recovered even with this method,
#                an emergency reboot will be performed.
#
# Usage:         This script is called from the wifi-watchdog script in 
#                /etc/init.d/network/ifup.d/ 
#                when the interface comes up
#
# Last modified: 10/12/2014
#
##################################################################################

# Every PINGPERIOD sec a ping is performed and if they fail                                                                                                                                                     
# for MAX_ERR consecutive times, a reboot will be performed
# or we sleep for SLEEPPERIOD sec.
PINGPERIOD=60
SLEEPPERIOD=600
MAX_ERR=3
ERR_CNT=0
RST_CNT=0

# Define if an emergency reboot should be performed on error
REBOOT_ON_ERROR=0

PIDFILE="/tmp/wifi-wd.pid"

# Log messages go to syslog
LOGCMD="logger -i -t wifiwatchdog"

# Wifi interface
#WLAN="wlan0"
WLAN=$(ifconfig | grep wlan | awk '{ print $1 }')

# Check that the interface that came up is the Wifi 
if [ "$IFACE" != "$WLAN" ]; then
   $LOGCMD "Wifi watchdog called for other interface $IFACE"
   exit 1
fi 

# Check if another instance is already running
if [ -f $PIDFILE ]; then
   $LOGCMD "Wifi watchdog already running with pid $(cat $PIDFILE)"
   exit 2
fi

# Create pidfile and make sure it is removed when exiting
echo $$ > $PIDFILE
trap "{ rm -f $PIDFILE; $LOGCMD Exiting; exit 0; }" INT KILL TERM

# Find out what our gateway is
#GATEWAY=$(route | grep "default" | awk '{print $2}')
GATEWAY=$(/sbin/ip route | grep $WLAN | awk '/default/ { print $3 }')

# Wait for syslog to become available
sleep 10

$LOGCMD "Starting Wifi watchdog on $WLAN"
$LOGCMD "Periodically checking connection to gateway $GATEWAY"

while [ 1 ]
do
   # check connection by pinging the gateway
   ping -c 1 -W 5 -I $WLAN $GATEWAY > /dev/null 2>&1
  
   if [ $? -ne 0 ]; then
      # no ping response, count errors
      ERR_CNT=$(($ERR_CNT+1))

      if [ $ERR_CNT -ge $MAX_ERR ]; then
         ERR_CNT=0
         RST_CNT=$(($RST_CNT+1))
         
         if [ $RST_CNT -ge $MAX_ERR ]; then
            if [ $REBOOT_ON_ERROR -ne 0 ]; then
            
               # reboot the system
               $LOGCMD "Wifi connection reset did not work, rebooting system"     
               /sbin/reboot
            else
            
               # just sleep for a while
               sleep $SLEEPPERIOD
               ERR_CNT=0   
               RST_CNT=0               
            fi
         else

            # bring up again Wifi interface
            $LOGCMD "Wifi connection is down for too long, restarting interface"
            /sbin/ifdown $WLAN
            sleep 5
            /sbin/ifup --force $WLAN
         fi
      fi
   else
      # ping succeeded, connection is up
      ERR_CNT=0   
      RST_CNT=0 
   fi
  
   # wait for next cycle
   sleep $PINGPERIOD

done

exit 0
