#!/usr/bin/env python

'''
Created on Jun 12, 2014

This script parses a JSON format configuration file, including a set of parameters defined by the user via an intuitive Web GUI on the TeleGea server 
(e.g operation modes, target temperatures), and extract the target temperature set previously.

Error codes:
    0 - Success
    1 - Generic error in configuration file
    2 - Operation mode not defined in configuration file
    3 - Unknown operation mode defined in configuration file
    4 - No target temperature defined for the current time
    5 - Operation mode is off
    
Usage: JsonParser.py [options]

Options:

  -h, --help            show this help message and exit
  --configFile=CONF_FILE
                        JSON format configuration file
Example:

didancas@dekit-laptop14:~/workspace/Python/Telegea$ ./JsonParser.py --configFile "/home/didancas/Desktop/thermostat.json"

@author: Daniele Casini

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

bold = "\033[1m"
red = "\033[31m"
reset = "\033[0;0m"

parser = OptionParser()
parser.add_option("--configFile",dest="CONF_FILE",help="JSON format configuration file")
 
(options, args) = parser.parse_args()
CONF_FILE = options.CONF_FILE

#Check the mandatory argument
if CONF_FILE==None:
        print bold+red+"At least one mandatory argument is missing\n"+reset 
        parser.print_help()  
        exit(-1)

with open(CONF_FILE) as json_file:
    
    # Return current local time and date
    (tm_year,tm_mon,tm_mday,tm_hour,tm_min,tm_sec,tm_wday,tm_yday,tm_isdst) = time.localtime()
    curr_time_stamp = time.mktime((tm_year,tm_mon,tm_mday,tm_hour,tm_min,tm_sec,tm_wday,tm_yday,tm_isdst))       
    weekday = datetime.date.today().strftime("%A").lower()
    
    # Deserialize json_file to a Python object 
    json_data = json.load(json_file)       
    
    # Return the current operation mode. If it is not defined exit.
    op_mode = json_data.get("operation_mode")
    if op_mode == None:
        exit (2)
        
    # If operation mode is not defined in configuration file exit.           
    mandatories = ["daily_schedule", "immediate" , "temp_override", "off"]
    if not op_mode in mandatories:
        exit (3)
   
    # Extract the target temperature according to current operation mode and local time. 
    # If operation mode is equal to "daily schedule", an algorithm allows to select a target temperature comparing 
    # the local time with the scheduled time slots. 
    # On the contrary, if operation mode is equal to "temp_override" and the following conditions are met:
    # 1. the scheduled date is equal to current date
    # 2. the scheduled time is included in a time-slot (i.e [start, stop])  
    # then it returns the temperature reported for the rest of time-slot duration including the current time.
    # Otherwise, an error code will be returned (i.e 4  (No target temperature defined for the current time) )
    
    if op_mode == "daily_schedule" or op_mode == "temp_override":
        elem = json_data.get("daily_schedule")
        if elem == None:
           exit (1)
           
        for day in elem:
            if day.get("weekday") == weekday:
                c = day.get("times_of_operation")
                for item in c:
                    t1 = item.get("start").split(":")
                    t1h = int(t1[0])
                    t1m = int(t1[1])
                    t2 = item.get("stop").split(":")  
                    t2h = int(t2[0])
                    t2m = int(t2[1])
                    
                    #(tm_year,tm_mon,weekday,tm_hour,tm_min,tm_sec,tm_wday,tm_yday,tm_isdst) = time.localtime()
                    start_time_stamp = time.mktime((tm_year,tm_mon,tm_mday,t1h,t1m,1,tm_wday,tm_yday,tm_isdst))
                    stop_time_stamp = time.mktime((tm_year,tm_mon,tm_mday,t2h,t2m,59,tm_wday,tm_yday,tm_isdst))
                    
                    # Check If current time is included within the time-slot  
                    if curr_time_stamp >= start_time_stamp and curr_time_stamp < stop_time_stamp:          
                        # Return the target temperature             
                        if op_mode == "daily_schedule":
                            temp = item.get("temp")
                            break
                        # Check if scheduled time is included within the time-slot
                        elif op_mode == "temp_override":    
                            elem = json_data.get("temp_override")
                            sched_time_stamp = elem.get("time_stamp")           
                            if (sched_time_stamp >= start_time_stamp and sched_time_stamp < stop_time_stamp):                            
                                temp = elem.get("temp")
                            else:
                                temp = item.get("temp")
    elif op_mode == "immediate":
        elem = json_data.get("immediate")            
        if elem == None:
           exit (1)
        temp = elem.get("temp") 
                
    elif op_mode == "off":
        exit (5)

if 'temp' not in locals():
    exit (4)

# return the temperature value
print (temp)
    