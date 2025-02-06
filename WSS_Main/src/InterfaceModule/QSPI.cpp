/*
 * QSPI.cpp
 *
 *  Created on: Sep 9, 2024
 *      Author: Administrator
 */
#include <cstring>
#include <mtd/mtd-user.h>
#include <mtd/mtd-abi.h>
#include <sys/ioctl.h>
#include "QSPI.h"


QSPIProtocol::QSPIProtocol(const char* mtd_device)
{
    if (strcmp(mtd_device, MTD_DEVICE_ACTIVEVER) == 0)
    {
    	sector_size = MTD_DEVICE_VER_SIZE;
        strcpy(m_mtd_device, MTD_DEVICE_ACTIVEVER);
    }
    else if(strcmp(mtd_device, MTD_DEVICE_BACKUPVER) == 0)
    {
    	sector_size = MTD_DEVICE_VER_SIZE;
    	strcpy(m_mtd_device, MTD_DEVICE_BACKUPVER);
    }

    else if(strcmp(mtd_device, MTD_DEVICE_DATA) == 0)
    {
    	strcpy(m_mtd_device, MTD_DEVICE_DATA);
    	sector_size = MTD_DEVICE_DATA_SIZE;
    }
    else
    {
    	strcpy(m_mtd_device, "");
    	sector_size = 0;
    }

}

QSPIProtocol::~QSPIProtocol()
{

}

Xuint32 QSPIProtocol::FlashErase()
{
// Open the MTD device
	int mtd_fd = open(m_mtd_device, O_RDWR);
	if (mtd_fd < 0)
	{
		perror("Failed to open MTD device");
		return 1;
	}

	// Assuming mtd_fd is opened and refers to the device.
	// Using ioctl to erase the entire firmware region; adjust as necessary.
	struct erase_info_user erase_info;
	ioctl(mtd_fd, MEMGETINFO, &erase_info);   // get the device info

//	erase_info.length = sector_size; // Size of firmware to erase
//	erase_info.start = sector_start_address; // Start address

	if (ioctl(mtd_fd, MEMERASE, &erase_info) != 0)
	{
		perror("Failed to erase MTD device");
		close(mtd_fd);
		return 1;
	}
 // Close the MTD device
	close(mtd_fd);
	return 0;
}

Xuint32 QSPIProtocol::FlashWrite()
{
// Open the MTD device
	int mtd_fd = open(m_mtd_device, O_RDWR);
	if (mtd_fd < 0)
	{
		perror("Failed to open MTD device");
		return 1;
	}
	/*
	 * Initialize the write buffer for a pattern to write to the FLASH
	 * and the read buffer to zero so it can be verified after the read,
	 * the test value that is added to the unique value allows the value
	 * to be changed in a debug environment to guarantee
	 */
	// Prepare data to write
	snprintf(write_buf, sector_size, "Hello, flash memory!");

	// Write data to MTD device
	if (write(mtd_fd, write_buf, sector_size) != sector_size)
	{
		perror("Failed to write to MTD device");
	    close(mtd_fd);
	    return 1;
	}
	printf("Data written to MTD device.\n");
 // Close the MTD device
	close(mtd_fd);
	return 0;
}

Xuint32 QSPIProtocol::FlashRead()
{
// Open the MTD device
	int mtd_fd = open(m_mtd_device, O_RDWR);
	if (mtd_fd < 0)
	{
		perror("Failed to open MTD device");
		return 1;
	}
	// Read data from MTD device
	if (read(mtd_fd, read_buf, sector_size) != sector_size)
	{
	    perror("Failed to read from MTD device");
	    close(mtd_fd);
	    return 1;
	}

	printf("Data read from MTD device: %s\n", read_buf);

 // Close the MTD device
	close(mtd_fd);
	return 0;
}


//QSPIProtocol::QSPIInitialization(Xuint8 m_QspiDeviceId)
//{
//	int Status;

//}








