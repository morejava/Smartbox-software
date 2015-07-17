#!/bin/bash
###################################################################
# 
# Description: 
#   Launch a Web browser in kiosk mode on HDMI display.
#   See following Web page for more info:
#   http://www.ediy.com.my/index.php/blog/item/
#        102-raspberry-pi-running-midori-browser-without-a-desktop
#
#   Note: this script needs the following packages installed:
#     - matchbox
#     - x11-xserver-utils
#     - unclutter
#
###################################################################

# Load generic configuration parameters
. /etc/geofox.conf

TIMEOUT=60
RC=1

# Default Web page to load
PLANT_ID=$(hostname | tr -d "[:alpha:]")
WEBPAGE="$TELEGEA_API/monitor.php?plant_id=$PLANT_ID&apikey=$API_KEY"

# Log messages go to syslog
LOGCMD="logger -i -t webbrowser"


$LOGCMD "Starting local display with WEBPAGE $WEBPAGE"

# Some screen settings
/usr/bin/xset -dpms     # disable DPMS (Energy Star) features.
/usr/bin/xset s off     # disable screen saver
/usr/bin/xset s noblank # don't blank the video device

# Hide mouse pointer
/usr/bin/unclutter &

# Launch window manager
# This is needed to make fullscreen work properly
/usr/bin/matchbox-window-manager &   

# TODO: Wait here until TeleGea WebAPI is accessible
# The current quick'n'dirty solution is to check if the TeleGea 
# server is alive. But the WebAPI could be hosted on a different
# server.
while [ $RC -ne 0 ]; do
  #/usr/bin/wget --timeout 10 --no-check-certificate $WEBPAGE
  ping -c 1 -W 5 $RSERVER > /dev/null 2>&1
  RC=$?
  if [ $RC -ne 0 ]; then
    #$LOGCMD "Telegea API not accessible (RC=$RC), retrying in $TIMEOUT seconds"
    $LOGCMD "Telegea server not available (RC=$RC), retrying in $TIMEOUT seconds"
    sleep $TIMEOUT
  fi
done

$LOGCMD "Telegea server is now available, spawning browser"
sleep 1

# Launch browser
/usr/bin/midori -e Fullscreen -a $WEBPAGE

$LOGCMD "Exiting local display"

exit 0
