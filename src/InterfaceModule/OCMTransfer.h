/*
 * OCMTransfer.h
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#ifndef SRC_INTERFACEMODULE_OCMTRANSFER_H_
#define SRC_INTERFACEMODULE_OCMTRANSFER_H_

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fstream>
#include <sys/time.h>
#include "InterfaceModule/MemoryMapping.h"

// Interval Vsync properties
#define VSyncTimerHH 0xA5
#define VSyncTimerH 0xA6
#define VSyncTimerL 0xA7
#define VSyncTimerLL 0xA8
// this is the base oscillator that we are dividing with the VsyncTimer registers
#define HEC7020_PLL_BASE	(150e6)

/*
 * Definition related to OCM memory use
 */
#define HEC7020_OCM_MAP_SIZE	0x1000
#define HEC7020_OCM_TRANS_SIZE 	256*1024
#define HEC7020_OCM_LOC 		0xFFFC0000			//0xe0880000//
#define HEC7020_OCM_MEM_BEGIN 	0x00
#define HEC7020_OCM_MEM_END 	0x20000
#define HEC7020_OCM_MEM_STEP 	1024
#define	HEC7020_OCM_CTRL		0x00
#define HEC7020_OCM_READPTR		0x01		// Read address of the FPGA DMA core. Assumes that the offset is to a variable defined as ocmref_t (below)
#define HEC7020_OCM_WRITEPTR	0x02		// Write address of the Linux software

class OCMTransfer {
public:
					OCMTransfer();
	virtual 		~OCMTransfer();

	int 			SetOutputToOCM(void);		// Done by me to use holoeye logic
	int 			SendPatternData(uint8_t *pattern);

	int 			EnsureVideoInputSourceIsEmbeddedLinux();

private:

	MemoryMapping*	mmapReg2{nullptr};
	MemoryMapping*	mmapOCM{nullptr};
	MemoryMapping*	ocmMem{nullptr};
	void*			ocm{nullptr};

private:

	//int EnsureVideoInputSourceIsEmbeddedLinux();

	// Interval Vsync Function
	int 			GetInternalVsyncFrequency (double *vsyncFrequencyHz);
	int 			SetInternalVsyncFrequency(double vsyncFrequencyHz);

	int 			GetRegister(int addr, unsigned char *vsyncProps);
	int 			VsyncSelectExt();
	int 			VsyncSelectInt();

	int 			SetARMInputActive(unsigned char value);
	int 			SetHDMISource(int hdmi);

	int 			GetARMInputActive(unsigned char *value);

	void 			MemcpyLoop(uint8_t* dest, const uint8_t* src, size_t n);	// test holoeye function made by nasir

	void 			TestOCMReadWrite(void);
};

#endif /* SRC_INTERFACEMODULE_OCMTRANSFER_H_ */
