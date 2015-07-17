/*
 * Sensor daemon
 *
 * Author: Ondrej Wisniewski
 *
 * Features:
 *  - Read 1-wire temperature and humidity sensors
 *  - Modbus TCP slave interface
 *
 * Build command (needs libmbsrv, libdht and lisht built and installed):
 *  gcc sensord.c -o sensord -lmbsrv -ldht -lsht `pkg-config --libs --cflags libmodbus`
 *
 * Author:  O. Wisniewski
 * Version: 0.6
 * Date:    2015/04/23
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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <sys/resource.h>

#include "modbustcp_server_lib.h"
#include "dht.h"
#include "sht21.h"

#define VERSION "0.6"

#define MODBUS_SLAVE_ADDRESS MODBUS_SLAVE_SENSOR_MODULE

/* 
 * Linux process priorities range from -20 to 10, where
 * higher values mean less priority. Default value is 0.
 */
#define PROC_PRIORITY -15

/* 
 * The DHT sensor uses a 1-wire communication 
 * and needs 1 GPIO pin (DATA)
 */
#define DHT_SENSOR1_PIN_DEFAULT 9   // SPI MISO on RPi
#define DHT_SENSOR2_PIN_DEFAULT 10  // SPI MOSI on RPi

/* 
 * The SHT sensor uses a 2-wire communication 
 * and needs 2 GPIO pins (DATA and CLOCK)
 * NOTE: CLK=DAT+1
 */
#define SHT_SENSOR1_DAT_PIN_DEFAULT 2  // I2C SDA on RPi
#define SHT_SENSOR1_CLK_PIN_DEFAULT 3  // I2C SCL on RPi
#define SHT_SENSOR2_DAT_PIN_DEFAULT 24 // GPIO 5 on RPi
#define SHT_SENSOR2_CLK_PIN_DEFAULT 25 // GPIO 6 on RPi

#define NUM_SENSOR_READ_RETRY 4


/*
 * Modbus register map of the SENSOR slave module:
 * 
 * ADDR  SOURCE              TYPE   DESCRIPTION
 * ---------------------------------------------
 *  1    /sensors/sensors1   R      temperature 1
 *  2    /sensors/sensors2   R      temperature 2
 *  3    /sensors/sensors3   R      temperature 3
 *  4    /sensors/sensors4   R      temperature 4
 *  5    /sensors/sensors5   R      temperature 5
 *  6    /sensors/sensors6   R      temperature 6
 *  7    /sensors/sensors7   R      temperature 7
 *  8    /sensors/sensors8   R      temperature 8
 *  9    /sensors/sensors9   R      temperature 9
 * 10    /sensors/sensors10  R      temperature 10
 * 11    GPIO #1    DHT22 1  R      humidity 1
 * 12    GPIO #1    DHT22 1  R      temperature 11
 * 13    GPIO #2    DHT22 2  R      humidity 2
 * 14    GPIO #2    DHT22 2  R      temperature 12
 * 15    GPIO #3/4  SHT22 3  R      humidity 3
 * 16    GPIO #3/4  SHT22 3  R      temperature 13
 * 17    GPIO #5/6  SHT22 4  R      humidity 4
 * 18    GPIO #5/6  SHT22 4  R      temperature 14
 */

/* 1 wire sensor definitions */
#define MAX_1W_SENSORS  (int)10
#define FIRST_1W_REG    (int)1
#define LAST_1W_REG     (int)(FIRST_1W_REG+MAX_1W_SENSORS-1)

static const char* reg_map_1w[MAX_1W_SENSORS+1] =
{  
 /*  0 */   "dummy",
 /*  1 */   "/sensors/sensor1",
 /*  2 */   "/sensors/sensor2",
 /*  3 */   "/sensors/sensor3",
 /*  4 */   "/sensors/sensor4",
 /*  5 */   "/sensors/sensor5",
 /*  6 */   "/sensors/sensor6",
 /*  7 */   "/sensors/sensor7",
 /*  8 */   "/sensors/sensor8",
 /*  9 */   "/sensors/sensor9",
 /*  10 */  "/sensors/sensor10"
};

/* DHT sensor defintions */
#define MAX_DHT_SENSORS (int)2
#define FIRST_DHT_REG   (int)(LAST_1W_REG+1)
#define LAST_DHT_REG    (int)(FIRST_DHT_REG+(2*MAX_DHT_SENSORS)-1)

static int reg_map_dht[] =
{  
 /*  11 */   DHT_SENSOR1_PIN_DEFAULT, 
 /*  12 */   DHT_SENSOR1_PIN_DEFAULT,
 /*  13 */   DHT_SENSOR2_PIN_DEFAULT,
 /*  14 */   DHT_SENSOR2_PIN_DEFAULT
};

/* SHT sensor defintions */
#define MAX_SHT_SENSORS (int)2
#define FIRST_SHT_REG   (int)(LAST_DHT_REG+1)
#define LAST_SHT_REG    (int)(FIRST_SHT_REG+(2*MAX_SHT_SENSORS)-1)

static int reg_map_sht[] =
{  
 /*  15 */   SHT_SENSOR1_DAT_PIN_DEFAULT, 
 /*  16 */   SHT_SENSOR1_DAT_PIN_DEFAULT,
 /*  17 */   SHT_SENSOR2_DAT_PIN_DEFAULT,
 /*  18 */   SHT_SENSOR2_DAT_PIN_DEFAULT
};

static int power_pin=0;


/**********************************************************
 * Function: read_1wire_sensor
 * 
 * Description:
 *           Read 1-wire temperature sensor via
 *           its sysfs file
 * 
 *           1w_therm sysfs File format :
 *           XX XX XX XX XX XX XX XX XX : crc=YY YES
 *           XX XX XX XX XX XX XX XX XX t=TTTTT
 * 
 *           Temperature Temp=TTTTT/1000
 * 
 * Returns:  0 on success, <0 otherwise
 *          -1: address out of range
 *          -2: failed to open sysfs file
 *          -3: failed to read sysfs file
 *          -4: CRC error
 *********************************************************/
int read_1wire_sensor(int addr, int *reg_val_p)
{  
   int rc = 0;
   int fd;
   char str[80];
   char *substr_p;
   char filename[32];
   
   memset(str, 0, 80);

   /* Register address range check */
   if (addr<FIRST_1W_REG || addr>LAST_1W_REG)
   {
      return -1;
   }
   
   snprintf(filename, sizeof(filename), "%s", reg_map_1w[addr]);
   fd = open(filename, O_RDONLY);
   if (fd < 0) {
      syslog(LOG_DAEMON | LOG_ERR, "Open %s: %s\n", filename, strerror(errno));
      rc = -2;
   }
   else if (pread(fd, str, 80, 0) == -1) {
      syslog(LOG_DAEMON | LOG_ERR, "Unable to pread temperature value: %s\n", strerror(errno));
      rc = -3;
   }
   else {
      /* Check if CRC is OK */
      substr_p = strstr(str, "crc=");
      if ((char)*(substr_p+7) == 'Y') {
         
         /* Read temperature value, convert to tenth of degrees */
         substr_p = strstr(str, "t=");
         *reg_val_p = atoi(substr_p+2)/100;
      }
      else {
         syslog(LOG_DAEMON | LOG_ERR, "CRC error in file %s\n", filename);
         rc = -4;
      }
   }
   
   close(fd);
   return rc;
}

/**********************************************************
 * Function: read_dht_sensor
 * 
 * Description:
 *           Read DHT temperature and humidity sensor
 *           using the DHT library
 * 
 * Returns:  0 on success, <0 otherwise
 *          -1: address out of range
 *          -2: DHT setup failed
 *          -3: sensor reading failed
 *********************************************************/
int read_dht_sensor(int addr, int *reg_val_p)
{  
   int retry = NUM_SENSOR_READ_RETRY;
   float fval;
   DHT_ERROR_t ecode;
  
   /* Register address range check */
   if (addr<FIRST_DHT_REG || addr>LAST_DHT_REG)
   {
      return -1;
   }
   
   /* Init sensor communication */
   dhtSetup(reg_map_dht[addr-FIRST_DHT_REG], DHT22);
   if (getStatus() != ERROR_NONE)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Error during DHT setup: %s\n", getStatusString());
      return -2;
   }

   /* Read sensor with retry (after delay) */
   do    
   {
      readSensor();
      /* Quick'n'dirty hack: always wait after sensor reading to avoid
       * reading too frequently. This should be implemented properly
       * in the dhtlib.
       */
      sleep(2);
      ecode = getStatus();
   }
   while ((ecode != ERROR_NONE) && retry--);
   
   /* Cleanup */
   dhtCleanup();
   
   if (ecode != ERROR_NONE)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Error reading DHT sensor after %d retries\n", NUM_SENSOR_READ_RETRY);
      if (power_pin)
      {
         /* Sensor might have locked up (happens occasionally), reset it */
         syslog(LOG_DAEMON | LOG_NOTICE, "Resetting DHT sensors\n");
         dhtReset(power_pin);
      }
      return -3;
   }
   else
   {
      //syslog(LOG_DAEMON | LOG_NOTICE, "DEBUG: H=%3.1f %%, T=%3.1f C (%d retries)\n", 
      //       getHumidity(), getTemperature(), NUM_SENSOR_READ_RETRY-retry);

      /* According to our register map, odd address
       * means humidiy, even means temperature 
       */
      fval = (addr%2) ? getHumidity():getTemperature();
      *reg_val_p = (int)(fval*10);
   }
   
   return 0;
}

/**********************************************************
 * Function: read_sht_sensor
 * 
 * Description:
 *           Read SHT temperature and humidity sensor
 *           using the SHT library
 * 
 * Returns:  0 on success, <0 otherwise
 *          -1: address out of range
 *          -2: SHT setup failed
 *          -3: sensor reading failed
 *********************************************************/
int read_sht_sensor(int addr, int *reg_val_p)
{  
   int16_t temperature;
   uint16_t humidity;
   uint8_t clk_pin;
   uint8_t dat_pin;
   uint8_t ecode;
  
   /* Register address range check */
   if (addr<FIRST_SHT_REG || addr>LAST_SHT_REG)
   {
      return -1;
   }
   
   dat_pin = reg_map_sht[addr-FIRST_SHT_REG];
   clk_pin = dat_pin + 1;
   
   /* Fix for GPIO pin renaming 21-->27 on RPi B, rev.2 */
   if (dat_pin==21) dat_pin=27;
   
   /* Init sensor communication */
   if (SHT21_Init(clk_pin, dat_pin) != 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Error during SHT setup\n");
      return -2;
   }

   /* Read sensor with retry (after delay) */
   ecode = SHT21_Read(&temperature, &humidity);
   
   /* Cleanup */
   /* NOTE: Calling SHT21_Cleanup() here causes sensord to segfault.
    * The reason is unknown. This needs some more investigation. But 
    * since a cleanup is not strictly necessary we simply don't do it. 
    */
   //SHT21_Cleanup();
   
   if (ecode != 0)
   {
      syslog(LOG_DAEMON | LOG_ERR, "Error 0x%X reading SHT sensor\n", ecode);
      return -3;
   }
   else
   {
      /* According to our register map, odd address
       * means humidiy, even means temperature 
       */
      *reg_val_p = (addr%2) ? (int)humidity:(int)temperature;
   }
   
   return 0;
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
   int rc;
   int retry = NUM_SENSOR_READ_RETRY;
   
   if((addr >= FIRST_1W_REG) && (addr <= LAST_1W_REG))
   {   
      /* read temperature sensor (with retry in case of CRC error) */
      do {
         rc = read_1wire_sensor(addr, reg_val_p);
      } while ((rc==-4) && retry--);
   }
   else if ((addr >= FIRST_DHT_REG) && (addr <= LAST_DHT_REG))
   {  
      /* read DHT humidity sensor (implicit retry) */
      rc = read_dht_sensor(addr, reg_val_p);
   }
   else if ((addr >= FIRST_SHT_REG) && (addr <= LAST_SHT_REG))
   {  
      /* read SHT humidity sensor */
      rc = read_sht_sensor(addr, reg_val_p);
   }
   else
   {
      rc = -1;
   }
   
   return rc;
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
   syslog(LOG_DAEMON | LOG_NOTICE, "caught signal %d in process %d\n", signum, pid);
  
   switch (signum) {
      case SIGTERM:
      case SIGINT:
         /* Terminating parent process */
         if (power_pin)
         {
            syslog(LOG_DAEMON | LOG_NOTICE, "Power off DHT sensors\n");
            dhtPoweroff(power_pin);
         }
         syslog(LOG_DAEMON | LOG_NOTICE, "Exiting Sensor daemon\n");
         closelog();
         kill(pid, SIGKILL);
      break;
      
      default:
         syslog(LOG_DAEMON | LOG_ERR, "unexpected signal received\n");
   }
}


/**********************************************************
 * 
 * Main function
 * 
 *********************************************************/
int main(int argc, char* argv[])
{
   int i;
   
   
   openlog("sensord", LOG_PID|LOG_CONS, LOG_USER);
   syslog(LOG_DAEMON | LOG_NOTICE, "Starting Sensor daemon (version %s)\n", VERSION);
      
   /* Parse command line to read GPIO pins for DHT and SHT sensors 
    * Format: 
    *    sensord <DHT1 pin>  <DHT2 pin> <DHT pwr pin> <SHT1 scl pin> <SHT2 scl pin> 
    */
   for (i=0; i<(argc-1) && i<(MAX_DHT_SENSORS+MAX_SHT_SENSORS+1); i++)
   {
      switch(i)
      {
         case 0: // parameter 1,2: DHT sensor pins
         case 1:
            /* Override default settings for DHT sensor pins */
            reg_map_dht[2*i] = atoi(argv[i+1]);
            reg_map_dht[2*i+1] = atoi(argv[i+1]);
            break;
            
         case 2: // parameter 3: DHT power pin
            power_pin = atoi(argv[i+1]);
            break;
               
         case 3: // parameter 4,5: SHT sensor pins
         case 4:
            /* Override default settings for SHT sensor pins */
            reg_map_sht[2*(i-3)] = atoi(argv[i+1]);
            reg_map_sht[2*(i-3)+1] = atoi(argv[i+1]);;
            break;            
      }
      /* TODO: check validity of Kernel Ids for GPIO pins */
   }
   
   for (i=0; i<MAX_DHT_SENSORS; i++)
   {
      if (reg_map_dht[2*i])
      {
         syslog(LOG_DAEMON | LOG_NOTICE, "Using DHT sensor %d on GPIO pin %d", i+1, reg_map_dht[2*i]);
      }
      else
      {
         syslog(LOG_DAEMON | LOG_NOTICE, "Using DHT sensor %d on SPI interface (MISO line)", i+1);
      }
   }
   
   for (i=0; i<MAX_SHT_SENSORS; i++)
   {
      if (reg_map_sht[2*i])
      {
         syslog(LOG_DAEMON | LOG_NOTICE, "Using SHT sensor %d on GPIO pins %d (SDA) and %d (SCL)", 
                                          i+1, reg_map_sht[2*i], reg_map_sht[2*i]+1);
      }
      else
      {
         syslog(LOG_DAEMON | LOG_NOTICE, "Using SHT sensor %d on I2C interface (SDA and SCL line)", i+1);
      }
   }
   
   if (power_pin)
   {
      syslog(LOG_DAEMON | LOG_NOTICE, "Using GPIO pin %d as power supply for DHT sensors", 
                                       power_pin);
      syslog(LOG_DAEMON | LOG_NOTICE, "Power on DHT sensors\n");
      dhtPoweron(power_pin);
   }
      
   /* Install signal handler for SIGTERM and SIGINT ("CTRL C") 
    * to be used to cleanly terminate and exit 
    * How do we exit from modbustcp_server() when TERM signal is received ???
    */
   signal(SIGTERM, doExit);
   signal(SIGINT, doExit);
   
   /* Set process priority to a high value to increase reliability  
    * of the time critical code in the DHT sensor driver.
    */
   if ( setpriority(PRIO_PROCESS, getpid(), PROC_PRIORITY) != 0 )
   {
      syslog(LOG_DAEMON | LOG_ERR, "Failed to increase process priority\n");
   }

   /* Start Modbus TCP server loop */
   modbustcp_server(MODBUS_SLAVE_ADDRESS,  // Modbus slave address
                    read_register_handler, // Read register handler
                    NULL                   // Write register handler
                   );
  
   syslog(LOG_DAEMON | LOG_NOTICE, "Exiting Sensor daemon\n");
   closelog();
   
   return 0;
}
