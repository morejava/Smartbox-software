/******************************************************************
 *  Modbus RTU daemon for TeleGea device
 *  
 *  Author: Ondrej Wisniewski
 *  
 *  Features:
 *  - Queries Modbus RTU devices (e.g. NIBE Modbus40) on external request
 * 
 *  Build command:
 *  gcc mbrtud.c -o mbrtud -lmbsrv `pkg-config --libs --cflags libmodbus`
 *  
 *  Changelog:
 *   22-07-2014: Initial version
 * 
 *  Copyright 2013-2015, DEK Italia
 * 
 *  This file is part of the Telegea platform.
 * 
 *  Telegea is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Telegea.  If not, see <http://www.gnu.org/licenses/>.
 * 
 ******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <modbus.h>

#include "modbustcp_server_lib.h"


#define VERSION "0.1"

#define MODBUS_SLAVE_ADDRESS MODBUS_SLAVE_MBRTU_MODULE

#define SERIAL_PORT_PREFIX   "/dev/tty"
#define DEFAULT_BAUDRATE      9600
#define DEFAULT_SLAVE_ADDR    1


static modbus_t *mb;
static int reg_addr_offset=0;

/*
 * Modbus register map of the Modbus RTU slave module:
 * 
 * The register map of this module is identical to one 
 * of the Modbus RTU device we are quering.
 */


/*********************************************************************
 * Function:    doExit()
 * 
 * Description: Signal handler function to do a clean shutdown
 * 
 * Parameters:  the received signal
 * 
 ********************************************************************/
static void doExit(int signum)
{
   pid_t pid=getpid();  
   syslog(LOG_DAEMON | LOG_NOTICE, "caught signal %d in process %d\n", signum, pid);
   
   switch (signum) {
      case SIGTERM:
      case SIGINT:
         /* Clean up and terminate */
         modbus_close(mb);
         modbus_free(mb);
         syslog(LOG_DAEMON | LOG_NOTICE, "Exiting Modbus RTU daemon\n");
         closelog();
         kill(pid, SIGKILL);
         break;
         
      default:
         syslog(LOG_DAEMON | LOG_ERR, "unexpected signal received\n");
   }
}


/**********************************************************
 * FUNCTION: read_register_handler
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
int read_register_handler(int addr, int *reg_val_p)
{
   int rc=0;
   uint16_t modbus_regs[2];

   /* Check addr range */
   /* TODO */
   
   /* Read specified Modbus register value from RTU device. 
    * We add an address offset (as specified by input parameter)
    * to the original address from the request.
    * (needed e.g. for Modbus40 module
    */
   addr += reg_addr_offset;
   modbus_regs[0] = 0;
   rc = modbus_read_registers(mb, addr, 1, modbus_regs);
   //printf("read reg %d\n", addr);
   if (rc != 1) 
   {
      return -1;
   } 
   else 
   {
      *reg_val_p = modbus_regs[0];
   }
   
   return 0;
}


/**********************************************************
 * FUNCTION: write_register_handler
 * 
 * DESCRIPTION: 
 *           Handles the write single register request
 * 
 * PARAMETERS: 
 *           int reg_addr - register address to read from
 *           int* reg_val - register value
 * 
 * RETURN:   0 on success
 *          -1 otherwise
 *********************************************************/
int write_register_handler(int addr, int reg_val)
{
   int rc=0;
   uint16_t modbus_regs[2];
   
   /* Check addr range */
   /* TODO */
   
   /* Write specified Modbus register value to RTU device.
    * We add an address offset (as specified by input parameter)
    * to the original address from the request.
    * (needed e.g. for Modbus40 module
    */
   addr += reg_addr_offset;
   modbus_regs[0] = reg_val;
   rc = modbus_write_registers(mb, addr, 1, modbus_regs);
   //printf("write reg %d\n", addr);
   if (rc != 1) 
   {
      return -1;
   } 
   
   return 0;
}


/*
 * 
 * MAIN process
 * 
 */ 
int main(int argc, char* argv[])
{   
   int  slave_addr      = DEFAULT_SLAVE_ADDR;
   char serial_port[32] = SERIAL_PORT_PREFIX;
   int  baud_rate       = DEFAULT_BAUDRATE;
   struct timeval timeout;
   
   
   if (argc<4)
   {
      printf("Usage:\n");
      printf("  mbrtud  <slave addr> <reg addr offset> <serial dev> [<baudrate>]\n");
      printf("      slave addr: slave address of the Modbus RTU\n");
      printf("      reg addr offset: offset to be added to register addresses in requests\n");
      printf("      serial dev: name of the serial device used for connection to the Modbus RTU (e.g. USB0 or S0)\n");
      printf("      baudrate:   baudrate used for serial connection (optional, default: 9600)\n");
      return 0;
   }

   openlog("mbrtud", LOG_PID|LOG_CONS, LOG_USER);
   syslog(LOG_DAEMON | LOG_NOTICE, "Starting Modbus RTU daemon (version %s)\n", VERSION);
   
   /* Install signal handler for SIGTERM and SIGINT ("CTRL C") to be used to terminate */
   signal(SIGTERM, doExit);
   signal(SIGINT, doExit);
   
   /* Parse input parameters */
   slave_addr      = atoi(argv[1]);
   reg_addr_offset = atoi(argv[2]);
   strcat(serial_port,    argv[3]);
   if (argc>4) baud_rate = atoi(argv[4]);

# if 1   
   printf("slave_addr=%d\n", slave_addr);
   printf("reg_addr_offset=%d\n", reg_addr_offset);
   printf("serial_port=%s\n", serial_port);
   printf("baud_rate=%d\n", baud_rate);
#endif
   
   /* Init the Modbus RTU connection */
   mb = modbus_new_rtu(serial_port, baud_rate, 'N', 8, 1);
   if (mb == NULL)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to create RTU485 context\n");
      modbus_free(mb);
      return 1;
   }
  
   modbus_set_slave(mb, slave_addr);
   if (modbus_connect(mb) == -1) 
   {
      syslog(LOG_DAEMON | LOG_ERR, "Connection to RTU485 slave %d failed: %s\n", 
                                    slave_addr, modbus_strerror(errno));
      modbus_free(mb);
      return 2;
   }
  
   timeout.tv_sec = 20;
   timeout.tv_usec = 0;
   modbus_set_response_timeout(mb, &timeout); 
   timeout.tv_sec = 1;
   timeout.tv_usec = 0;
   modbus_set_byte_timeout(mb, &timeout);
  
   //modbus_set_debug(mb, TRUE);

   syslog(LOG_DAEMON | LOG_NOTICE, "Connection to RTU slave %d established on %s at %dbaud\n", 
                                    slave_addr, serial_port, baud_rate);
      
   /* Start Modbus TCP server loop */
   modbustcp_server(MODBUS_SLAVE_ADDRESS,  // Modbus slave address
                    read_register_handler, // Read register handler
                    write_register_handler // Write register handler
   );
   
   return 0;
}
