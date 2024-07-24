/*
 * I2CProtocol.h
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#ifndef SRC_INTERFACEMODULE_I2CPROTOCOL_H_
#define SRC_INTERFACEMODULE_I2CPROTOCOL_H_

/**************************** Type Definitions *******************************/

typedef unsigned char   Xuint8;
typedef char            Xint8;      /**< signed 8-bit */
typedef unsigned short  Xuint16;    /**< unsigned 16-bit */
typedef short           Xint16;     /**< signed 16-bit */
typedef unsigned int    Xuint32;

class I2CProtocol {
public:
					I2CProtocol(Xuint8 m_slaveDeviceAddr = 0);
	virtual 		~I2CProtocol();

	Xuint8 			slaveDeviceAddr = 0;


	/************************** Function Prototypes ******************************/

	int 			IicRead(Xuint16 offset_addr, Xuint8 *Buf, Xuint8 Len);
	int 			IicWrite(Xuint16 offset_addr, Xuint8 *Buf, Xuint8 Len);
};

#endif /* SRC_INTERFACEMODULE_I2CPROTOCOL_H_ */
