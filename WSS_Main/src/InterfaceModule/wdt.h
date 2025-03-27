/*
 * wdt.h
 *
 *  Created on: Feb 10, 2025
 *      Author: Administrator
 */

#ifndef SRC_INTERFACEMODULE_WDT_H_
#define SRC_INTERFACEMODULE_WDT_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/signal.h>


// Watchdog definitions
#define WATCHDOG_IOCTL_BASE 'W'

struct watchdog_info {
    unsigned int options;          /* Options the card/driver supports */
    unsigned int firmware_version; /* Firmware version of the card */
    char identity[32];             /* Identity of the board */
};

#define WDIOC_GETSUPPORT        _IOR(WATCHDOG_IOCTL_BASE, 0, struct watchdog_info)
#define WDIOC_GETSTATUS         _IOR(WATCHDOG_IOCTL_BASE, 1, int)
#define WDIOC_GETBOOTSTATUS     _IOR(WATCHDOG_IOCTL_BASE, 2, int)
#define WDIOC_GETTEMP           _IOR(WATCHDOG_IOCTL_BASE, 3, int)
#define WDIOC_SETOPTIONS        _IOR(WATCHDOG_IOCTL_BASE, 4, int)
#define WDIOC_KEEPALIVE         _IOR(WATCHDOG_IOCTL_BASE, 5, int)
#define WDIOC_SETTIMEOUT        _IOWR(WATCHDOG_IOCTL_BASE, 6, int)
#define WDIOC_GETTIMEOUT        _IOR(WATCHDOG_IOCTL_BASE, 7, int)
#define WDIOC_GETTIMELEFT       _IOR(WATCHDOG_IOCTL_BASE, 10, int)

#define WDIOF_MAGICCLOSE        0x0100  /* Supports magic close char */
#define WDIOS_DISABLECARD       0x0001  /* Turn off the watchdog timer */


int watchdog_init(const char *device, int timeout);
int watchdog_feed();
int watchdog_disable();
void signal_handler(int sig);



#endif /* SRC_INTERFACEMODULE_WDT_H_ */
