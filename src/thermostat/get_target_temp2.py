#!/usr/bin/env python

'''
Created on Jun 12, 2014

This script parses a JSON format configuration file, including a set of parameters defining a weekly 
schedule and returns the temperature value defined for the current time.

The file format is as follows:
{
"operation_mode":<mode>,
"daily_schedule":
[
   {"weekday":"monday","times_of_operation":[{"start":<start>,"stop":<stop>,"temp":<temp>}, ... ]},
   {"weekday":"tuesday","times_of_operation":[{"start":<start>,"stop":<stop>,"temp":<temp>}, ... ]},
   {"weekday":"wednesday","times_of_operation":[{"start":<start>,"stop":<stop>,"temp":<temp>}, ... ]},
   {"weekday":"thursday","times_of_operation":[{"start":<start>,"stop":<stop>,"temp":<temp>}, ... ]},
   {"weekday":"friday","times_of_operation":[{"start":<start>,"stop":<stop>,"temp":<temp>}, ... ]},
   {"weekday":"saturday","times_of_operation":[{"start":<start>,"stop":<stop>,"temp":<temp>}, ... ]},
   {"weekday":"sunday","times_of_operation":[{"start":<start>,"stop":<stop>,"temp":<temp>}, ... ]}
],
"temp_override":{"time_stamp":<timestamp>,"temp":<temp>},
"immediate":{"temp":<temp>},
"off":"off"
}


Exit codes:
    0 - Success
    1 - ERROR: madatory filed missing in configuration file
    2 - ERROR: operation mode not defined in configuration file
    3 - ERROR: unknown operation mode defined in configuration file
    4 - INFO:  no target temperature defined for the current time
    5 - INFO:  operation mode is off
    
Usage: JsonParser.py --configFile=CONF_FILE

@author: Daniele Casini, Ondrej Wisniewski

Copyright 2013-2015, DEK Italia

This file is part of the Telegea platform.

Telegea is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Telegea.  If not, see <http://www.gnu.org/licenses/>.

'''

import json
import time
import datetime
from optparse import OptionParser

# Parse command line
parser = OptionParser()
parser.add_option("--configFile",dest="CONF_FILE",help="JSON format configuration file")
(options, args) = parser.parse_args()
CONF_FILE = options.CONF_FILE

# Check for mandatory argument
if CONF_FILE==None:
        print "At least one mandatory argument is missing\n"
        parser.print_help()  
        exit(-1)

# Define possible operating modes
KNOWN_OP_MODES = ("daily_schedule", "immediate" , "temp_override", "off")

# Define timeout of temp_override mode (in seconds)
TEMP_OVERRIDE_TMO = 2*3600


# Function which searches the daily schedule and returns 
# the temperature applicable to the current time
###############################################################
def get_temp_from_schedule(elem):
   
    temp  = None
    start = None
    stop  = None
    
    # Get current time and date
    time_stamp = time.time()
    (tm_year,tm_mon,tm_mday,tm_hour,tm_min,tm_sec,tm_wday,tm_yday,tm_isdst) = time.localtime()
    today = datetime.date.today().strftime("%A").lower()

    if not elem == None:
       # Read all weekday entries
       for day in elem:
          if day.get("weekday") == today:
             
             # Get todays operating times            
             times_of_operation = day.get("times_of_operation")
             for item in times_of_operation:
                t1 = item.get("start").split(":")
                t1h = int(t1[0])
                t1m = int(t1[1])
                t2 = item.get("stop").split(":")  
                t2h = int(t2[0])
                t2m = int(t2[1])
                
                print "todays (" + today + ") programmed period: " + str(t1) + " - " + str(t2)
                
                start_time_stamp = time.mktime((tm_year,tm_mon,tm_mday,t1h,t1m,0,tm_wday,tm_yday,tm_isdst))
                stop_time_stamp = time.mktime((tm_year,tm_mon,tm_mday,t2h,t2m,59,tm_wday,tm_yday,tm_isdst))
                
                # Check if time_stamp is within the given time slot
                if time_stamp >= start_time_stamp and time_stamp < stop_time_stamp:          
                   # Save the the target temperature
                   temp  = item.get("temp")
                   start = start_time_stamp
                   stop  = stop_time_stamp
                   print "programmed temperature: " + temp
     
    print ("get_temp_from_schedule returns", temp, start, stop)
    return (temp, start, stop)
   
   
# Open configuration file
with open(CONF_FILE) as json_file:   
    
    # Deserialize json formatted file to a Python object 
    json_data = json.load(json_file)       
    
    # Get the current operation mode
    op_mode = json_data.get("operation_mode")
    print "op_mode is " + str(op_mode)
    
    # Perform sanity checks
    ###################################################
    
    # Check if mandatory operation mode is defined
    if op_mode == None:
       print "ERROR: operation mode not defined"
       exit (2)
        
    # Check if operation mode is one of the known modes
    if not op_mode in KNOWN_OP_MODES:
       print "ERROR: operation mode is unknown"
       exit (3)
        
    # Check if needed data is present in configuration file
    elem = json_data.get(op_mode) 
    if elem == None:
       print "ERROR: data for operation mode " + op_mode + " not present"
       exit (1)

    # Get needed data according to operation mode   
    ###################################################
    
    # Daily schedule:
    # Get temperature defined for the current time
    if op_mode == "daily_schedule":
        (temp, start, stop) = get_temp_from_schedule(elem)
        
    # Temporary override:
    # Override temperature defined for the current time
    elif op_mode == "temp_override":
        (temp, start, stop) = get_temp_from_schedule(json_data.get("daily_schedule"))
        time_stamp = elem.get("time_stamp")
        if temp is None:
            # No temperature is defined for current time, 
            # check if temp_override has timed out
            if (time_stamp > time.time() - TEMP_OVERRIDE_TMO):
                temp = elem.get("temp")
                print "Overriding no temperature with " + temp
        else:
            # A temperature is defined for the current time, 
            # check if temp_override was set within this timeslot
            if (time_stamp >= start and time_stamp < stop):
                temp = elem.get("temp")
                print "Overriding scheduled temperature with " + temp
       
    # Immediate
    # Set immediate temperature value
    elif op_mode == "immediate":
        temp = elem.get("temp") 
               
    # Off
    # Thermostat is off
    elif op_mode == "off":
        exit (5)

# Check if any temperature was found
if temp == None:
    exit (4)

# Return the temperature value applicable for the current time
print (temp)
    