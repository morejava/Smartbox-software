#!/bin/bash
###################################################################
# 
# Description: 
#   Launch a Web browser in kiosk mode on TFT touchscreen and
#   display the Thermostat GUI.
#   See following Web page for more info:
#   http://www.ediy.com.my/index.php/blog/item/
#        102-raspberry-pi-running-midori-browser-without-a-desktop
#
#   Note: this script needs the following packages installed:
#     - matchbox
#     - x11-xserver-utils
#
###################################################################

# Load generic configuration parameters
. /etc/geofox.conf

# Default Web page to load
WEBPAGE="http://$THERMOSTAT_DEVICE_IP/therm/index.php"

X_PID=0
M_PID=0

function cleanup()
{ 
   kill $M_PID
   kill $X_PID
   echo "Exiting thermostat_gui"
   exit 0
}

# Run cleanup when we are terminated
trap cleanup INT KILL TERM

# Launch window manager
# This is needed to make fullscreen work properly
FRAMEBUFFER=/dev/fb1 /usr/bin/xinit /usr/bin/matchbox-window-manager -use_cursor no &
X_PID=$(echo $!)
echo "X started with pid $X_PID"

# Wait for window manager to start up
sleep 1

# Some screen settings
DISPLAY=:0 /usr/bin/xset -dpms     # disable DPMS (Energy Star) features.
DISPLAY=:0 /usr/bin/xset s off     # disable screen saver
DISPLAY=:0 /usr/bin/xset s noblank # don't blank the video device

# Swap x and y axis (needed if screen is rotated 90° or 270°)
DISPLAY=:0 xinput --set-prop 'ADS7846 Touchscreen' 'Evdev Axes Swap' 1
#DISPLAY=:0 xinput --set-prop 'stmpe-ts' 'Evdev Axes Swap' 1
# Invert Y axis on touchscreen (needed if screen is rotated 270°)
DISPLAY=:0 xinput --set-prop 'ADS7846 Touchscreen' 'Evdev Axis Inversion' 0 1
#DISPLAY=:0 xinput --set-prop 'stmpe-ts' 'Evdev Axis Inversion' 0 1

# Launch browser
DISPLAY=:0 /usr/bin/midori -e Fullscreen -a $WEBPAGE &
M_PID=$(echo $!)
echo "Midori started with pid $M_PID"

# Wait for termination
while [ 1 ]; do sleep 1; done

exit 0
