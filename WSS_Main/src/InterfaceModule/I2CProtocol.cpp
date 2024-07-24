/*
 * I2CProtocol.cpp
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#include "InterfaceModule/I2CProtocol.h"

/***************************** Include Files *********************************/
#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <unistd.h>             // Close() system call
#include <sys/ioctl.h>
#include <stdio.h>
#include <linux/i2c-dev.h>
#include <iostream>

/************************** Constant Definitions *****************************/

#define I2C_SLAVE_FORCE 0x0706
#define I2C_SLAVE    0x0703    /* Change slave address            */
#define I2C_FUNCS    0x0705    /* Get the adapter functionality */
#define I2C_RDWR    0x0707    /* Combined R/W transfer (one stop only)*/

#define MUX_ADDR            0x74
#define MUX_CHANNEL_KINTEX7        0x04

/*
 * Page size of the EEPROM. This depends on the type of the EEPROM available
 * on board.
 */
#define PAGESIZE 128


/***************** Macros (Inline Functions) Definitions *********************/


/************************** Variable Definitions *****************************/

/*
 * FD of the IIC device opened.
 */
int Fdiic;

I2CProtocol::I2CProtocol(Xuint8 m_slaveDeviceAddr) {

	slaveDeviceAddr = m_slaveDeviceAddr;

	if(slaveDeviceAddr == 0)
	{
		printf("Driver<I2CProtocol>: Slave Device Address Not Found.\n");
	}

	/*
	 * Open the device.
	 */

	Fdiic = open("/dev/i2c-0", O_RDWR);

	if(Fdiic < 0)
	{
		printf("Cannot open the IIC device\n");
	}

}

I2CProtocol::~I2CProtocol() {

	if(Fdiic >0)
	{
		close(Fdiic);
	}

}

/*****************************************************************************/
/**
*
* Read the data from the EEPROM.
*
* @param    TestLoops - indicates the number of time to execute the test.
*
* @return   Status of the read from EEPROM
*
* @note     None.
*
******************************************************************************/
int I2CProtocol::IicRead(Xuint16 offset_addr, Xuint8 *Buf, Xuint8 Len)
{
    Xuint8 WriteBuffer[2];    /* Buffer to hold location address.*/
    Xuint8 BytesWritten;
    Xuint16 BytesToRead= Len;
    Xuint8 BytesRead=0;            /* Number of Bytes read from the IIC device. */
    Xuint8 ReadBytes;
    int Status = 0;

    Status = ioctl(Fdiic, I2C_SLAVE_FORCE, slaveDeviceAddr);

    if(Status < 0)
    {
        printf("\n Unable to set the EEPROM address\n");

        return -1;
    }

    if(sizeof(offset_addr) == 1)
    {
        WriteBuffer[0] = (Xuint8)(offset_addr);
    }
    else
    {
        WriteBuffer[0] = (Xuint8)(offset_addr >> 8);
        WriteBuffer[1] = (Xuint8)(offset_addr);
    }


    /*
     * Position the address pointer in EEPROM.
     */
    BytesWritten = write(Fdiic, WriteBuffer, sizeof(offset_addr));

    if(BytesToRead > PAGESIZE)
	{
	  ReadBytes = PAGESIZE;
	}
	else
	{
	  ReadBytes = BytesToRead;
	}


    /*
     * Read the bytes.
     */
    while(BytesToRead > 0)
    {

        BytesRead = read(Fdiic, Buf, ReadBytes);

        if(BytesRead !=  ReadBytes)
        {
            printf("\nITP_IIC_TEST1: Test Failed in read\n");
            return -1;
        }

        /*
         * Update the read counter.
         */
        BytesToRead -= BytesRead;
        if(BytesToRead > PAGESIZE)
        {
            ReadBytes = PAGESIZE;
        }
        else
        {
            ReadBytes = BytesToRead;
        }

    }

    return 0;
}

/*****************************************************************************/
/**
*
* Write data to the EEPROM.
*
* @param    TestLoops - indicates the number of time to execute the test.
*
* @return   None.
*
* @note     None.
*
******************************************************************************/
int I2CProtocol::IicWrite(Xuint16 offset_addr, Xuint8 *Buf, Xuint8 Len)
{
    Xuint8 WriteBuffer[PAGESIZE + sizeof(offset_addr)]; /* Buffer to hold data to be written */
    Xuint16 BytesToWrite = Len;
    Xuint8 BytesWritten;    /* Number of Bytes written to the IIC device. */
    Xuint16 Index;                /* Loop variable. */
    Xuint8 WriteBytes;
    int Status = 0;

    Status = ioctl(Fdiic, I2C_SLAVE_FORCE, slaveDeviceAddr);
    if(Status < 0)
    {
        printf("\n Unable to set the EEPROM address\n");

        return -1;
    }

    /*
     * Load the offset address inside EEPROM where data need to be written.
     */
    if(sizeof(offset_addr) == 1)
    {
        WriteBuffer[0] = (Xuint8)(offset_addr);
    }
    else
    {
        WriteBuffer[0] = (Xuint8)(offset_addr >> 8);
        WriteBuffer[1] = (Xuint8)(offset_addr);
    }

    /*
     * Load the data to be written into the buffer.
     */
    for(Index = 0; Index < BytesToWrite; Index++)
    {
        WriteBuffer[Index + sizeof(offset_addr)] = *Buf;
        Buf++;
    }

    /*
     * Limit the number of bytes to the page size.
     */
    if(BytesToWrite > PAGESIZE)
    {
        WriteBytes = PAGESIZE + sizeof(offset_addr);
    }
    else
    {
        WriteBytes = BytesToWrite + sizeof(offset_addr);
    }


    while(WriteBytes > 0)
    {
        /*
         * Write the data.
         */
        BytesWritten = write(Fdiic, WriteBuffer, WriteBytes);

        /*
         * Wait till the EEPROM internally completes the write cycle.
         */
        sleep(1);

        if(BytesWritten != WriteBytes)
        {
            printf("\nTest Failed in write\n");
            return -1;
        }

        /*
         * Update the write counter.
         */
        WriteBytes -= BytesWritten;

        // Update offset address and try write again until BytesWritten == WriteBytes
        offset_addr += BytesWritten;

        WriteBuffer[0] = (Xuint8)(offset_addr >> 8);			// Move pointer in eeprom to next bytes to write
        WriteBuffer[1] = (Xuint8)(offset_addr);
    }

    return 0;
}
