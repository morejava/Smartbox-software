#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Handles the on board status led 
#
# Usage:         This script is called from the led_on, led_off scripts in 
#                /etc/init.d/network/ifup.d/ and ifdown.d/
#                when the interface comes up and goes down
#
# Last modified: 03/11/2015
#
##################################################################################


# Log messages go to syslog
LOGCMD="logger -i -t ledctl"

$LOGCMD "up/down event for $IFACE"

if [[ "$IFACE" == "wlan0" || "$IFACE" == "eth0" ]]; then
   case "$1" in
      on)
         # Start blinking led
         echo timer > /sys/class/leds/led0/trigger
         ;; 
      off)
         # Led reports flash memory access
         echo mmc0 > /sys/class/leds/led0/trigger   
         ;;
      *)
         $LOGCMD "ERROR: unknown parameter"
   esac
fi

exit 0
