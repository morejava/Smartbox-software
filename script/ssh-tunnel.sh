#! /bin/bash
##################################################################################
#
# Author:        Ondrej Wisniewski
#
# Description:   Establishes a permanent reverse ssh tunnel to the remote server.
#                This is the only communication channel that we have to reach the
#                device from the server. Therefore it is extremely important that
#                this channel is always active and that it will always be restored
#                automatically after an interruption.
#
# Usage:         This script is started automatically by the init script ssh-tunnel
#
# Parameter:     None
#
# Last modified: 10/10/2014 
#
##################################################################################

# Load generic configuration parameters
. /etc/geofox.conf

# The remote port to be opened on the server is calulated from a base plus the
# individual device number which is the digit at the end of the hostname
PORTBASE=3000
PORTOFFSET=$PLANTID
REMOTEPORT=$(($PORTBASE+$PORTOFFSET))
RETRY_PERIOD=60
SSH_PID=0

# Log messages go to syslog
LOGCMD="logger -i -t ssh-tunnel"

function cleanup()
{ 
   # Make sure the ssh tunnel is killed before exiting
   if [ $SSH_PID -ne 0 ]; then
      $LOGCMD "Stopping SSH tunnel" 
      kill $SSH_PID
   fi
   $LOGCMD "Exiting" 
   exit 0
}

# Run cleanup when we are terminated
trap cleanup INT KILL TERM

# Define NIC name according to connection type
case "$CONNECTION" in
    CABLE)
	NIC="eth0"
        ;;
    WLAN)
	NIC="wlan0"
        ;;
    GPRS)
	NIC="ppp0"
        ;;
    *)
	$LOGCMD "ERROR: unknown connection type $CONNECTION, exiting"
	exit 1
        ;;
esac

IFOPSTATE="/sys/class/net/$NIC/operstate"

$LOGCMD "Starting ssh-tunnel daemon (using NIC $NIC)"

# In order to detect a broken ssh connection, the keep alive messages need to be
# enabled both on the client and on the server side.
# server: add "ClientAliveInterval 20" and "ClientAliveCountMax 2" to /etc/ssh/sshd_config
# client: add "ServerAliveInterval 20" and "ServerAliveCountMax 2" to /etc/ssh/ssh_config

# We establish the ssh tunnel to the remote server and try again when
# a broken connection is detected. We never exit from this loop.
while [ 1 ]
do   
   # Check if network interface is up
   if [ "$(cat $IFOPSTATE)" == "up" ]; then
   
      # Start ssh connection, will not return from this call unless connection breaks
      $LOGCMD "Establishing reverse ssh tunnel for port $REMOTEPORT on server $RSERVER"
      ssh -p $RPORT -N -R $REMOTEPORT:localhost:22 $RLOGIN@$RSERVER &
      SSH_PID=$!
 
      # Wait for ssh command to return
      wait $SSH_PID
      SSH_PID=0
      
      # Connection is broken
      $LOGCMD "Broken connection to server detected."
   else
      $LOGCMD "Network interface $NIC is down."
   fi
   
   # Wait before retry
   $LOGCMD "Retrying in $RETRY_PERIOD s"
   sleep $RETRY_PERIOD
done

exit 0
