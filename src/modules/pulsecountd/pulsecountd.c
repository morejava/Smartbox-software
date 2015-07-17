/******************************************************************
* 
* Pulse counter daemon for embedded SBC (e.g. ACME FoxG20, RaspberryPi)
* 
*
* Author: Ondrej Wisniewski
* 
* Features:
*  - Count pulses on GPIO pins
*  - Filtering of glitches
*  - Handle active high or active low logic
*
* Build command:
*  gcc pulsecountd.c -o pulsecountd -lrt -lmbsrv `pkg-config --libs --cflags libmodbus`
*   
* Changelog:
*   04-11-2013: Initial version
*   10-12-2013: Added virtual counters
*   16-12-2013: Added Modbus server functionality
*   12-03-2014: Handle the case of statefile not created yet when setup() runs
*   17-07-2014: Added possibility to specify dummy counter lines
*   14-10-2014: Added filtering of glitches, 
*               Added handling of active high or active low logic
*   19-11-2014: Permit fractional numbers as divisors
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
******************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <poll.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "modbustcp_server_lib.h"


#define VERSION "0.7"

/* GPIO sysfs interface */
#define EXPORT_FILE    "/sys/class/gpio/export"
#define UNEXPORT_FILE  "/sys/class/gpio/unexport"
#define GPIO_BASE_FILE "/sys/class/gpio/gpio"

/* export file for pulse counters */
#define PULSECOUNT_FILE "/tmp/pulsecount"
#define MAX_COUNTERS 8 

#define DEBUG 0

#define MODBUS_SLAVE_ADDRESS MODBUS_SLAVE_COUNTER_MODULE

#define FIRST_REG 1
#define LAST_REG 16

/*
 * Modbus register map of the COUNTER slave module:
 * 
 * ADDR  SOURCE               TYPE  DESCRIPTION
 * ---------------------------------------------
 *  1    /tmp/pulsecount<1>_1   R   counter 1_1
 *  2    /tmp/pulsecount<1>_2   R   counter 1_2
 *  3    /tmp/pulsecount<2>_1   R   counter 2_1
 *  4    /tmp/pulsecount<2>_2   R   counter 2_2
 *  5    /tmp/pulsecount<3>_1   R   counter 3_1
 *  6    /tmp/pulsecount<3>_2   R   counter 3_2
 *  7    /tmp/pulsecount<4>_1   R   counter 4_1
 *  8    /tmp/pulsecount<4>_2   R   counter 4_2
 *  9    /tmp/pulsecount<5>_1   R   counter 5_1
 *  10   /tmp/pulsecount<5>_2   R   counter 5_2
 *  11   /tmp/pulsecount<6>_1   R   counter 6_1
 *  12   /tmp/pulsecount<6>_2   R   counter 6_2
 *  13   /tmp/pulsecount<7>_1   R   counter 7_1
 *  14   /tmp/pulsecount<7>_2   R   counter 7_2
 *  15   /tmp/pulsecount<8>_1   R   counter 8_1
 *  16   /tmp/pulsecount<8>_2   R   counter 8_2
 */

typedef enum {
   LOW,
   HIGH
}
PIN_STATE_t;

typedef struct
{
   int   pin;
   float divisor;
   char  statefile[32];
   pid_t child_pid;
}
COUNTERPARAM_t;

typedef struct
{
   int pin;                    /* GPIO pin Kernel Id */
   int value_fd;               /* GPIO sysfs value file descripter */
   int state_fd;               /* State file descripter */
   int export1_fd;             /* Counter 1 export file descripter */
   int export2_fd;             /* Counter 2 export file descripter */
   unsigned long pulse_count1; /* Counter 1 */
   unsigned long pulse_count2; /* Counter 2 */
   float divisor;
}
COUNTER_t;

/* Global variables */
static COUNTER_t counter;
COUNTERPARAM_t counter_param[MAX_COUNTERS];

static int cont = 1;
static pid_t parentpid;
static pid_t modbus_server_pid;


/*********************************************************************
 * Function: setup()
 * 
 * Description: Setup of globally used resources
 * 
 * Parameters: param     - Parameter struct containing (in)
 *                         Pin number, divisor and status file name
 *             counter_p - Pointer to counter struct (out)
 * 
 * Return:     0 if cuccessful, >0 in case of error
 * 
 ********************************************************************/
static int setup(COUNTERPARAM_t param, COUNTER_t* counter_p)
{
  int fd;
  char b[64];

  // Prepare GPIO pin connected to sensors data pin to be used with GPIO sysfs
  // (export to user space)
  fd = open(EXPORT_FILE, O_WRONLY);
  if (fd < 0) {
    perror(EXPORT_FILE);
    return 1;
  }  
  snprintf(b, sizeof(b), "%d", param.pin);
  if (pwrite(fd, (void *)b, strlen(b), 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to export pin=%d (already in use?): %s",
            param.pin, strerror(errno));
    return 2;
  }  
  close(fd);
  
  /* Save pin Id and divisor */
  counter_p->pin=param.pin;
  counter_p->divisor=param.divisor;
  
  // Define edge interrupt (only both edges are supported on FoxG20)  
  // to be used later with the poll() function for edge detection
  snprintf(b, sizeof(b), "%s%d/edge", GPIO_BASE_FILE, param.pin);
  fd = open(b, O_RDWR);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s", b, strerror(errno));
    return 3;
  }
  if (pwrite(fd, "both", 4, 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to write 'both' to %s: %s",
            b, strerror(errno));
    return 4;
  }
  close(fd);
  
  // Define gpio direction as input
  snprintf(b, sizeof(b), "%s%d/direction", GPIO_BASE_FILE, param.pin);
  fd = open(b, O_RDWR);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s", b, strerror(errno));
    return 5;
  }
  if (pwrite(fd, "in", 2, 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to write 'in' to %s: %s",
            b, strerror(errno));
    return 6;
  }
  close(fd);
  
  // Open gpio value file for fast reading/writing when requested
  snprintf(b, sizeof(b), "%s%d/value", GPIO_BASE_FILE, param.pin);
  fd = open(b, O_RDWR);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s", b, strerror(errno));
    return 7;
  }
  counter_p->value_fd=fd;

  // Open (or create) pulse count export file 1
  snprintf(b, sizeof(b), "%s%d_1", PULSECOUNT_FILE, param.pin);
  fd = open(b, O_WRONLY|O_CREAT);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s", b, strerror(errno));
    return 8;
  }
  if (pwrite(fd, "0000000000\n", 11, 0) < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to write '0' to %s: %s",
            b, strerror(errno));
    return 9;
  }
  counter_p->export1_fd=fd;

  if (strlen(param.statefile)) 
  {
    // Open (or create) pulse count export file 2
    snprintf(b, sizeof(b), "%s%d_2", PULSECOUNT_FILE, param.pin);
    fd = open(b, O_WRONLY|O_CREAT);
    if (fd < 0) {
       syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s", b, strerror(errno));
       return 10;
    }
    if (pwrite(fd, "0000000000\n", 11, 0) < 0) {
       syslog(LOG_DAEMON | LOG_ERR, "Unable to write '0' to %s: %s",
               b, strerror(errno));
       return 11;
    }
    counter_p->export2_fd=fd;
    
    // wait for statefile to be created (this ugly hack will do for now)
    sleep(3);
    
    // Open status file
    snprintf(b, sizeof(b), "%s", param.statefile);
    fd = open(b, O_RDONLY);
    if (fd < 0) {
      syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s", b, strerror(errno));
      return 10;
    }
    counter_p->state_fd=fd;
  }
  else 
  {
    counter_p->export2_fd=0;
    counter_p->state_fd=0;
  }
  
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
static void cleanup(COUNTER_t counter)
{
  int fd;
  char b[8];

  // close pulse count export files
  close(counter.export1_fd);
  if (counter.export2_fd)
     close(counter.export2_fd);

  // close GPIO value file
  close(counter.value_fd);

  // close state file
  if (counter.state_fd)
     close(counter.state_fd);

  // free GPIO pin connected to sensors data pin to be used with GPIO sysfs  
  if (counter.pin)
  {
     fd = open(UNEXPORT_FILE, O_WRONLY);
     if (fd < 0) {
        perror(UNEXPORT_FILE);
        return;
     }
     snprintf(b, sizeof(b), "%d", counter.pin);
     if (pwrite(fd, b, strlen(b), 0) < 0) {
        syslog(LOG_DAEMON | LOG_ERR, "Unable to unexport pin=%d: %s",
               counter.pin, strerror(errno));
        return;
     }
     close(fd);
  }
}


/*********************************************************************
 * Function:    digitalRead()
 * 
 * Description: Read from the data pin
 * 
 * Parameters:  none
 * 
 ********************************************************************/
static PIN_STATE_t digitalRead(int value_fd)
{
  int fd=value_fd;
  char d[2]={0,0};

  if (fd) {
    if (pread(fd, (void*)d, 1, 0) != 1) {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to pread gpio value: %s",
                                    strerror(errno));
    }
  }
  return (d[0] == '0' ? LOW : HIGH);
}


/*********************************************************************
 * Function:    stateRead()
 * 
 * Description: Read from the state file
 * 
 * Parameters:  none
 * 
 ********************************************************************/
static unsigned char stateRead(int state_fd)
{
  int fd=state_fd;
  char d[2]={0,0};

  if (fd) {
    if (pread(fd, (void*)d, 1, 0) != 1) {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to pread state value: %s",
                                    strerror(errno));
    }
    return (atoi(d));
  }
  else
    return (1);
}


/**********************************************************
 * Function: time_diff_ms()
 * 
 * Description:
 *           Calculate the difference between two time 
 *           values in milliseconds
 * 
 * Returns:  time difference (in ms)
 *********************************************************/
static unsigned long time_diff_ms(struct timespec now_ts, struct timespec prev_ts)
{
   unsigned long diff_sec;
   unsigned long diff_msec;
   
   if (now_ts.tv_nsec > prev_ts.tv_nsec)
   {
      /* normal difference */
      diff_msec = (now_ts.tv_nsec - prev_ts.tv_nsec)/1000000;
      diff_sec = now_ts.tv_sec - prev_ts.tv_sec;
   }
   else
   {  /* handle zero crossing */
      diff_msec = (1000000000 - prev_ts.tv_nsec + now_ts.tv_nsec)/1000000;
      diff_sec = now_ts.tv_sec - prev_ts.tv_sec - 1;
   }
   
   /* return difference in milliseconds (this will *
    * overflow if difference is greater 49,7 days) */
   return ((diff_sec*1000) + diff_msec);
}


/*********************************************************************
 * Function:    waitForEdge()
 * 
 * Description: Waits for an edge change on the data pin
 * 
 * Parameters:  none
 * 
 ********************************************************************/
static int waitForEdge(PIN_STATE_t* value, int value_fd)
{
  struct pollfd a_poll;
  int fd=value_fd;
  int res;
  
  a_poll.fd = fd;
  a_poll.events = POLLPRI;
  a_poll.revents = 0;
  
  //digitalRead();
  res = poll(&a_poll, 1, -1);
  if (res < 0) {   // error
    syslog(LOG_DAEMON | LOG_ERR, "poll() failed: %s", strerror(errno));
    return 1;
  }

  if (a_poll.revents & (POLLPRI | POLLERR)) {
    *value=digitalRead(fd);
    return 0;
  }
  else {
    syslog(LOG_DAEMON | LOG_ERR, "poll() detected unknown event");
    return 2;
  }
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
      }
      else
      {   
        /* Clean up child process and terminate */
        if (pid != modbus_server_pid)
           cleanup(counter);
        kill(pid, SIGKILL);
      }
      break;
      
    default:
      syslog(LOG_DAEMON | LOG_ERR, "unexpected signal received");
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
  char d[11];
  int suffix;
  int pin;
  
  /* Check addr range */
  if((addr < FIRST_REG) || (addr > LAST_REG)) {
    syslog(LOG_DAEMON | LOG_ERR, "Address %d out of range", addr);
    return -1;
  }
  
  suffix=(addr%2)?1:2;
  pin=counter_param[(addr-1)/2].pin;
   
  /* Open counter file containing requested counter value */
  snprintf(b, sizeof(b), "%s%d_%d", PULSECOUNT_FILE, pin, suffix);
  fd = open(b, O_RDONLY);
  if (fd < 0) {
    syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s", b, strerror(errno));
    return -1;
  }

  /* Read counter value */
  if (pread(fd, (void *)d, 10, 0) != 10) {
    syslog(LOG_DAEMON | LOG_ERR, "Unable to pread counter value: %s",
                     strerror(errno));
    close(fd);
    return -1;
  }
  
  d[10]=0;
  *reg_val_p = atoi(d);
  
  close(fd);
  return 0;
}


/*********************************************************************
 * 
 * MAIN process
 * 
 ********************************************************************/ 
int main(int argc, char* argv[])
{
  pid_t pid;
  int num_counters=0;
  int last_counter;
  int i, k;
  PIN_STATE_t pin_value;
  PIN_STATE_t active_value=LOW;
  struct timespec pulse_start_time, pulse_end_time;
  unsigned char pulse_started;
  
   
  /* Parse input parameters */
  if ((argc==1) || ((argc==2) && !strcmp(argv[1], "-h")))
  {
    printf("Usage:\n");
    printf("  pulsecountd <pin1> <div1> [<statefile1>] [<pin2> <div2> [<statefile2>] ... [<pinN> <divN> [<statefileN>]]]\n");
    printf("      pinX:       kernel Id of GPIO pin to count pulses on (use 0 for dummy pin)\n");
    printf("      divX:       divisor for pulse count values\n");
    printf("      statefileX: file containing the counter selection\n");
    printf("                  (optional, used for dual virtual counters)\n\n");
    printf("      Note: max number of counters is %d\n", MAX_COUNTERS);
    return 1;
  }
 
  memset(counter_param, 0, sizeof(counter_param));
  for (i=1, k=0; i<(argc); i++, k++)
  {
    /* Get pin Id and divisor for each counter */
    counter_param[k].pin = atoi(argv[i++]);
    counter_param[k].divisor = atof(argv[i]);
     
    /* is this the last parameter? */
    if (i<(argc-1))
    {
      /* is next parameter a string (filename)? */
      if ((*argv[i+1] < '0') || (*argv[i+1] > '9'))
      {
        /* next parameter is the optional statefile name, save it */
        strcpy(counter_param[k].statefile, argv[++i]);
      }
    }
    if (counter_param[k].pin) num_counters++;
#if 0
    printf("counter_param[%d].pin=%d\n", k, counter_param[k].pin);
    printf("counter_param[%d].divisor=%f\n", k, counter_param[k].divisor);
    printf("counter_param[%d].statefile=%s\n\n", k, counter_param[k].statefile);
#endif
  }
  last_counter = k;

  openlog("pulsecountd", LOG_PID|LOG_CONS, LOG_USER);
  syslog(LOG_DAEMON | LOG_NOTICE, "Starting Pulse counter daemon (version %s)", VERSION);
  syslog(LOG_DAEMON | LOG_NOTICE, "Using %s logic", (active_value==HIGH)?"ACTIVE_HIGH":"ACTIVE_LOW" );

  /* Install signal handler for SIGTERM and SIGINT ("CTRL C") 
   * to be used to cleanly terminate parent annd child  processes
   */
  signal(SIGTERM, doExit);
  signal(SIGINT, doExit);
  
  /* Save the parent pid (used in signal handler) */
  parentpid = getpid();

  /* Init counter struct */
  memset((void*)&counter, 0, sizeof(counter));
  
  syslog(LOG_DAEMON | LOG_NOTICE, "Creating %d counter processes", num_counters);
  
  /* Create a child process for each counter */
  for (i=0; i<last_counter; i++)
  {  
    /* Check for dummy pin */
    if (counter_param[i].pin == 0) continue;
    
    /* Create child */
    pid=fork();
    
    if (pid == -1)
    {
      syslog(LOG_DAEMON | LOG_ERR, "Error creating child process for counter %d", i+1);
      return 2;
    }
    
    if (pid != 0)
    {
      /***** We are in the parent process *****/

      /* Save child pid */
      counter_param[i].child_pid=pid;
    }
    else
    {
      /***** We are in the child process *****/
         
      syslog(LOG_DAEMON | LOG_NOTICE, "Started child process counting pulses on GPIO pin with Kernel Id %d", 
                                                                                      counter_param[i].pin);
      
      /* Init */
      if (setup(counter_param[i], &counter) != 0)
      {
        /* Setup failed, wait to be terminated */
        while (1) usleep(100000);
      }
      
      /* Wait for possibly ongoing pulse to end */   
      while (digitalRead(counter.value_fd) == active_value) usleep(100000);
      pulse_started = 0;
      
      
      /***** Main child loop *****/
      while (1)
      {
        if (waitForEdge(&pin_value, counter.value_fd) == 0)
        {
          if (pin_value == active_value)
          {
            /* Pulse started */
            if (pulse_started == 0)
            {
              clock_gettime(CLOCK_REALTIME, &pulse_start_time);
              pulse_started = 1;
            }
            else
            {
#if DEBUG
              syslog(LOG_DAEMON | LOG_DEBUG, "Warning: detected starting pulse out of sequence on pin %d",
                                              counter_param[i].pin);
#endif
            }
          }
          else
          {
            char str[20];
            
            /* Pulse ended */
            if (pulse_started == 1)
            {
              clock_gettime(CLOCK_REALTIME, &pulse_end_time);
#if DEBUG
              syslog(LOG_DAEMON | LOG_DEBUG, "Detected pulse with length %lu ms on pin %d", 
                                              time_diff_ms(pulse_end_time, pulse_start_time), counter_param[i].pin);
#endif
              pulse_started = 0;
            }
            else
            {
#if DEBUG
              syslog(LOG_DAEMON | LOG_DEBUG, "Warning: detected ending pulse out of sequence on pin %d",
                                              counter_param[i].pin);
#endif
              continue;
            }
            
            /* TODO: check pulse lenght and filter glitches */
            
            /* Check statefile for which virtual counter to increment */
            switch (stateRead(counter.state_fd))
            {
               case 1:
                  /* Increment counter 1, apply divisor and save value */
                  counter.pulse_count1++;
                  sprintf(str, "%010lu\n", (unsigned long)(counter.pulse_count1/counter.divisor));
                  pwrite(counter.export1_fd, str, strlen(str), 0);
                  break;
                  
               case 2:
                  /* Increment counter 2, apply divisor and save value */
                  counter.pulse_count2++;
                  sprintf(str, "%010lu\n", (unsigned long)(counter.pulse_count2/counter.divisor));
                  pwrite(counter.export2_fd, str, strlen(str), 0);
                  break;
                  
               default:
                  /* unknown state, do nothing */
                  ;
                  //printf("unknown value %d in statefile\n", stateRead(counter.state_fd));
            }
          }
        }
        else
        {
          /* waitForEdge failed, wait to be terminated */
          while (1) usleep(100000);
        }
      }
    }
  }
  
  /* Create child process for TCP server */
  modbus_server_pid=fork();
  if (modbus_server_pid == -1)
  {
    syslog(LOG_DAEMON | LOG_ERR, "Error creating child process for Modbus server");
    return 3;
  }
    
  if (modbus_server_pid == 0)
  {
    /***** We are in the child process *****/
    
    modbus_server_pid = getpid();
   
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
  }

  
  /* Wait for all counter child processes to terminate */
  for (i=0; i<last_counter; i++)
  {
    /* Check for dummy pin */
    if (counter_param[i].pin == 0) continue;
    
    kill(counter_param[i].child_pid, SIGTERM);
    if (waitpid(counter_param[i].child_pid, NULL, 0) == counter_param[i].child_pid)
    {
      syslog(LOG_DAEMON | LOG_NOTICE, "Child process %d successfully terminated", counter_param[i].child_pid);
    }
    else
    {
      syslog(LOG_DAEMON | LOG_ERR, "Error terminating child process %d", counter_param[i].child_pid);
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
  
  syslog(LOG_DAEMON | LOG_NOTICE, "Exiting Pulse counter daemon");
  closelog();
   
  return 0;
}
