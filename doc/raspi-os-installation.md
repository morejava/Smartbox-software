# Raspberry Pi OS installation for TeleGea

## Base system

### Install a minimal Raspbian base system

The standard Raspbian distribution is a complete operating system which comes with a lot of software preinstalled that is not needed for TeleGea. Therefore we start from a minimal Raspbian base system. There is a choice between these distros:  
- Minibian distribution

    https://minibianpi.wordpress.com
     
- Raspbian unattended netinstaller

    https://github.com/debian-pi/raspbian-ua-netinst

An 8GB SD card or flash memory is considered for installation but also 4GB should work fine. The installation process for the OS will create a small boot partition and a second partition for the root file system. The size of this partition should be chosen before the installation to be around 2GB. The remaining space will be used to create the data partition.  

Go through the installation of the base system until you boot into the freshly installed system.  
    
### Create data partition and folders

The data partition will be created from the unused space on the SD card. Run the partitioning tool and follow the instructions:  

    cfdisk /dev/mmcblk0

Then tell the kernel we have a new partition table:

    partprobe /dev/mmcblk0

Format the new partition:

    mkfs.ext4 /dev/mmcblk0p3
    
Create the data folder to mount the new partition to:

    mkdir /media/data  

Create other folders which will be needed:

    mkdir /geofox_data
    mkdir /sensors

Create symlinks:

    rm /etc/motd; ln -s /var/run/motd /etc/motd
    ln -s /tmp/geofox.conf /etc/geofox.conf


### Upgrade to Debian jessie release [optional]

Current Raspbian distribution is shipped with Debian wheezy which is the stable release. However, to use more up to date software packages it is recommended to upgrade to Debian jessie (current testing release).

Change  `wheezy` to `jessie` in the following line of file `/etc/apt/sources.list`:

    deb http://mirrordirector.raspbian.org/raspbian wheezy main non-free

Then run the upgrade procedure:

    apt-get update
    # install sysv init system to avoid installation of systemd
    apt-get install sysvinit-core
    apt-get upgrade


### Upgrade Kernel [optional]

The Linux Kernel release 3.18.8 or later (with fbtft driver) is needed for using the RPi display. Therefore, if a kernel (firmware) update is needed, it can be performed as follows:

    apt-get install rpi-update
    rpi-update

    
### Modify kernel configuration

Assuming that a Kernel 3.18.x (or later) is installed, device tree overlays are used to load the needed kernel drivers at startup. We need the `w1-gpio` related drivers to enable 1-wire support and `rpi-display` related drivers to enable support for the Watterott RPi display. The latter is used for the thermostat application GUI only. Please note that, when enabling this overlay, almost all GPIO pins will be used for the display and are not available to other applications any more.  

Add the overlays and parameters for 1-wire bus and, only if needed, RPI display to /boot/config.txt:

     dtoverlay=w1-gpio,gpiopin=4
     dtoverlay=rpi-display,speed=32000000,rotate=270

     
### Install additional packages

Now the needed packages which are missing from the base system will be installed.
- build support: gcc make pkg-config git

    `apt-get install gcc make pkg-config git`
    
- libs: libmodbus-dev libmodbus5
    
    `apt-get install libmodbus-dev libmodbus5`
    
- system: resolvconf monit ntpdate sudo avahi-daemon ifplugd

    `apt-get install resolvconf monit ntpdate sudo avahi-daemon ifplugd`

- tools: aptitude bc htop wpasupplicant wireless-tools usbutils curl

    `apt-get install aptitude bc htop wpasupplicant wireless-tools usbutils curl`


### Install wiringPi from source

The wiringPi tools and library are needed by the TeleGea software. Since there is no package ready to install, the installation has to be performed from the source code following these steps:

    git clone git://git.drogon.net/wiringPi
    cd wiringPi
    ./build


### Set hostname and root password

In order to modify the systems default hostname, modify the `/etc/hostname` file with the TeleGea default hostname:

    echo geopi > /etc/hostname

Set the new default password and reboot to apply the changes:

    passwd

    
### Generate SSH keys for server login (optional)

For remote access to the device, it needs to connect automatically to the TeleGea server without user interaction. The authentication will therefore be performed with an ssh key exchange. So a public/private key pair has to be generated and the public key copied to the server.

First generate the keys (hit Enter when asked):

    ssh-keygen -t rsa
    
Then copy the public key to the server using scp:

    cat .ssh/id_rsa.pub | ssh -p<srv_port> <user>@<srv_ip> 'cat >> .ssh/authorized_keys'

Now the login can be performed without password request:

    ssh -p<srv_port> <user>@<srv_ip>
    

### Set time zone

The default timezone for the Raspbian installation is GMT. So if you are not deploying the device in Greenwich, UK, you need to change the timezone.  
To do this, execute the following command:

    dpkg-reconfigure tzdata
    
and follow the instructions for selecting your local region.


## Custom software 

Currently there is no build machine with a cross compilation environment for the custom TeleGea software. Therefore no pre-built binary packages are available and the software has to be installed from source. The source code for the device software is available from the TeleGea Github repository.

    https://github.com/Telegea/Smartbox-software.git
    
To install the software on the Raspberry Pi the repository needs to be cloned to your PC.

    git clone https://github.com/Telegea/Smartbox-software.git

Then you can use the scp tool (or any graphical GUI) to copy the necessary files to the device, as described in the following chapters.


### Install custom file system modifications

The standard root file system needs to be customised with some files (scripts and configuration files) for the TeleGea system. To do this, the fsmods folder will be copied onto the target.

    scp -r device/fsmods/etc/* root@<device-ip-addr>:/etc

The global device configuration is done in the file /etc/telegea.conf. In this file, the single modules can be enabled/disabled and the module specific default parameters can be modified. All Telegea modules are disabled by default. Reboot the system to make the modifications become active.  


### Install custom platform service modules

First the custom libraries need to be installed on the device. To do this, the library source code folder will be copied onto the target.

    scp -r device/src/libraries root@<device-ip-addr>:/root/
    
On the target, cd into each library directory and build and install the binaries  
[*TODO: create a top level makefile to do this in one go*].

    cd dhtlib/
    make
    make install
    
    cd ..
    
    cd shtlib/
    make
    make install
    
    cd ..
    
    cd modbus_tcp_server_lib/
    make 
    make install

Now the custom modules can be installed. To do this, the modules source code folder will be copied onto the target.

    scp -r device/src/modules root@<device-ip-addr>:/root/

On the target, cd into the modules directory, build and install the binaries.

    cd modules
    make
    make install

Each module is implemented as a system daemon and comes with an init script which is located in the modules `init.d/` subdir. These init scripts need to be copied to the systems init directory `/etc/init.d/`.  
[*TODO: do this automatically during "make install"*]  


### Install custom helper script modules

To install the custom script modules, the related files will be copied from the script folder to the devices `/usr/local/bin/` folder.  

    scp device/script/* root@<device-ip-addr>:/usr/local/bin/

    
## Thermostat and Thermostat GUI

The Thermostat application is an autonomous module and is made of the thermostat algorithm (bash and python scripts) and the GUI (web browser and web server with php scripts). The algorithm can run without the GUI and therefore can be installed also without a local touch screen.


### Install Thermostat module

To install the thermostat module copy the related scripts to the device:

    scp device/src/thermostat/thermostat.sh root@<device-ip-addr>:/usr/local/bin/
    scp device/src/thermostat/get_target_temp.py root@<device-ip-addr>:/usr/local/bin/
    scp device/src/thermostat/init.d/thermostat root@<device-ip-addr>:/etc/init.d
    
Install Python support on the device:

    apt-get install python
    

### Install Thermostat GUI (touchscreen)

The thermostat touch screen GUI is displayed as a Web page on a local Web browser in full screen mode. Therefore a web server, the web browser and some dependencies need to be installed.

Copy the Web page files to the Web server document root on the device:

    scp device/src/thermostat/Gui/* root@<device-ip-addr>:/var/www/therm/

Install daemon and start scripts:

    scp device/src/thermostat/Gui/themostat-gui.sh root@<device-ip-addr>:/usr/local/bin/
    scp device/src/thermostat/init.d/thermostat_gui root@<device-ip-addr>:/etc/init.d

Install lighttpd HTTP server with PHP support:

    apt-get install lighttpd php5-common php5-cgi php5

Enable PHP module for lighttpd:

    lighty-enable-mod fastcgi-php

Install X related stuff:

    apt-get install xinit xinput x11-xserver-utils matchbox-window-manager
    
Install needed fonts for browser:

    apt-get install ttf-mscorefonts-installer

Alternatively, in case the needed font package is not available from the Debian repository, it needs to be downloaded and installed manually:

    wget http://ftp.de.debian.org/debian/pool/contrib/m/msttcorefonts/ttf-mscorefonts-installer_3.6_all.deb
    apt-get install cabextract
    dpkg -i ttf-mscorefonts-installer_3.6_all.deb

Install the Midori light weight web browser (Note: this also installs a *lot* of dependencies):

    apt-get install midori
