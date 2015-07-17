/*
 * Inspired by the random_test_client example in the modbus library
 *
 * PURPOSE: This program is launched from the command line, to read or write
 * one or multiple (in future versions!) registers of a Modbus device
 * through a TCP connection.
 *
 * USAGE = geomon_modbustcp_client <IP_ADDR> <TCP_PORT> <OP> <SLAVE> <ADDR> [<VALUE>]
 *
 * Build instructions:
 * gcc geomon_modbustcp_client.c -o geomon_modbustcp_client `pkg-config --libs --cflags libmodbus`
 *
 * Author: O. Wisniewski
 * Version: 0.3
 * Date: 2015/02/17
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
 * 
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <modbus/modbus.h>

#define DEBUG 0

#define VERSION "0.3"

/* Server response timeout (in sec) */
#define RESPONSE_TMO 15


int main(int argc, char* argv[])
{
  modbus_t *ctx;
  char *ip_addr;
  int   tcp_port;
  int   operation;
  int   slave_addr;
  int   reg_addr;
  int   rc;
  struct timeval timeout;
  uint16_t modbus_reg=0;

  
  /* Parse command line */
  if (argc >= 6)
  {
    ip_addr    =      argv[1];
    tcp_port   = atoi(argv[2]);
    operation  = atoi(argv[3]);
    slave_addr = atoi(argv[4]);
    reg_addr   = atoi(argv[5]);
    
    if ((argc == 7) && (operation == 2)) 
    {
      modbus_reg = atoi(argv[6]);
    }
  }
  else
  {
    printf("Modbus TCP client, version %s\n", VERSION);
    printf("usage: %s <IP_ADDR> <TCP_PORT> <OP> <SLAVE> <ADDR> [<VALUE>] \n", argv[0]);
    return 0;
  }
 
  /* Create TCP connection context */
  ctx = modbus_new_tcp(ip_addr, tcp_port);
  
#if DEBUG
  modbus_set_debug(ctx, TRUE);
#endif

  /* Set server response timeout */  
  timeout.tv_sec = RESPONSE_TMO;
  timeout.tv_usec = 0;
  modbus_set_response_timeout(ctx, &timeout);
 
  /* Set remote slave address */  
  modbus_set_slave(ctx, slave_addr);

  /* Start TCP connection */
  if (modbus_connect(ctx) == -1) {
    fprintf(stderr, "TCP Connection to %s : %d failed: %s\n",
	    ip_addr, tcp_port, modbus_strerror(errno));
    modbus_free(ctx);
    return -1;
  }

  /* Handle requested operation */
  switch(operation)
  {  
    case 1:        /* Read single register */
      rc = modbus_read_registers(ctx, reg_addr, 1, &modbus_reg);
      if (rc != 1) {
        printf("PLANT: %s %d %d %d  ERR\n", ip_addr, tcp_port, operation, rc); 
        printf("%d: ERR\n", reg_addr); 
      } 
      else {
        printf("PLANT: %s %d %d\n", ip_addr, tcp_port, operation);
        printf("%d: %X: %d\n", reg_addr, modbus_reg, modbus_reg);
      }
      break;
 
    case 2:        /* Write single register */
      rc = modbus_write_register(ctx, reg_addr, (int)modbus_reg);
      if (rc != 1) {
        printf("PLANT: %s %d %d %d  ERR\n", ip_addr, tcp_port, operation, rc); 
        printf("%d: ERR %d\n", reg_addr, modbus_reg);
      } 
      else {
        printf("PLANT: %s %d %d\n", ip_addr, tcp_port, operation);
        printf("%d: %X: %d\n", reg_addr, modbus_reg, modbus_reg);
      }    
      break;
      
    default:
      printf("PLANT: %s %d %d ERROR: Operation not supported\n", ip_addr, tcp_port, operation);
  }
  
  /* Close the connection */
  modbus_close(ctx);
  modbus_free(ctx);
  return 0;
}
