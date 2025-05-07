/*
 * wdt.cpp
 *
 *  Created on: Feb 10, 2025
 *      Author: Administrator
 */
#ifndef _WATCHDOG_RESET_

#include "wdt.h"
#include <cstdio>
#include <cstdlib>

volatile uint32_t* WATCHDOG_RESET_STATUS_REG = nullptr;
//volatile uint32_t* WATCHDOG_CONTROL_REG = nullptr;
//const char* WATCHDOG_FLAG_PATH = "/mnt/watchdog_reboot.flag";
// Global variables
static int wdt_fd = -1; // Watchdog file descriptor
static int watchdog_timeout = 1; // Default timeout in seconds

FaultsAttr m_watchDogEvent;

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

void boot_check() {
    uint32_t reg_value = *WATCHDOG_RESET_STATUS_REG; 
    printf("[Boot Check] WatchDog Register (0xF8F00630) current value: 0x%08X\n", reg_value);
    if (reg_value & 0x1) {
        std::lock_guard<std::mutex> lock(m_watchDogEvent.mtx);
        m_watchDogEvent.Raised = true;
        m_watchDogEvent.RaisedCount += 1;
        m_watchDogEvent.Degraded = true;
        m_watchDogEvent.DegradedCount += 1;
        FaultMonitor::logFault(WATCH_DOG_EVENT, m_watchDogEvent);
        *WATCHDOG_RESET_STATUS_REG = reg_value | 0x1;  // write 1 to bit 0 to clear flag
    }
}

void init_watchdog_registers() {
    const off_t REG_PHYS_ADDR = 0xF8F00630; 
    const size_t MAP_SIZE = sysconf(_SC_PAGESIZE); 
    
    int mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) {
        perror("open(/dev/mem) failed");
        exit(EXIT_FAILURE);
    }

    off_t page_base = REG_PHYS_ADDR & ~(MAP_SIZE - 1);
    off_t page_offset = REG_PHYS_ADDR - page_base;
    
    void* mapped_base = mmap(
        NULL, 
        MAP_SIZE, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED, 
        mem_fd, 
        page_base
    );
    if (mapped_base == MAP_FAILED) {
        perror("mmap() failed");
        close(mem_fd);
        exit(EXIT_FAILURE);
    }
    
    WATCHDOG_RESET_STATUS_REG = (volatile uint32_t*)((char*)mapped_base + page_offset);
    close(mem_fd); 
}

#endif