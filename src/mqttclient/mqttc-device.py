#!/usr/bin/env python
###############################################################################
#
# MQTT client for the Smartbox device (CMD Receiver)
#
# Author: Ondrej Wisniewski
#
# Features:
# - handles commands sent from the Telegea server to the Smartbox
#
# Changelog:
#   02-10-2015: Initial version
# 
# Copyright 2015, DEK Italia
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
#
# Initialization:
#  * client connects to broker
#  * client subscribes to command messages
#  * client publishes "I'm alive" message
#  * client waits for command
#  
# Loop:
#  * client receives command from server
#  * client performs command
#  * client publishes commands reply
#
# Uses the Paho MQTT Python client library 
# https://www.eclipse.org/paho/clients/python/
# Install with "pip install paho-mqtt"
#
###############################################################################

import paho.mqtt.client as mqtt
import argparse
import subprocess
import signal
import syslog
#import time
#import socket
#import sys

version="0.2"

# Constant definitions
clientId="smartbox"
caCerts="/root/mqttclient/ca.crt"
portbase=5000


# Command handler for Modbus requests
def do_modbusRequest(cprm):
    
    # command format:
    # 1 <modAddr> <regAddr> [<regVal>]
    
    syslog.syslog(syslog.LOG_NOTICE, "Received Modbus request")

    operation = "1"
    regVal = ""
    
    if len(cprm) > 2:
        # At least 3 parameters present
        modAddr = cprm[1]
        regAddr = cprm[2]
        tcpPort = str(int(modAddr)+portbase)
        #print("modAddr="+modAddr)
        #print("regAddr="+regAddr)
        
        if len(cprm) > 3:
            # At least 4 parameters: write operation
            regVal = cprm[3]
            operation = "2"
            #print("regVal="+regVal)
    
        # Call modbustcp client to perform command
        try:
            reply = subprocess.check_output([extCmd, ipAddr, tcpPort, operation, modAddr, regAddr, regVal])
        except subprocess.CalledProcessError:
            reply = "ERROR"
            syslog.syslog(syslog.LOG_ERR, "Command execution failed")

        # Publish reply with command result
        client.publish(topic_reply, reply, 0, False)

    else:
        syslog.syslog(syslog.LOG_ERR, "Command missing required parameters")
        
    return;
    

# Command handler for Service command
def do_serviceCmd(cprm):
    syslog.syslog(syslog.LOG_NOTICE, "Received Service command")
    
    # TODO: add service command handling here
    
    # Publish reply with command result
    reply = "OK"
    client.publish(topic_reply, reply, 0, False)
    
    return;


# Command handler for future use
def do_whatever(cprm):
    syslog.syslog(syslog.LOG_NOTICE, "Received Future use command")
    
    # Publish reply with command result
    reply = "OK"
    client.publish(topic_reply, reply, 0, False)
    
    return;


# Command types
cmdTypeHandler = {
    "1": do_modbusRequest,
    "2": do_serviceCmd,
    "3": do_whatever
}


# The callback for when the client receives a CONNACK response from the broker.
def on_connect(client, userdata, rc):
    syslog.syslog(syslog.LOG_NOTICE, "Connected to MQTT broker")

    # Subscribing in on_connect() means that if we lose the connection and
    # reconnect then subscriptions will be renewed.
    client.subscribe(topic_command)
    
    # Send alive message to broker so subscribers know we are connected
    client.publish(topic_will, msg_alive, 0, False)
    
    return;


# The callback for when the client disconnects from the broker.
def on_disconnect(client, userdata, rc):
    if rc != 0:
        syslog.syslog(syslog.LOG_NOTICE, "Disconnected unexpectedly from MQTT with code " + str(rc))
    return;


# The callback for when a PUBLISH message is received from the broker.
def on_message(client, userdata, msg):
    syslog.syslog(syslog.LOG_NOTICE, "Received command from broker: " + str(msg.payload))
    cmdstr = str(msg.payload)
    
    # Check payload length
    if len(cmdstr) > 0:
        # get parameters
        cmdprm = cmdstr.split()
        cmdType = cmdprm[0]
    
        # Handle command according to cmd type
        try:
            cmdTypeHandler[cmdType](cmdprm)
        except KeyError:
            syslog.syslog(syslog.LOG_ERR, "Unknown command type: %s" % cmdType)
    
    return;


# The signal handler for the TERM and INT signal
def on_signal(signum, frame):
    # Disconnect the client, this will exit from the main loop and
    # subsequently terminate the client.
    # Attention: In this case no will is sent to our subscribers!
    #            (is this what we want or should we publish the will
    #             message explicitely here before disconnecting?)
    client.disconnect()
    syslog.syslog(syslog.LOG_NOTICE, "Exiting MQTT client daemon")


# Parse command line 
parser = argparse.ArgumentParser()
parser.add_argument('--brokerAddr', '-b', type=str, help='Broker address', required=True)
parser.add_argument('--plantId', '-p', type=int, help='Plant Id', required=True)
parser.add_argument('--extCmd',  '-c', type=str, help='ModbusTCP master command', required=True)
parser.add_argument('--ipAddr',  '-i', type=str, help='ModbusTCP slave IP address', required=True)
args = parser.parse_args()

# Get parameters
brokerAddr = args.brokerAddr
plantId  = str(args.plantId)
extCmd   = args.extCmd
ipAddr   = args.ipAddr
clientId = clientId + plantId

# Init syslog
syslog.openlog("mqttc", syslog.LOG_PID|syslog.LOG_CONS, syslog.LOG_USER)
syslog.syslog(syslog.LOG_NOTICE, "Starting MQTT client daemon (version "+version+")")
syslog.syslog(syslog.LOG_NOTICE, "  with parameters brokerAddr="+brokerAddr+" plantId="+plantId+" extCmd="+extCmd+" ipAddr="+ipAddr)

# Define topics 
topic_command = "smartbox/" + plantId + "/command"
topic_reply   = "smartbox/" + plantId + "/reply"
topic_will    = "smartbox/" + plantId + "/deadoralive"

# Define messages to publish
msg_alive     = "plant " + plantId + " is alive"
msg_dead      = "plant "+ plantId + " is dead"

# Init signal handler
signal.signal(signal.SIGTERM, on_signal)
signal.signal(signal.SIGINT, on_signal)

# Init mqtt client object    
client = mqtt.Client(clientId)
client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message
client.will_set(topic_will, msg_dead, 0, False)
client.tls_set(ca_certs=caCerts)


# Handle first connection here ourselfs
# We leave this here as reference only, will handle first 
# connection in the m,ain client.loop_forever() instead
#########################################################
#connected = False
#while connected==False:
#    try:
#        connected = True
#        # Connect to the Telegea mqtt broker
#        client.connect(brokerAddr, 1883, 60)
#    except socket.error:
#        err = sys.exc_info()[0]
#        syslog.syslog(syslog.LOG_ERR, "Error %s reconnecting to broker" % err)
#        connected = False
#        time.sleep(10)
#        syslog.syslog(syslog.LOG_DEBUG, "DEBUG: try again")
#########################################################

client.connect_async(brokerAddr, 8883, 60)

# Blocking call that processes network traffic, dispatches callbacks and
# handles reconnecting (also for first connection).
client.loop_forever(retry_first_connection=True)
