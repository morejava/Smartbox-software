/******************************************************************
  Control daemon for ACME FoxG20 SBC
  
  Author: Ondrej Wisniewski
  
  Features:
  - Controls digital outputs (relays) on external request

  Build command:
  gcc controld.c -o controld -lmbsrv `pkg-config --libs --cflags libmodbus`
  
  Changelog:
   15-11-2013: Initial version
   17-12-2013: Added Modbus server functionality
   15-05-2014: Added possibility to specify dummy control lines
   
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
   
******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <sys/inotify.h>

#include "modbustcp_server_lib.h"

#define VERSION "0.3"

#define DEBUG 0

/* GPIO sysfs interface */
#define EXPORT_FILE    "/sys/class/gpio/export"
#define UNEXPORT_FILE  "/sys/class/gpio/unexport"
#define GPIO_BASE_FILE "/sys/class/gpio/gpio"

#define MAX_CONTROL_LINES 8

#define MODBUS_SLAVE_ADDRESS MODBUS_SLAVE_CONTROL_MODULE

#define FIRST_REG 1
#define LAST_REG 16

/*
 * Modbus register map of the CONTROL slave module:
 * 
 * ADDR  SOURCE               TYPE  DESCRIPTION
 * ---------------------------------------------
 *  1    ctrl_file 1          RW    Relay 1
 *  2    ctrl_file 2          RW    Relay 2
 *  3    ctrl_file 3          RW    Relay 3
 *  4    ctrl_file 4          RW    Relay 4
 *  5    ctrl_file 5          RW    Relay 5
 *  6    ctrl_file 6          RW    Relay 6
 *  7    ctrl_file 7          RW    Relay 7
 *  8    ctrl_file 8          RW    Relay 8
 *  9    TBD                    R   TBD
 * 10    TBD                    R   TBD
 * 11    TBD                    R   TBD
 * 12    TBD                    R   TBD
 * 13    TBD                    R   TBD
 * 14    TBD                    R   TBD
 * 15    TBD                    R   TBD
 * 16    TBD                    R   TBD
 */

typedef enum {
   LOW,
   HIGH
}
PIN_STATE_t;


/* output lines */
static int output_pin[MAX_CONTROL_LINES];
static int value_fd[MAX_CONTROL_LINES];

/* control files */
static int control_fd[MAX_CONTROL_LINES];

static int fdnotify = -1;
static int num_ctrl_lines;
static pid_t modbus_server_pid;



/*********************************************************************
 * Function: setup()
 * 
 * Description: Setup of globally used resources
 * 
 * Parameters: IN  pin - GPIO Kernel Id of used IO pin
 *             OUT value_fd - file descriptor for digital value file
 * 
 ********************************************************************/
static int setup(int pin, int* value_fd)
{
  int fd;
  char b[64];

  // Prepare GPIO pin connected to data pin to be used with GPIO sysfs
  // (export to user space)
  fd = open(EXPORT_FILE, O_WRONLY);
  if (fd < 0) {
    perror(EXPORT_FILE);
    return 1;
  }  
  snprintf(b, sizeof(b), "%d", pin);
  if (pwrite(fd, b, strlen(b), 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to export pin=%d (already in use?): %s\n",
            pin, strerror(errno));
    return 2;
  }  
  close(fd);

  // Define gpio direction as output
  snprintf(b, sizeof(b), "%s%d/direction", GPIO_BASE_FILE, pin);
  fd = open(b, O_RDWR);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", b, strerror(errno));
    return 5;
  }
  if (pwrite(fd, "out", 3, 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to write 'out' to %s: %s\n",
            b, strerror(errno));
    return 6;
  }
  close(fd);
  
  // Open gpio value file for fast reading/writing when requested
  snprintf(b, sizeof(b), "%s%d/value", GPIO_BASE_FILE, pin);
  fd = open(b, O_RDWR);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", b, strerror(errno));
    return 7;
  }
  *value_fd=fd;

  return 0;
}

/*********************************************************************
 * Function:    cleanup()
 * 
 * Description: Cleanup of globally used resources
 * 
 * Parameters:  none
 * 
 ********************************************************************/
static void cleanup(int pin, int value_fd)
{
  int fd;
  char b[8];

  // close gpio value file
  close(value_fd);

  // free GPIO pin connected to sensors data pin to be used with GPIO sysfs  
  fd = open(UNEXPORT_FILE, O_WRONLY);
  if (fd < 0) {
    perror(UNEXPORT_FILE);
    return;
  } 
  snprintf(b, sizeof(b), "%d", pin);
  if (pwrite(fd, b, strlen(b), 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to unexport pin=%d: %s\n",
            pin, strerror(errno));
    return;
  }  
  close(fd);
}

/*********************************************************************
 * Function:    ctrlfileRead()
 * 
 * Description: Read from the control file
 * 
 * Parameters:  none
 * 
 ********************************************************************/
static char ctrlfileRead(int control_fd)
{
  int fd=control_fd;
  char d[1];

  if (pread(fd, d, 1, 0) != 1) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to pread command value: %s\n",
                     strerror(errno));
  }
  
  return d[0];
}

/*********************************************************************
 * Function:    ctrlfileWrite()
 * 
 * Description: Write to the control file
 * 
 * Parameters:  none
 * 
 ********************************************************************/
static void ctrlfileWrite(int control_fd, char value)
{
  int fd=control_fd;
  char d[2];
  
  sprintf(d, "%d", value);
#if DEBUG  
  printf("DEBUG: writing value %d=%s\n", value, d);
#endif
  if (pwrite(fd, d, 1, 0) != 1) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to pwrite command value: %s\n",
                     strerror(errno));
  }
}

/*********************************************************************
 * Function:    digitalWrite()
 * 
 * Description: Write to the data pin
 * 
 * Parameters:  value - output value (HIGH|LOW)
 * 
 ********************************************************************/
static void digitalWrite(int value_fd, PIN_STATE_t value)
{
  int fd=value_fd;
  char d[1];
  
  d[0] = (value == LOW ? '0' : '1');
  if (pwrite(fd, d, 1, 0) != 1) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to pwrite %c to gpio value: %s\n",
                     d[0], strerror(errno));
  }
}

/*********************************************************************
 * Function:    waitForCommand()
 * 
 * Description: Waits for an external command via watched files
 * 
 * Parameters:  ...
 * 
 * Returns:  0 on success, >0 otherwise
 ********************************************************************/
static int waitForCommand(int* index, int fdnotify, int* wd_p, unsigned int num_lines)
{
  int i;
  int length;
  char buffer[MAX_CONTROL_LINES*32];
  struct inotify_event *event = NULL;
    
  /* Wait until one of the watched files is modified.
   * read() will block here if none of the registered events 
   * has occured on any file
   */
  length = read(fdnotify, buffer, sizeof(buffer));
  if (length < 0) 
  {
    syslog(LOG_DAEMON | LOG_ERR, "read() failed: %s\n", strerror(errno));
    return 1;
  }
  
#if DEBUG
  printf("DEBUG: read() returned %d bytes\n", length);
#endif
  
  event = (struct inotify_event *)buffer;
  if (!(event->mask & IN_MODIFY))
  {
    syslog(LOG_DAEMON | LOG_ERR, "Unknown event occured for file wd=%d (mask 0x%04X\n", 
                     event->wd, event->mask);
    return 2;
  }
  
  /* Search index of active line */
  for(i=0; i<num_lines; i++)
  {
     if (wd_p[i]==event->wd) break;
  }
  
  *index = i;

  return 0;  
}
 

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
  int i;
  pid_t pid=getpid();
  
  syslog(LOG_DAEMON | LOG_NOTICE, "caught signal %d\n", signum);
  
  switch (signum) {
    case SIGTERM:
    case SIGINT:
        /* Clean up and terminate */
        if (pid != modbus_server_pid)
        {
          for (i=0; i<num_ctrl_lines; i++)
          {
            if (output_pin[i])
            {
              cleanup(output_pin[i], value_fd[i]);
              close(control_fd[i]);
            }
          }
          close(fdnotify);
          kill(modbus_server_pid, SIGTERM);
          syslog(LOG_DAEMON | LOG_NOTICE, "Exiting Control daemon\n");
          closelog();
        }
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
  char d[2];
  
  /* Check addr range */
  if((addr < FIRST_REG) || (addr > LAST_REG)) {
    syslog(LOG_DAEMON | LOG_ERR, "Address %d out of range\n", addr);
    return -1;
  }
  
  if(addr > num_ctrl_lines) {
    syslog(LOG_DAEMON | LOG_ERR, "Control line not defined\n");
    return -2;
  }
  
  /* Read current value from control file */
  d[0]=ctrlfileRead(control_fd[addr-1]);
  d[1]=0;
  *reg_val_p = atoi(d);
  
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
  /* Check addr range */
  if((addr < FIRST_REG) || (addr > LAST_REG)) {
    syslog(LOG_DAEMON | LOG_ERR, "Address %d out of range\n", addr);
    return -1;
  }
  
  if(addr > num_ctrl_lines) {
    syslog(LOG_DAEMON | LOG_ERR, "Control line not defined\n");
    return -2;
  }
  
  /* Write value to control file */
  ctrlfileWrite(control_fd[addr-1], reg_val);
  
  return 0;
}


/*
 * 
 * MAIN process
 * 
 */ 
int main(int argc, char* argv[])
{
  int i;
  char control_fn[MAX_CONTROL_LINES][64];
  int wd[MAX_CONTROL_LINES];
  int line_index;
  char command;
   
   
  if (argc==1)
  {
    printf("Usage:\n");
    printf("  controld  <pin1> <ctrl_file1> [<pin2> <ctrl_file2>] ...\n");
    printf("      pinX: kernel Id of GPIO pin to control (use 0 for dummy pin)\n");
    printf("      ctrl_fileX: control file which is used to send commands to the daemon\n");
    return 0;
  }

  openlog("controld", LOG_PID|LOG_CONS, LOG_USER);
  syslog(LOG_DAEMON | LOG_NOTICE, "Starting Control daemon (version %s)\n", VERSION);

  memset((void*)control_fd, 0, sizeof(control_fd));
  memset((void*)control_fn, 0, sizeof(control_fn));
  memset((void*)wd, 0, sizeof(wd));

  /* Install signal handler for SIGTERM and SIGINT ("CTRL C") to be used to terminate */
  signal(SIGTERM, doExit);
  signal(SIGINT, doExit);

  /* Parse input parameters */
  num_ctrl_lines = (argc-1)/2;
  if (num_ctrl_lines > MAX_CONTROL_LINES)
  {
    syslog(LOG_DAEMON | LOG_ERR, "Too many output lines specified: %d (max. %d)\n", num_ctrl_lines, MAX_CONTROL_LINES);
    return 1;
  }
  
  syslog(LOG_DAEMON | LOG_NOTICE, "Controlling output lines:\n");
  for (i=0; i<num_ctrl_lines; i++)
  {
    output_pin[i] = atoi(argv[2*i+1]);
    if (output_pin[i] != 0)
    {
      strcpy(control_fn[i], argv[2*i+2]);
      syslog(LOG_DAEMON | LOG_NOTICE, " file %s --> pin %d\n", control_fn[i], output_pin[i]);
    }
  }
  
  /* Init */
  for (i=0; i<num_ctrl_lines; i++)
  {
    if (output_pin[i] != 0)
    {
      if (setup(output_pin[i], &value_fd[i]) != 0)
      { 
        /* GPIO setup failed */
        return 2;
      }
      control_fd[i] = open(control_fn[i], O_RDWR);
      if (control_fd[i] < 0) 
      {
        syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", control_fn[i], strerror(errno));
        return 3;
      }
    }
  }

  /* Create child process for Modbus TCP server */
  modbus_server_pid=fork();
  if (modbus_server_pid == -1)
  {
    syslog(LOG_DAEMON | LOG_ERR, "Error creating child process for Modbus server\n");
    return 4;
  }
    
  if (modbus_server_pid == 0)
  {
    /***** We are in the child process *****/
    
    modbus_server_pid = getpid();
   
    /* Start Modbus TCP server loop */
    modbustcp_server(MODBUS_SLAVE_ADDRESS,  // Modbus slave address
                     read_register_handler, // Read register handler
                     write_register_handler // Write register handler
                    );
  }
    
  /* Use inotify API to watch control files */
  fdnotify = inotify_init();
  if (fdnotify < 0) 
  {
    syslog(LOG_DAEMON | LOG_ERR, "inotify_init failed: %s\n", strerror(errno));
    return 5;
  }

  /* Add control files to the watch list, watch file modification */
  for (i=0; i<num_ctrl_lines; i++)
  {
    if (control_fn[i][0])
    if ((wd[i] = inotify_add_watch(fdnotify, control_fn[i], IN_MODIFY)) < 0)
    {
      syslog(LOG_DAEMON | LOG_ERR, "inotify_add_watch failed: %s\n", strerror(errno));
    }
  }
  
  /* Read control files and restore the permanent output states */
  for (i=0; i<num_ctrl_lines; i++)
  {
    if (control_fd[i] == 0) continue;
    
    command = ctrlfileRead(control_fd[i]);
    
    switch(command)
    {
      case '1': // switch to HIGH
      case '3': // LOW pulse (reset to HIGH)
        syslog(LOG_DAEMON | LOG_NOTICE, "Restoring command \"%c\" from control file %s\n",
               command, control_fn[i]);
        digitalWrite(value_fd[i], HIGH);
        break;
          
      case '2': // switch to LOW
      case '4': // HIGH pulse (reset to LOW)
        syslog(LOG_DAEMON | LOG_NOTICE, "Restoring command \"%c\" from control file %s\n",
               command, control_fn[i]);
        digitalWrite(value_fd[i], LOW);
        break;
        
      default: // nothing to restore
         ;
    }
  }
  
  /***** Main loop *****/
  while (1) 
  {
    /* Wait for a command to arrive via one of the command files */
    if (waitForCommand(&line_index, fdnotify, wd, num_ctrl_lines) != 0)
    {
      /* waitForCommand failed */
      syslog(LOG_DAEMON | LOG_ERR, "Error while waiting for changes of control files\n");
    }
    else
    {
      /* Read new command from control file */
      command = ctrlfileRead(control_fd[line_index]);
      
      syslog(LOG_DAEMON | LOG_NOTICE, "Received command \"%c\" from control file %s\n", 
             command, control_fn[line_index]);
     
      /* Change output according to command */
      switch(command)
      {
        case '0': // do nothing
          break;
          
        case '1': // switch to HIGH
          digitalWrite(value_fd[line_index], HIGH);
          break;
          
        case '2': // switch to LOW
          digitalWrite(value_fd[line_index], LOW);
          break;
          
        case '3': // generate LOW pulse
          digitalWrite(value_fd[line_index], LOW);
          usleep(500000);
          digitalWrite(value_fd[line_index], HIGH);
          break;
          
        case '4': // generate HIGH pulse
          digitalWrite(value_fd[line_index], HIGH);
          usleep(500000);
          digitalWrite(value_fd[line_index], LOW);
          break;
          
        default:
          syslog(LOG_DAEMON | LOG_ERR, "Unknown command in control file: %c\n", command);
      }
    }
    
    /* Wait a little before processing the next event */
    usleep(500000);
  }
  
  return 0;
}
