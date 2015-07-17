/******************************************************************
  Status monitor daemon
  
  Author: Ondrej Wisniewski
  
  Features:
  - Monitors one or two digital inputs and stores the latest value
  - support for multiple input pairs and multiple single inputs
  - TODO: Filtering of short glitches

  Build command:
  gcc statusd.c -o statusd -lmbsrv `pkg-config --libs --cflags libmodbus`
  
  Changelog:
   06-11-2013: Initial version
   16-12-2013: Added Modbus server functionality
   03-07-2015: Added support for single status line
   08-07-2015: Added support for multiple flip flops
   
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
#include <poll.h>
#include <signal.h>
#include <syslog.h>
#include <sys/wait.h>

#include "modbustcp_server_lib.h"

#define VERSION "0.4"

#define DEBUG 0

/* GPIO sysfs interface */
#define EXPORT_FILE    "/sys/class/gpio/export"
#define UNEXPORT_FILE  "/sys/class/gpio/unexport"
#define GPIO_BASE_FILE "/sys/class/gpio/gpio"

/* export file base name for flip flop states */
#define FLIPFLOPSTATE_FILE "/tmp/status"
#define MAX_FLIPFLOPS 16

#define MODBUS_SLAVE_ADDRESS MODBUS_SLAVE_STATUS_MODULE

#define FIRST_REG 1
#define LAST_REG MAX_FLIPFLOPS

/*
 * Modbus register map of the STATUS slave module:
 * 
 * ADDR  SOURCE                TYPE  DESCRIPTION
 * --------------------------------------------------------------
 *  1    /tmp/status1.<1>_<2>    R   flipflop / single line state
 *  2    /tmp/status2.<1>_<2>    R   flipflop / single line state
 *  3    /tmp/status3.<1>_<2>    R   flipflop / single line state
 *  4    /tmp/status4.<1>_<2>    R   flipflop / single line state
 *  5    /tmp/status5.<1>_<2>    R   flipflop / single line state
 *  6    /tmp/status6.<1>_<2>    R   flipflop / single line state
 *  7    /tmp/status7.<1>_<2>    R   flipflop / single line state
 *  8    /tmp/status8.<1>_<2>    R   flipflop / single line state
 *  9    /tmp/status9.<1>_<2>    R   flipflop / single line state
 * 10    /tmp/status10.<1>_<2>   R   flipflop / single line state
 * 11    /tmp/status11.<1>_<2>   R   flipflop / single line state
 * 12    /tmp/status12.<1>_<2>   R   flipflop / single line state
 * 13    /tmp/status13.<1>_<2>   R   flipflop / single line state
 * 14    /tmp/status14.<1>_<2>   R   flipflop / single line state
 * 15    /tmp/status15.<1>_<2>   R   flipflop / single line state
 * 16    /tmp/status16.<1>_<2>   R   flipflop / single line state
 */


typedef enum {
   LOW,
   HIGH
}
PIN_STATE_t;

typedef enum {
   ACTIVE_LOW,
   ACTIVE_HIGH
}
LOGIC_MODE_t;

typedef struct
{
   int   pin1;
   int   pin2;
   LOGIC_MODE_t lmode;
   pid_t child_pid;
}
STATUSPARAM_t;

typedef struct
{
   int pin1;                   /* GPIO pin Kernel Id */
   int pin2;                   /* GPIO pin Kernel Id */
   LOGIC_MODE_t lmode;         /* logic mode */
   int value_fd1;              /* GPIO sysfs value file descripter */
   int value_fd2;              /* GPIO sysfs value file descripter */
   int export_fd;              /* State export file descripter */
}
STATUS_t;

/* Global variables */
static STATUS_t status;
STATUSPARAM_t status_param[MAX_FLIPFLOPS];

static int cont = 1;
static pid_t parentpid;
static pid_t modbus_server_pid;


/*********************************************************************
 * Function: init_pin()
 * 
 * Description: Init GPIO pin for status monitoring
 * 
 * Parameters: pin - GPIO Kernel Id of used IO pin
 *             value_fd - file descriptor for gpio sysfs value file
 * 
 * Returns:  0 on success, >0 otherwise
 * 
 ********************************************************************/
static int init_pin(int pin, int* value_fd)
{
  int fd;
  char b[64];

  
  if (pin==0) {
     value_fd = 0;
     return 0;
  }
  
  // Prepare GPIO pin connected to sensors data pin to be used with GPIO sysfs
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

  // Define edge interrupt (only both edges are supported on FoxG20)  
  // to be used later with the poll() function for edge detection
  snprintf(b, sizeof(b), "%s%d/edge", GPIO_BASE_FILE, pin);
  fd = open(b, O_RDWR);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", b, strerror(errno));
    return 3;
  }
  if (pwrite(fd, "both", 4, 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to write 'both' to %s: %s\n",
            b, strerror(errno));
    return 4;
  }
  close(fd);
  
  // Define gpio direction as input
  snprintf(b, sizeof(b), "%s%d/direction", GPIO_BASE_FILE, pin);
  fd = open(b, O_RDWR);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", b, strerror(errno));
    return 5;
  }
  if (pwrite(fd, "in", 2, 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to write 'in' to %s: %s\n",
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
 * Function: setup()
 * 
 * Description: Setup of globally used resources
 * 
 * Parameters: param     - Parameter struct containing the
 *                         Pin numbers (in)
 *             status_p  - Pointer to status struct (out)
 * 
 * Return:     0 if successful, >0 in case of error
 * 
 ********************************************************************/
static int setup(STATUSPARAM_t param, STATUS_t* status_p)
{
  int fd;
  char b[64];

  if (init_pin(param.pin1, &(status_p->value_fd1)) || 
      init_pin(param.pin2, &(status_p->value_fd2))) {
    
    return 1;   
  }

  /* Open (or create) flip flop state export file */
  snprintf(b, sizeof(b), "%s.%d_%d", FLIPFLOPSTATE_FILE, param.pin1, param.pin2);
  fd = open(b, O_WRONLY|O_CREAT);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", b, strerror(errno));
    return 2;
  }
  
  /* Write initial state value to file */
  if (pwrite(fd, "0\n", 2, 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to write '0' to %s: %s\n",
           b, strerror(errno));
    return 3;
  }
  
  status_p->export_fd = fd;
  status_p->pin1 = param.pin1;
  status_p->pin2 = param.pin2;
  status_p->lmode = param.lmode; 
  
  return 0;
}

/*********************************************************************
 * Function:    cleanup()
 * 
 * Description: Cleanup of globally used resources
 * 
 * Parameters: status    - Status struct containing the
 *                         Pin numbers and file descriptors (in)
 * 
 ********************************************************************/
static void cleanup(STATUS_t status)
{
  int fd;
  char b[8];

  // close status export file
  close(status.export_fd);

  // close GPIO value files
  if (status.value_fd1)
    close(status.value_fd1);
  if (status.value_fd2)
    close(status.value_fd2);

  // free GPIO pins 
  if (status.pin1)
  {
     fd = open(UNEXPORT_FILE, O_WRONLY);
     if (fd < 0) {
        perror(UNEXPORT_FILE);
        return;
     }
     snprintf(b, sizeof(b), "%d", status.pin1);
     if (pwrite(fd, b, strlen(b), 0) < 0) {
        syslog(LOG_DAEMON | LOG_ERR, "Unable to unexport pin=%d: %s",
               status.pin1, strerror(errno));
        close(fd);
        return;
     }
     
     if (status.pin2)
     {
       snprintf(b, sizeof(b), "%d", status.pin2);
       if (pwrite(fd, b, strlen(b), 0) < 0) {
          syslog(LOG_DAEMON | LOG_ERR, "Unable to unexport pin=%d: %s",
                 status.pin2, strerror(errno));
          close(fd);
          return;
       }
     }
     close(fd);
  }
}

/*********************************************************************
 * Function:    digitalRead()
 * 
 * Description: Read from the data pin
 * 
 * Parameters: value_fd - file descriptor for gpio sysfs value file
 * 
 * Returns:  pin state (HIGH/LOW)
 * 
 ********************************************************************/
static PIN_STATE_t digitalRead(int value_fd)
{
  int fd=value_fd;
  char d[1];

  if (pread(fd, d, 1, 0) != 1) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to pread gpio value: %s\n",
                     strerror(errno));
  } 
  
  return (d[0] == '0' ? LOW : HIGH);
}

/*********************************************************************
 * Function:    waitForEdge()
 * 
 * Description: Waits for an edge change on one or two data pins
 * 
 * Parameters: flipflop_state (OUT) - number of last active input or
 *                                    state of single input
 *             value_fd1 (IN) - file descriptor for gpio sysfs value file
 *             value_fd2 (IN) - file descriptor for gpio sysfs value file
 *             lmode (IN)     - logig mode (ACTIVE_HIGH/ACTIVE_LOW)
 * 
 * Returns:  0 on success, >0 otherwise
 * 
 ********************************************************************/
static int waitForEdge(unsigned int* flipflop_state, int value_fd1, int value_fd2, LOGIC_MODE_t lmode)
{
  struct pollfd a_poll[2];
  nfds_t nfds=1;
  static int output=0;
  PIN_STATE_t active_level = (lmode==ACTIVE_HIGH?HIGH:LOW);
  
  
  a_poll[0].fd = value_fd1;
  a_poll[0].events = POLLPRI;
  a_poll[0].revents = 0;
  digitalRead(value_fd1);

  if (value_fd2) {
    a_poll[1].fd = value_fd2;
    a_poll[1].events = POLLPRI;
    a_poll[1].revents = 0;
    nfds = 2;
    digitalRead(value_fd2);
  }

  /* Wait for a change on the data pins */
  if (poll(a_poll, nfds, -1) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "poll() failed: %s\n", strerror(errno));
    return 1;
  }
  
  /* wait 50ms to let the input settle */
  usleep(50000);
  
  /* check input 1 */
  if (a_poll[0].revents & POLLPRI) {
#if DEBUG    
    printf("Edge on input 1\n");
#endif
    
    /* If 2 data pins are defined, return number of input, 
     * otherwise return logic state of first input
     */
    if (value_fd2) {
      if (digitalRead(value_fd1) == active_level) 
         output = 1;
    }
    else {
      output = (digitalRead(value_fd1) == active_level ? 1:2);
    }
  }
  
  /* check input 2 (if defined) */
  if (value_fd2) {
    if (a_poll[1].revents & POLLPRI) {
#if DEBUG    
      printf("Edge on input 2\n");
#endif
      if (digitalRead(value_fd2) == active_level) 
         output = 2;
    }
  }
  
  *flipflop_state = output;

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
  pid_t pid=getpid(); 
  syslog(LOG_DAEMON | LOG_NOTICE, "caught signal %d in process %d", signum, pid);
  
  switch (signum) {
    case SIGTERM:
    case SIGINT:
      /* Terminating parent process */
      if (pid==parentpid)
      {  
        /* Make parent terminate */
        cont = 0;
        syslog(LOG_DAEMON | LOG_NOTICE, "parent starting cleanup");
      }
      else
      {
        /* Clean up child process and terminate */
        if (pid != modbus_server_pid)
        {
          cleanup(status);
        }
        kill(pid, SIGKILL);
      }
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
  int fd;
  char b[32];
  char d[2];
  
  /* Check addr range */
  if((addr < FIRST_REG) || (addr > LAST_REG)) {
    syslog(LOG_DAEMON | LOG_ERR, "Address %d out of range\n", addr);
    return -1;
  }
  
  /* Open file containing the requested state value */
  snprintf(b, sizeof(b), "%s.%d_%d", FLIPFLOPSTATE_FILE, 
                                     status_param[addr-1].pin1, 
                                     status_param[addr-1].pin2);
  fd = open(b, O_RDONLY);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", b, strerror(errno));
    return -1;
  }

  /* Read state value */
  if (pread(fd, d, 1, 0) != 1) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to pread state value: %s\n",
                     strerror(errno));
    close(fd);
    return -1;
  }
  
  d[1]=0;
  *reg_val_p = atoi(d);
  
  close(fd);
  return 0;
}


/*
 * 
 * MAIN process
 * 
 */ 
int main(int argc, char* argv[])
{
  pid_t pid;
  unsigned int flipflop_state=0;
  int i,k;
  int num_flipflops=0;
  int last_flipflop;
   
   
  /* Parse input parameters */
  if ( (argc<3 ) )
  {
    printf("Usage:\n");
    printf("  statusd <pinX_1> <pinX_2> [-n] ...\n");
    printf("      pinX_1: kernel Id of first GPIO pin to monitor\n");
    printf("      pinX_2: kernel Id of second GPIO pin to monitor (0 if unused)\n");
    printf("          -n: pulse active low (default: active high)\n");
    printf("      Note: max number of pin pairs is %d\n", MAX_FLIPFLOPS);
    return 1;
  }

  openlog("statusd", LOG_PID|LOG_CONS, LOG_USER);
  syslog(LOG_DAEMON | LOG_NOTICE, "Starting Status monitor daemon (version %s)\n", VERSION);

  
  memset(status_param, 0, sizeof(status_param));
  for (i=1, k=0; i<(argc); i++, k++)
  {
    /* Get pin Ids for each flip flop */
    status_param[k].pin1 = atoi(argv[i++]);
    status_param[k].pin2 = atoi(argv[i]);
    
    /* ACTIVE_HIGH mode (default) */
    status_param[k].lmode=ACTIVE_HIGH;

    /* is this the last parameter ? */
    if (i<(argc-1))
    {
      /* is next parameter "-n" ? */
      if (!strcmp(argv[i+1], "-n"))
      {
        /* ACTIVE_LOW mode requested */
        status_param[k].lmode=ACTIVE_LOW;
        i++;
      }
    }
    
    /* Count number of defined flip flops */
    if (status_param[k].pin1)
      num_flipflops++;
#if DEBUG
    printf("status_param[%d].pin1=%d\n", k, status_param[k].pin1);
    printf("status_param[%d].pin2=%d\n", k, status_param[k].pin2);
    printf("status_param[%d].lmode=%d\n\n", k, status_param[k].lmode);
#endif
  }
  
  /* Save index of last defined flip flop*/
  last_flipflop = k;
  
#if DEBUG
  printf("num_flipflops=%d\n", num_flipflops);
  printf("last_flipflop=%d\n", last_flipflop);  
#endif
  
  /* Install signal handler for SIGTERM and SIGINT ("CTRL C") 
   * to be used to cleanly terminate parent annd child  processes
   */
  signal(SIGTERM, doExit);
  signal(SIGINT, doExit);
  
  /* Save the parent pid (used in signal handler) */
  parentpid = getpid();

  /* Init status struct */
  memset((void*)&status, 0, sizeof(status));
  
  syslog(LOG_DAEMON | LOG_NOTICE, "Creating %d status monitor processes", num_flipflops);
  
  /* Create a child process for each flip flop */
  for (i=0; i<last_flipflop; i++)
  {  
    /* Check for dummy pin */
    if (status_param[i].pin1 == 0) continue;
    
    /* Create child */
    pid=fork();
    
    if (pid == -1)
    {
      syslog(LOG_DAEMON | LOG_ERR, "Error creating child process for flip flip %d", i+1);
      return 2;
    }
    
    if (pid != 0)
    {
      /***** We are in the parent process *****/

      /* Save child pid */
      status_param[i].child_pid=pid;
    }
    else
    {
      /***** We are in the child process *****/
         
      if (status_param[i].pin2) 
      {
        syslog(LOG_DAEMON | LOG_NOTICE, " Started child process monitoring input pins %d and %d for changes\n", 
                                                                   status_param[i].pin1, status_param[i].pin2);
      }
      else
      {
        syslog(LOG_DAEMON | LOG_NOTICE, " Started child process monitoring single input pin %d for changes\n", 
                                                                                        status_param[i].pin1);
      }
      syslog(LOG_DAEMON | LOG_NOTICE, " Using %s logic", 
                                            (status_param[i].lmode==ACTIVE_HIGH)?"ACTIVE_HIGH":"ACTIVE_LOW" );

      
      /* Init */
      if (setup(status_param[i], &status) != 0)
      { 
        /* Setup failed, wait to be terminated */
        while (1) usleep(100000);
      }
  

      /***** Main child loop *****/
      while (1) 
      {
        if (waitForEdge(&flipflop_state, status.value_fd1, status.value_fd2, status.lmode) != 0)
        {
          /* waitForEdge failed */
          syslog(LOG_DAEMON | LOG_ERR, "Error during edge detection on pins %d and %d\n", 
                                                               status.pin1, status.pin2);
        }
        else
        {
          char str[20];
          sprintf(str, "%d\n", flipflop_state);
          pwrite(status.export_fd, str, strlen(str), 0);
#if DEBUG    
          printf("Detected edge for pins %d/%d, new input state: %d\n", status.pin1, status.pin2, flipflop_state);
#endif
        }

        /* Sleep for 500ms */
        usleep(500000);
      }
    }
  }
  
  /* Create child process for Modbus TCP server */
  modbus_server_pid=fork();
  if (modbus_server_pid == -1)
  {
    syslog(LOG_DAEMON | LOG_ERR, "Error creating child process for Modbus server\n");
    return 5;
  }
    
  if (modbus_server_pid == 0)
  {
    /***** We are in the child process *****/
    
    /* Start Modbus TCP server loop */
    modbustcp_server(MODBUS_SLAVE_ADDRESS,  // Modbus slave address
                     read_register_handler, // Read register handler
                     NULL                   // Write register handler
                    );
  }
  else
  {
    /***** We are in the parent process *****/

    /* Wait for termination signal */
    while (cont) sleep(1);
    usleep(500000);
  }

  /* Wait for all status monitor child processes to terminate */
  for (i=0; i<last_flipflop; i++)
  {
    /* Check for dummy pin */
    if (status_param[i].pin1 == 0) continue;
    
    kill(status_param[i].child_pid, SIGTERM);
    if (waitpid(status_param[i].child_pid, NULL, 0) == status_param[i].child_pid)
    {
      syslog(LOG_DAEMON | LOG_NOTICE, "Child process %d successfully terminated", status_param[i].child_pid);
    }
    else
    {
      syslog(LOG_DAEMON | LOG_ERR, "Error terminating child process %d", status_param[i].child_pid);
    }
  }
  
  /* Terminate Modbus server */
  kill(modbus_server_pid, SIGTERM);
  if (waitpid(modbus_server_pid, NULL, 0) == modbus_server_pid)
  {
    syslog(LOG_DAEMON | LOG_NOTICE, "Modbus server process %d successfully terminated", modbus_server_pid);
  }
  else
  {
    syslog(LOG_DAEMON | LOG_ERR, "Error terminating Modbus server process %d", modbus_server_pid);
  }
  
  syslog(LOG_DAEMON | LOG_NOTICE, "Exiting Status monitor daemon");
  closelog();
   
  return 0;
}
