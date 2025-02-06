/*
 * QSPI.h
 *
 *  Created on: Sep 9, 2024
 *      Author: Administrator
 */

#ifndef SRC_INTERFACEMODULE_QSPI_H_
#define SRC_INTERFACEMODULE_QSPI_H_

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>


#define MTD_DEVICE_ACTIVEVER  	"/dev/mtd04" 		// Adjust accordingly for your MTD device
#define MTD_DEVICE_BACKUPVER  	"/dev/mtd05" 		// Adjust accordingly for your MTD device
#define MTD_DEVICE_DATA  		"/dev/mtd06"		// Adjust accordingly for your MTD device


#define BASE_MEM_ADDR			0x00000000
#define SECTOR_ACTIVEVER_ADDR	0x00000000
#define SECTOR_BACKUPVER_ADDR	0x00000000
#define SECTOR_DATA_ADDR		0x00000000

#define MTD_DEVICE_VER_SIZE  	0x200000
#define MTD_DEVICE_DATA_SIZE    0x10000

/**************************** Type Definitions *******************************/

typedef unsigned char   Xuint8;
typedef char            Xint8;      /**< signed 8-bit */
typedef unsigned short  Xuint16;    /**< unsigned 16-bit */
typedef short           Xint16;     /**< signed 16-bit */
typedef unsigned int    Xuint32;	/**< signed 16-bit */

using namespace std;

class QSPIProtocol{
public:
	QSPIProtocol(const char* mtd_device);
	virtual 	~QSPIProtocol();

	/************************** Function Prototypes ******************************/

	Xuint32 FlashErase(void);

	Xuint32 FlashWrite(void);

	Xuint32 FlashRead(void);


private:

	Xint8 m_mtd_device[11];

	// Buffer for read/write operations
	int sector_size; // Example sector size
	int sector_start_address;

	Xint8* write_buf;
	Xint8* read_buf;

//	Xuint32 QSPIInitialization(Xuint8 m_QspiDeviceId);

};




#endif /* SRC_INTERFACEMODULE_QSPI_H_ */
