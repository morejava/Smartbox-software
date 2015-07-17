/*
 * Modbus TCP server library
 * - listens to Modbus TCP requests from local or remote clients
 * - performs requested read or write operations via specific callback functions
 * 
 * Author: O. Wisniewski
 * Version: 0.1
 * Date: 2013/11/29
 * 
 */
#ifndef _MODBUSTCP_SERVER_LIB_H_
#define _MODBUSTCP_SERVER_LIB_H_

#define MODBUSTCP_SERVER_PORT_BASE 5000

/* Slave address list for known modules */
#define MODBUS_SLAVE_SENSOR_MODULE  1
#define MODBUS_SLAVE_COUNTER_MODULE 2
#define MODBUS_SLAVE_STATUS_MODULE  3
#define MODBUS_SLAVE_CONTROL_MODULE 4
#define MODBUS_SLAVE_MBRTU_MODULE   5
#define MODBUS_SLAVE_SNMP_MODULE    6
#define MODBUS_SLAVE_CUSTOM_MODULE  7

/********************************************************************
 * 
 * PUBLIC FUNCTION: 
 *           modbustcp_server()
 * 
 * DESCRIPTION: 
 *           Handles the communication with Modbus TCP clients and
 *           performs requested Read or Write operations via callback
 *           functions (provided as input paramters)
 * 
 *           Supported Modbus operations:
 *           - Read Holding Registers (FC 0x03)
 *           - Read Input Registers   (FC 0x04)
 *           - Write Single Register  (FC 0x06)
 * 
 * PARAMETERS: 
 *           tcp_port     - TCP port to listen for incoming requests
 *           read_cb_fun  - function pointer to Read register handler
 *           write_cb_fun - function pointer to Write register handler
 * 
 * RETURN:   0 on success
 *          -1 otherwise
 * 
 *******************************************************************/
int modbustcp_server(int tcp_port, int (*read_cb_fun)(int, int*), int (*write_cb_fun)(int, int));


#endif