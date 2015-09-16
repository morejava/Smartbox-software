# Telegea Smartbox configuration 

The Telegea software installed on the Smartbox device can be customised to fit specific needs.  

The configuration is done by modifying the global device configuration file  

    /etc/telegea.conf

This file contains the complete configuration data for the Telegea Smartbox device. It uses the ini style format and is divided into sections. Each section decribes the configuration of a specific Telegea module. System wide parameters are defined in the `[generic]` section.  

For a detailed description of the single software modules and their parameters see the corresponding documentation.


### Generic device parameters

Section name is `[generic]`

Plant Id (assigned after registration on Telegea server)

    PLANTID=<xyz>

Geografic location (city name and contry code)  

    LOCATION=<mytown,mycountrycode>

HTTP API parameters (API_KEY assigned after registration on Telegea server)

    TELEGEA_API="https://www.telegea.org/input"
    API_KEY=<1234567890>

Storage location for raw sensor data (don't change)

    STORAGE_DIR=/media/data

Storage location of saved geodat files  (don't change)

    DATA_DIR=/geofox_data


### Parameters for the Register scanner (data logger) application

Section name is `[regscanner]`

Enable this module (true or false)

    REGSCANNER_ENABLED=false

Seconds between consecutive reads of registers

    INFRAREAD_DELAY=2   

Seconds between two complete read loops 

    INFRALOOP_DELAY=120 

Location of the file containing the register list

    REGISTER_LIST="/etc/telegea_mbreg_list.txt"


### Parameters for the Alarm monitor 

Section name is `[alrmonitor]`

Enable this module (true or false)

    ALRMONITOR_ENABLED=false

Seconds between consecutive reads of registers

    AMON_INFRAREAD_DELAY=2

Seconds between two complete read loops 

    AMON_INFRALOOP_DELAY=10 


### Parameters for the Thermostat application

Section name is `[thermostat]`

Enable this module (true or false)

    THERMOSTAT_ENABLED=false
    
Operating parameters

    MAX_TEMP=40
    MIN_TEMP=10
    MAX_OPERATING_TIME=120 ;minutes
    MIN_IDLE_TIME=5        ;minutes
    ROOM_TEMP_REG_NAME="Room temp"
    HEATCOOL_CTRL1_REG_NAME="Clima zone 1 switch"
    HEATCOOL_CTRL2_REG_NAME=
    MODBUS_DEVICE_IP="127.0.0.1"
    SEASON_MODE="heating"  ;heating or cooling
    USER_MODE="smart"      ;smart or dumb 
    TEMP_OFFSET=0


### Parameters for the Thermostat GUI application

Section name is `[thermostatgui]`

Enable this module (true or false)

    THERMOSTATGUI_ENABLED=false
    
IP address of the device where thermostat module is running

    THERMOSTAT_DEVICE_IP="127.0.0.1"


### Parameters for the Sensor module

Section name is `[sensord]`

Enable this module (true or false)

    SENSORD_ENABLED=false

List of Kernel Ids of the GPIO pins used for the DHT sensors  
*Add your Ids here if you don't use the default values*  
*Use value "0" to use SPI as communication interface instead of GPIO*

    DHT_SENSOR1_PIN=0 # default is 9
    DHT_SENSOR2_PIN=0 # default is 10

Optionally the DHT sensors can be powered via a GPIO pin. This allows to reset the sensor when it locks up

    DHT_POWER_PIN=0

List of Kernel Ids of the GPIO pins used for the SHT sensors  
*Add your Ids here for the DAT pin if you don't use the default values.*  
*DAT and CLK pins must be consecutive, specify only DAT pin here.*  
*Use value "0" to use I2C as communication interface instead of GPIO.*

    SHT_SENSOR1_DAT_PIN=2  # default is 2 for DAT, 3 for CLK
    SHT_SENSOR2_DAT_PIN=24 # default is 24 for DAT, 25 for CLK

GPIO line used by 1w-therm driver

    W1_THERM_PIN=4


### Parameters for the Control module

Section name is `[controld]`

Enable this module (true or false)

    CONTROLD_ENABLED=false

List of control files to read commands from (don't change)

    CONTROL1_FILE="/sensors/state1"
    CONTROL2_FILE="/sensors/state2"
    CONTROL3_FILE="/sensors/state3"
    CONTROL4_FILE="/sensors/state4"
    CONTROL5_FILE="/sensors/state5"
    CONTROL6_FILE="/sensors/state6"
    CONTROL7_FILE="/sensors/state7"
    CONTROL8_FILE="/sensors/state8"

List of Kernel Ids of the GPIO pins we want to control  
*Set to "0" if unused.*

    OUTPUT1_ID=22 ; Clima mode switch
    OUTPUT2_ID=23 ; Clima zone 1 switch
    OUTPUT3_ID=0  ; Clima zone 2 switch
    OUTPUT4_ID=0  ; Clima zone 3 switch
    OUTPUT5_ID=0  ; Clima zone 4 switch
    OUTPUT6_ID=0  ; Extra Control Switch 1 
    OUTPUT7_ID=0  ; Extra Control Switch 2
    OUTPUT8_ID=0  ; Spare


### Parameters for the Pulsecount module

Section name is `[pulsecountd]`

Enable this module (true or false)

    PULSECOUNTD_ENABLED=false

List of Kernel Ids of the GPIO pins we want to create a counter for  
*Set to "0" if unused.*

    COUNTER1_ID=18 ; Counter Reg1/2   (Flow clima/Flow hot water)
    COUNTER2_ID=27 ; Counter Reg3/4   (Flow hot water)
    COUNTER3_ID=0  ; Counter Reg5/6   (Dummy)
    COUNTER4_ID=0  ; Counter Reg7/8   (Dummy)
    COUNTER5_ID=17 ; Counter Reg9/10  (Energy)
    COUNTER6_ID=0  ; Counter Reg11/12 (Dummy)
    COUNTER7_ID=0  ; Counter Reg13/14 (Dummy)
    COUNTER8_ID=0  ; Counter Reg15/16 (Dummy)

List of divisors which are applied to the real counter values (can be <1)  
*Check counter datasheet for value to use*

    COUNTER1_DIV=1
    COUNTER2_DIV=1
    COUNTER3_DIV=1
    COUNTER4_DIV=1
    COUNTER5_DIV=1
    COUNTER6_DIV=1
    COUNTER7_DIV=1
    COUNTER8_DIV=1

List of state files for virtual counters  
*Leave empty if not used*

    COUNTER1_SFILE= ;"/tmp/status.x_y"
    COUNTER2_SFILE=
    COUNTER3_SFILE=
    COUNTER4_SFILE=
    COUNTER5_SFILE=
    COUNTER6_SFILE=
    COUNTER7_SFILE=
    COUNTER8_SFILE=


### Parameters for the Status module

Section name is `[statusd]`

Enable this module (true or false)

    STATUSD_ENABLED=false

List of Kernel Ids of the GPIO pin pairs or single pins we want to watch  
*Set to "0" if unused or leave blank.*

    INPUT1_1_ID=24    ; Status 1_1
    INPUT1_2_ID=25    ; Status 1_2
    INPUT1_LOGIC="-n" ; Status 1 logic

    INPUT2_1_ID=      ; Status 2_1
    INPUT2_2_ID=      ; Status 2_2
    INPUT2_LOGIC=     ; Status 2 logic

    INPUT3_1_ID=      ; Status 3_1
    INPUT3_2_ID=      ; Status 3_2
    INPUT3_LOGIC=     ; Status 3 logic

    INPUT4_1_ID=      ; Status 4_1
    INPUT4_2_ID=      ; Status 4_2
    INPUT4_LOGIC=     ; Status 4 logic

    INPUT5_1_ID=      ; Status 5_1
    INPUT5_2_ID=      ; Status 5_2
    INPUT5_LOGIC=     ; Status 5 logic

    INPUT6_1_ID=      ; Status 6_1
    INPUT6_2_ID=      ; Status 6_2
    INPUT6_LOGIC=     ; Status 6 logic

    INPUT7_1_ID=      ; Status 7_1
    INPUT7_2_ID=      ; Status 7_2
    INPUT7_LOGIC=     ; Status 7 logic

    INPUT8_1_ID=      ; Status 8_1
    INPUT8_2_ID=      ; Status 8_2
    INPUT8_LOGIC=     ; Status 8 logic


### Parameters for the ModbusRTU module

Section name is `[mbrtud]`

Enable this module (true or false)

    MBRTUD_ENABLED=false

Modbus device communication settings

    SLAVE_ADDR=1
    REGADDR_OFFSET=40000 ; Needed for NIBE Modbus40 module
    SERIAL_DEV=USB0      ; Using USB to RS485 converter
    BAUDRATE=9600        ; Serial communication speed
