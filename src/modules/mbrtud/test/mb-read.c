/*
 * 
 * 
 * Build with this command:
 * gcc mb-read.c -o mb-read -lmodbus
 * 
 * 
 */


#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <modbus/modbus.h>

#define SERIAL_PORT   "/dev/ttyAMA0"
#define BAUDRATE      9600
#define RTS_DELAY     100

#define MAX_REG       32


int main(int argc, char* argv[])
{
  modbus_t *mb;
  uint16_t tab_reg[MAX_REG];
  int i, rc;
  int slave_addr;
  int start_addr;
  int num_reg;
  int poll_period=0;
  int count=0;
  
  
  if (argc < 4)
  {
    printf("usage: %s <slave_addr> <start_addr> <num_reg> [<poll_period>]\n", argv[0]);
    return 0;
  }

  if (argc > 3)
  {  
    slave_addr = atoi(argv[1]);
    start_addr = atoi(argv[2]);
    num_reg    = atoi(argv[3]);
  }
  
  if (argc == 5)
  {  
    poll_period = atoi(argv[4]);
  }
  
  memset(tab_reg, 0, MAX_REG*(sizeof(uint16_t)));
  
  
  /**************************************************************
   * Initialize communication port
   **************************************************************/
  
  /* Create Modbus context*/
  mb = modbus_new_rtu(SERIAL_PORT, BAUDRATE, 'N', 8, 1);
  if (mb == NULL) {
    printf("Unable to create the libmodbus context\n");
    return -1;
  }
  
  /* Set debug mode */
  modbus_set_debug(mb, TRUE);
  
  /* Set slave address */
  modbus_set_slave(mb, slave_addr);
  
  /* Connect to serial port */
  printf("Connecting to slave addr %d\n", slave_addr);
  if (modbus_connect(mb) == -1)
  {
    printf("Connection failed: %s\n", modbus_strerror(errno));
    modbus_free(mb);
    return -1;
  }

  /* Enable RS485 direction control via RTS line */
  if (modbus_rtu_set_rts(mb, MODBUS_RTU_RTS_DOWN) == -1)
  {
    printf("Setting RTS mode failed: %s\n", modbus_strerror(errno));
    modbus_free(mb);
    return -1;
  }

  /* Set RTS control delay (before and after transmission) */
  if (RTS_DELAY>0)
  {
    if (modbus_rtu_set_rts_delay(mb, RTS_DELAY) == -1)
    {
      printf("Setting RTS delay failed: %s\n", modbus_strerror(errno));
      modbus_free(mb);
      return -1;
    }
  }
  printf("RTS delay is %dus\n", modbus_rtu_get_rts_delay(mb));


  /**************************************************************
   * Main loop 
   **************************************************************/
  do
  {  
    /* Read registers starting from given address */
    rc = modbus_read_registers(mb, start_addr, num_reg, tab_reg);
    if (rc != num_reg) 
    {
      printf("Unable to read registers: %s\n", modbus_strerror(errno));
      modbus_close(mb);
      modbus_free(mb);
      return -1;
    }
  
    /* Print received register values */
    for (i=0; i<num_reg; i++)
      printf("%d: reg %d: 0x%04X\n", count++, start_addr+i, tab_reg[i]);
    
    usleep(poll_period*1000000);
  
  } while (poll_period);
  
  
  /**************************************************************
   * Clean up end exit
   **************************************************************/
  modbus_close(mb);
  modbus_free(mb);
  
  return 0;
}
