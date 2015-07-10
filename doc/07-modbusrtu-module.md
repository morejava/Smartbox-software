# Modbus RTU module

## Description

The Modbus RTU module implements a Modbus RTU master which communicates with slave devices via a RS485 serial line.  

The request is received via the built in Modbus TCP slave using the IP protocol. Then this request is forwarded to the slave device and its response is handled by sending it back to the original requester. In other words, this module implements a ModbusTCP to ModbusRTU converter.  
&nbsp;


## Configuration

Configuration is done via command line parameters when the module executable is invoked.  

Syntax:  

    mbrtud <slave addr> <reg addr offset> <serial dev> [<baudrate>]

The `<slave addr>` parameter is the Modbus address of the remote slave device we want to communicate with.  

The value passed in the `<reg addr offset>` parameter is added to the requested register address before sending the request to the remote slave device. This is needed for devices with a register address range which does not start at 1 (e.g. NIBE Modbus40 device addresses start at 40000).  

The `<serial dev>` parameter is the Linux device name used for the serial communication. The string `/dev/tty` is automatically prepended to the value given, therefore it is sufficient to specify the short name, e.g `USB0` or `S0`.  

The `<baudrate>` parameter is optional and specifies the baudrate used for the serial communication. If omitted, the default value is 9600.  
&nbsp;


## Modbus register map

#### Slave Address: 4

Register Address | Description | Unit | Type           | Divisor | Connection
-----------------|-------------|------|----------------|---------|-----------
1                |Register 1     |NA|Unsigned int 16bit|NA|NA
...              |               |  |                  |  |
8200             |Register 8200|NA|Unsigned int 16bit|NA|NA

The actual register map is the one defined on the slave device which is being queried. An address range from 1 to 8200 is supported. To move the address range to higher values, the register offset value can be provided (see configuration).  
