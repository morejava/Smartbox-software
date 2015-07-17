/*
 * Modbus TCP server library
 * - listens to Modbus TCP requests from local or remote clients
 * - performs requested read or write operations via specific callback functions
 * 
 * Build instructions, using provided Makefile:
 *   make
 *   make install
 * 
 * Author: O. Wisniewski
 * Version: 0.3
 * Date: 2014/07/23
 * 
 * TODO: handle termination signal and cleanup before existing
 *
 * Copyright 2013-2015, DEK Italia
 * 
 * This file is part of the Telegea platform.
 * 
 * Telegea is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Telegea.  If not, see <http://www.gnu.org/licenses/>.
 */

#if 0
/**********************************************************
 * 
 * The following is a sample code showing how to use the 
 * Modbus TCP server library in a Modbus server application.
 * 
 **********************************************************/

/**********************************************************
 * FUNCTION: handle_reg_read_cb
 * 
 * DESCRIPTION: 
 *           Handles the read single register request
 * 
 * PARAMETERS: 
 *           int reg_addr - register address to read from
 *           int* reg_val - pointer to register value
 * 
 * RETURN:   0 on success
 *          -1 otherwise
 *********************************************************/
int handle_reg_read_cb(int reg_addr, uint16_t* reg_val)
{
  /* TODO: add module specific code here 
   * to read requested register value 
   */
  *reg_val = reg_addr+100;
  printf("DBG: Read val %d from addr %d\n", *reg_val, reg_addr);
  
  return 0;
}

/**********************************************************
 * FUNCTION: handle_reg_write_cb
 * 
 * DESCRIPTION: 
 *           Handles the write single register request
 * 
 * PARAMETERS: 
 *           int reg_addr - register address to write to
 *           int reg_val  - register value to write
 * 
 * RETURN:   0 on success
 *          -1 otherwise
 *********************************************************/
int handle_reg_write_cb(int reg_addr, uint16_t reg_val)
{
  /* TODO: add module specific code here 
   * to write requested register value 
   */
  printf("DBG: Wrote val %d to addr %d\n", reg_val, reg_addr);
  
  return 0;
}

/**********************************************************
 * MAIN function
 *********************************************************/
int main(void)
{ 
   modbustcp_server(own_slave_addr, handle_reg_read_cb, handle_reg_write_cb);
   
   return 0;
} 
#endif


#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <modbus.h>

#include "modbustcp_server_lib.h"

#define VERSION "0.3"

/* For the NIBE Modbus40 module we need to handle register
 * addresses in the range [40001 - 48198]. To save memory
 * we map these to [1 - 8198].
 * For all the other modules, an address range [1 - 16]
 * is sufficient.
 */
#define MAX_REG 8200

#define DEBUG 0


/* Modbus request structure */
typedef struct {
   uint8_t slave_addr;
   uint8_t fc;
   uint8_t reg_addr_hi;
   uint8_t reg_addr_lo;
   uint8_t reg_val_hi;
   uint8_t reg_val_lo;
} modbus_request_t;

/* Flag to indicate exit from main loop */
static int cont=1;



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
 *           own_slave_addr - Own Modbus slave address
 *           read_cb_fun    - function pointer to Read register handler
 *           write_cb_fun   - function pointer to Write register handler
 * 
 * RETURN:   0 on success
 *          -1 otherwise
 * 
 *******************************************************************/
int modbustcp_server(int own_slave_addr, int (*read_cb_fun)(int, int*), int (*write_cb_fun)(int, int))
{
   int socket, accept_socket;
   int header_length;
   modbus_t *ctx; 
   int tcp_port = MODBUSTCP_SERVER_PORT_BASE + own_slave_addr;
   int num_connections=1;
   
   
   openlog("modbus server", LOG_PID|LOG_CONS, LOG_USER);
   syslog(LOG_DAEMON | LOG_NOTICE, "Starting Modbus server for slave #%d (version %s using libmodbus %s)\n", 
                                                          own_slave_addr, VERSION, LIBMODBUS_VERSION_STRING);
   
   syslog(LOG_DAEMON | LOG_NOTICE, "Listening on port %d\n", tcp_port);

   /* Create new connection context */
   ctx = modbus_new_tcp("127.0.0.1", tcp_port);
   
#if DEBUG
   modbus_set_debug(ctx, TRUE);  
#endif
   
   /* Get header length */
   header_length = modbus_get_header_length(ctx);
   
   /* Create the listen socket */
   socket = modbus_tcp_listen(ctx, num_connections);
   
   
   /***** Main server loop *****/
   while (cont)
   {
      uint8_t query[MODBUS_TCP_MAX_ADU_LENGTH];
      int rc;
      
      /* Wait for incoming connections */
      accept_socket = modbus_tcp_accept(ctx, &socket);
      if (accept_socket == -1) 
      { 
         cont = 0;
         syslog(LOG_DAEMON | LOG_ERR, "Slave #%d: modbus_tcp_accept() failed; %s", 
                                          own_slave_addr, modbus_strerror(errno));
      } 
            
      /* Receive data from client */    
      rc = modbus_receive(ctx, query);      /* rc is the query size */
      if (rc == -1) 
      { 
         syslog(LOG_DAEMON | LOG_ERR, "Slave #%d: modbus_receive() failed: %s", 
                                       own_slave_addr, modbus_strerror(errno));
      }
      else 
      { 
         /* Get information from request buffer */
         modbus_request_t *modbus_request;
         int slave_addr;
         int operation;
         int reg_addr;
         int reg_val;
         unsigned int exception_code;
         modbus_mapping_t *mb_mapping;
         
         modbus_request = (modbus_request_t *)&query[header_length-1];
         
         slave_addr = modbus_request->slave_addr;
         operation  = modbus_request->fc;
         reg_addr   = (int)modbus_request->reg_addr_hi<<8 | (int)modbus_request->reg_addr_lo;
         reg_val    = (int)modbus_request->reg_val_hi<<8 | (int)modbus_request->reg_val_lo;
         
         exception_code = 0;
         
#if DEBUG
         printf("DBG: received request for slave %d, op %d, addr %d, reg_val %d\n", 
                                         slave_addr, operation, reg_addr, reg_val); 
#endif
         
         /* Initialise new response data structure */
         mb_mapping = modbus_mapping_new(0,0,MAX_REG,0);
         if (mb_mapping == NULL) 
         {
            syslog(LOG_DAEMON | LOG_ERR, "Slave #%d: Failed to allocate the mapping: %s", 
                                                 own_slave_addr, modbus_strerror(errno));
            modbus_free(ctx);
            return -1;
         }
         
         /* Check if slave address matches with our own address 
          * TODO: should we respond with an exception here ?
          */
         if (slave_addr != own_slave_addr)
         {
            syslog(LOG_DAEMON | LOG_ERR, "Slave #%d: slave address %d doesn't match our own address", 
                                                                         own_slave_addr, slave_addr);
         }
         
         /* Perform requested operation using provided callback functions */
         switch (operation)
         {
            case 0x03:  /* FC Read Holding Registers */
            case 0x04:  /* FC Read Input Registers */
               if (reg_addr < MAX_REG) 
               {
                  if (read_cb_fun) 
                  {
                     /* Call the "Read register" handler function */ 
                     if ((*read_cb_fun)(reg_addr, &reg_val) != 0)
                     {  /* Error during register read occured */
                        exception_code = MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE;
                     }
                     else
                     {  /* Copy read value into response buffer */
                        mb_mapping->tab_registers[reg_addr] = reg_val;
                     }
                  }
                  else
                  { /* Function for this operation is not defined */
                     exception_code = MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
                  }         
               }
               else
               {  /* Register address out of range */
                  exception_code = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
               }
               break;	
         
            case 0x06:  /* FC Write single register */
               if (reg_addr < MAX_REG) 
               {
                  if (write_cb_fun) 
                  {
                     /* Call the "Write register" handler function */
                     if ((*write_cb_fun)(reg_addr, reg_val) != 0)
                     {  
                        /* Error during register write occured */
                        exception_code = MODBUS_EXCEPTION_SLAVE_OR_SERVER_FAILURE;
                     }
                  }
                  else
                  {  /* Function for this operation is not defined */
                     exception_code = MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
                  }         
               }
               else
               {  /* Register address out of range */
                  exception_code = MODBUS_EXCEPTION_ILLEGAL_DATA_ADDRESS;
               }
               break;
      
            default:
               printf("MODBUS_TCP_SERVER: Invalid operation %d\n", operation);
               exception_code = MODBUS_EXCEPTION_ILLEGAL_FUNCTION;
               
         } //end switch statement
   
         /* Send reply to client */
         if (exception_code == 0)
         {
            rc = modbus_reply(ctx, query, rc, mb_mapping);
            if (rc == -1) 
            {
               syslog(LOG_DAEMON | LOG_ERR, "Slave #%d: Failed to send reply to the client: %s", 
                                                        own_slave_addr, modbus_strerror(errno));
            }
         }
         else
         {
            rc = modbus_reply_exception(ctx, query, exception_code);
            if (rc == -1) 
            {
               syslog(LOG_DAEMON | LOG_ERR, "Slave #%d: Failed to send exception reply to the client: %s", 
                                                        own_slave_addr, modbus_strerror(errno));
            }
         }
         modbus_mapping_free(mb_mapping); 
      }
      
      close(accept_socket);
      
   } // end of main server loop

   syslog(LOG_DAEMON | LOG_NOTICE, "Exiting Modbus server for slave #%d", own_slave_addr);

   close(socket);
   modbus_close(ctx);
   modbus_free(ctx);
   return 0;
}
