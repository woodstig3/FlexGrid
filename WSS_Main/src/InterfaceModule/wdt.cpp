/*
 * wdt.cpp
 *
 *  Created on: Feb 10, 2025
 *      Author: Administrator
 */
#ifndef _WATCHDOG_RESET_

#include "wdt.h"

// Global variables
static int wdt_fd = -1; // Watchdog file descriptor
static int watchdog_timeout = 1; // Default timeout in seconds

// Function to initialize the watchdog
int watchdog_init(const char *device, int timeout) {
    int ret;

    // Open the watchdog device
    wdt_fd = open(device, O_RDWR);
    if (wdt_fd < 0) {
        perror("Failed to open watchdog device");
        return -1;
    }

    // Set the watchdog timeout
    ret = ioctl(wdt_fd, WDIOC_SETTIMEOUT, &timeout);
    if (ret < 0) {
        perror("Failed to set watchdog timeout");
        close(wdt_fd);
        return -1;
    }

    watchdog_timeout = timeout;
    printf("Watchdog initialized with timeout: %d seconds\n", watchdog_timeout);

    return 0;
}

// Function to feed the watchdog
int watchdog_feed() {
    if (wdt_fd < 0) {
        fprintf(stderr, "Watchdog not initialized\n");
        return -1;
    }

    int ret = ioctl(wdt_fd, WDIOC_KEEPALIVE, NULL);
    if (ret < 0) {
        perror("Failed to feed watchdog");
        return -1;
    }

//    printf("Watchdog fed\n");
    return 0;
}

// Function to disable the watchdog
int watchdog_disable() {
    if (wdt_fd < 0) {
        fprintf(stderr, "Watchdog not initialized\n");
        return -1;
    }

    int val = WDIOS_DISABLECARD;
    int ret = ioctl(wdt_fd, WDIOC_SETOPTIONS, &val);
    if (ret < 0) {
        perror("Failed to disable watchdog");
        return -1;
    }

    printf("Watchdog disabled\n");
    close(wdt_fd);
    wdt_fd = -1;
    return 0;
}

// Signal handler for graceful shutdown
void signal_handler(int sig) {
    printf("\nReceived signal %d, disabling watchdog...\n", sig);
    watchdog_disable();
    exit(0);
}

#endif

