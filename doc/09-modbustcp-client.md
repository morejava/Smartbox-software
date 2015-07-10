# Modbus TCP client

## Description

This module is implemented as a command line tool and is used to query the different server modules for values they acquire via the supported interfaces. Communication with the server modules is done via the Modbus TCP protocol. Therefore the client program can be invoked both locally on the device or remotely from a different machine, as long as there is a network connection to the device.  

Usually this program is executed periodically by a script which continuously collects the sensors data, saves and transmits them to the server.  
&nbsp;


## Configuration

The parameters needed by the client program are specified directly at the command line.  

Syntax:  

    geomon_modbustcp_client <IP_ADDR> <TCP_PORT> <OP> <SLAVE> <ADDR> [<VALUE>]

Parameter |Description
----------|-----------
`<IP_ADDR>` | IP address of the device where the required module is running
`<TCP_PORT>`| TCP port on the device where the required module is listening
`<OP>`      | Operation (1-Read, 2-Write)
`<SLAVE>`   | Modbus slave address of the required module
`<ADDR>`    | Modbus register address provided by the required module
`<VALUE>`   | Value to write to Modbus register (only used for Write operation)
