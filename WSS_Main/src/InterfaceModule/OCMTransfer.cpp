/*
 * OCMTransfer.cpp
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#include "InterfaceModule/OCMTransfer.h"
#include "GlobalVariables.h"

OCMTransfer::OCMTransfer()
{
	mmapReg2 = new MemoryMapping(MemoryMapping::REG);
	mmapOCM  = new MemoryMapping(MemoryMapping::OCMREG);

	// For RING MEMORY MAPPING - 256KB 0xFFFC_0000 - 0xFFFF_FFFF
	ocmMem = new MemoryMapping(MemoryMapping::OCMMEM);
	ocmMem->GetOCMMemoryRegion(&ocm);

	std::cout << "OCM Transfer ocm region = " << ocm << std::endl;

	SetOutputToOCM();		// Switch output to OCM

#ifdef _DEVELOPMENT_MODE_
	//TestOCMReadWrite();
#endif

}

OCMTransfer::~OCMTransfer()
{
	if(mmapOCM)
	{
		delete mmapOCM;
		mmapOCM = nullptr;
	}

	if(mmapReg2)
	{
		delete mmapReg2;
		mmapReg2 = nullptr;
	}

	if(ocmMem)
	{
		delete ocmMem;
		ocmMem = nullptr;
	}
}

int OCMTransfer::SendPatternData(uint8_t *pattern)
{	std::cout << "SendPatternData..." << ocm << std::endl;

	int pixel_count;
	int required_size = g_LCOS_Height*g_LCOS_Width; //1952*1080;
	int RP=0;
	int WP=0;
	int j;


	for (j=0; j<1; ++j)
	{
		pixel_count = 0;
		MemcpyLoop((uint8_t*)ocm+HEC7020_OCM_TRANS_SIZE/2, pattern,HEC7020_OCM_TRANS_SIZE/2);

		pixel_count = HEC7020_OCM_TRANS_SIZE/2;
		mmapOCM->WriteRegister_OCM32(HEC7020_OCM_WRITEPTR, 128*1024);
		usleep(100);

		while (pixel_count < required_size){

			mmapOCM->ReadRegister_OCM32(HEC7020_OCM_READPTR, &RP);
			//std::cout << "RP = " << RP << std::endl;
			//usleep(500);<-STABLE DELAY, but 100ms more time			// THIS DELAY IS MUST! Or RP value will not update
			mmapOCM->ReadRegister_OCM32(HEC7020_OCM_WRITEPTR, &WP);
			//usleep(500);<-STABLE DELAY, but 100ms more time			// THIS DELAY IS MUST! Or RP value will not update

			if (WP==HEC7020_OCM_MEM_END){
				if (RP!=0){
					WP=HEC7020_OCM_MEM_BEGIN;
				}
			}
			else if ( RP<=WP ){
				//check if the remaining image data size to copy is greater than or equal to the remaining memory space until the end of the OCM.
				if ((required_size-pixel_count)>=(HEC7020_OCM_TRANS_SIZE/2-WP)){
					//copies the remaining image data to the OCM and sets the write pointer to HEC7020_OCM_MEM_END,
					MemcpyLoop((uint8_t*)ocm+HEC7020_OCM_TRANS_SIZE/2+WP, pattern+pixel_count,HEC7020_OCM_TRANS_SIZE/2-WP);
					pixel_count+=HEC7020_OCM_TRANS_SIZE/2-WP;
					WP=HEC7020_OCM_MEM_END;
				}
				else{	// Case when remaining image data is less than 1024 steps or simply less than OCM size
					//copies the remaining image data to the OCM and sets the write pointer to the new position.
					MemcpyLoop((uint8_t*)ocm+HEC7020_OCM_TRANS_SIZE/2+WP, pattern+pixel_count, required_size-pixel_count);
					WP=WP + required_size - pixel_count;
					pixel_count = required_size;
				}
			}
			else if ((RP-WP)>HEC7020_OCM_MEM_STEP){
				//copies HEC7020_OCM_MEM_STEP bytes from the image to the OCM and increment the write pointer by HEC7020_OCM_MEM_STEP.
				MemcpyLoop((uint8_t*)ocm+HEC7020_OCM_TRANS_SIZE/2+WP, pattern+pixel_count,HEC7020_OCM_MEM_STEP);
				pixel_count+=HEC7020_OCM_MEM_STEP;
				WP=WP+HEC7020_OCM_MEM_STEP;
			}

			mmapOCM->WriteRegister_OCM32(HEC7020_OCM_WRITEPTR, WP);//usleep(300);	<-STABLE DELAY, but 100ms more time
		}

		do
		{
			mmapOCM->ReadRegister_OCM32(HEC7020_OCM_READPTR, &RP); usleep(10); //usleep(1000); <-STABLE DELAY, but 100ms more time
		}while(RP != WP);

		mmapOCM->WriteRegister_OCM32(HEC7020_OCM_WRITEPTR, 0);
	}

	mmapOCM->WriteRegister_OCM32(HEC7020_OCM_WRITEPTR, 0);

#ifdef _DEVELOPMENT_MODE_
	printf("Pattern send finished....\n");
#endif

//	clock_t tstart = clock();
//	std::cout << "Last clock = " << tstart << "  CLOCKS_PER_SEC  "<< CLOCKS_PER_SEC << std::endl;

	//usleep(500);<-STABLE DELAY, but 100ms more time

	return 0;
}

int OCMTransfer::SetOutputToOCM(void)
{
	int status;

	//printf("Perform axis_switch to 8bit data  bus\n\r");

	//Change axis_switch_0 to 8bit data bus. OCM Module Reg6[0] = 1
	status = mmapOCM->WriteRegister_OCM(0x18, 0x1);usleep(200);

	status |= EnsureVideoInputSourceIsEmbeddedLinux();

	return status;
}

int OCMTransfer::EnsureVideoInputSourceIsEmbeddedLinux()
{
	// first, we need to switch off HDMI and set VSync Frequency:
	// We first test if everything is already set because when setting the values there needs to be at least one frame of pause.

	int status = 0;
	int adjust_values = 0;
	int reason_to_switch = 0; // 0: no reason.

	double internalVSyncFreq = 0.0;
	status = GetInternalVsyncFrequency(&internalVSyncFreq);
	//printf("*********InternalVsyncFrequency = %f Hz\n\r", internalVSyncFreq);

	if ( (status != 0) || (internalVSyncFreq != 60.0))
	{
		adjust_values = 1;
		reason_to_switch = 1;
	}


	unsigned char vsyncProps = 0;
	status = GetRegister(0x17,&vsyncProps);
	//printf("*********VsyncProperties &vsyncProps = %04x\n\r", vsyncProps);

	if ( (status != 0) || (vsyncProps != 0x08))
	{
		adjust_values = 1;
		reason_to_switch = 2;
	}


	unsigned char interface = 0;
	status = GetARMInputActive(&interface);
	//printf("*********getARMInputActive = %d\n\r", interface);

	if ( (status != 0) || (interface == 0))
	{
		adjust_values = 1;
		reason_to_switch = 3;
	}

	if (adjust_values)
	{
		status = SetInternalVsyncFrequency(60);
		status = SetHDMISource(0); // switch to internal source (embedded linux)

		// this seems to be necessary to not destroy the img field if the calculation is faster than 17 ms. This value increased with Pluto-2.1, so I now just increased it to 100ms, since it is only called when switching from other interface to embedded Linux system.
		//HOLOEYE_SLEEP(100);
		usleep(100000);		//100ms

		printf("Switched video input to embedded Linux.");
		if (reason_to_switch == 1)
			printf(" Reason is internal vsync frequency was not 60Hz.");
		else if (reason_to_switch == 2)
			printf(" Reason is internal vsync was not enabled.");
		else if (reason_to_switch == 3)
			printf(" Reason is another video input source was active.");
		printf("\n");
	}

	return status;
}
int OCMTransfer::GetRegister(int addr,unsigned char *vsyncProps)
{
	return (mmapReg2->ReadRegister_Reg2(addr, vsyncProps));usleep(200);
}

int OCMTransfer::GetInternalVsyncFrequency (double *vsyncFrequencyHz)
{
	unsigned char value;
	unsigned long vsyncRatio;
	int status;

	status = mmapReg2->ReadRegister_Reg2(VSyncTimerHH, &value);usleep(200);
	vsyncRatio = value;
	if (!status) status |= mmapReg2->ReadRegister_Reg2(VSyncTimerH, &value);
	vsyncRatio = (vsyncRatio << 8) | value;
	usleep(200);
	if (!status) status |= mmapReg2->ReadRegister_Reg2(VSyncTimerL, &value);
	vsyncRatio = (vsyncRatio << 8) | value;
	usleep(200);
	if (!status) status |= mmapReg2->ReadRegister_Reg2(VSyncTimerLL, &value);
	vsyncRatio = (vsyncRatio << 8) | value;
	usleep(200);

	*vsyncFrequencyHz = HEC7020_PLL_BASE / vsyncRatio;

	//printf("vsyncFrequencyHz = %f \n\r", *vsyncFrequencyHz);

	return status;
}

int OCMTransfer::SetInternalVsyncFrequency(double vsyncFrequencyHz)
{
	unsigned long vsyncRatio = HEC7020_PLL_BASE/vsyncFrequencyHz;
	int error;

	if (HEC7020_PLL_BASE - vsyncRatio * vsyncFrequencyHz  > 32.5 )
		vsyncRatio++;

	//printf("vsyncRatio = %04x\n\r", vsyncRatio);

	uint8_t data = (uint8_t)(vsyncRatio & 0xFF);	// VLL = 0xA8
	error = mmapReg2->WriteRegister_Reg2(VSyncTimerLL, data);
	usleep(200);
	//printf("A8 = %02x\n\r", data);

	vsyncRatio >>= 8;
	data = (uint8_t)(vsyncRatio & 0xFF);			// VL = 0xA7
	if(!error) mmapReg2->WriteRegister_Reg2(VSyncTimerL, data);
	usleep(200);
	//printf("A7 = %02x\n\r", data);

	vsyncRatio >>= 8;
	data = (uint8_t)(vsyncRatio & 0xFF);			// VH = 0xA6
	if(!error) mmapReg2->WriteRegister_Reg2(VSyncTimerH, data);
	usleep(200);
	//printf("A6 = %02x\n\r", data);

	vsyncRatio >>= 8;
	data = (uint8_t)(vsyncRatio & 0xFF);			// VHH = 0xA5
	if(!error) mmapReg2->WriteRegister_Reg2(VSyncTimerHH, data);
	usleep(200);
	//printf("A5 = %02x\n\r", data);

	return error;
}

int OCMTransfer::VsyncSelectExt()
{
	int status;
	uint8_t value;

	status = mmapReg2->ReadRegister_Reg2(0x17, &value);usleep(200);
	//printf("hec7020_vsyncSelectExt = %04x\n\r", value);
	value = 0x0;
	status |= mmapReg2->WriteRegister_Reg2(0x17, value);usleep(200);

	return status;
}

int OCMTransfer::VsyncSelectInt()
{
	int status;
	uint8_t value;

	status = mmapReg2->ReadRegister_Reg2(0x17, &value);usleep(200);
	//printf("hec7020_vsyncSelectExt = %04x\n\r", value);
	value = 0x08;
	status |= mmapReg2->WriteRegister_Reg2(0x17, value);usleep(200);

	return status;
}

int OCMTransfer::SetARMInputActive(unsigned char value)
{
	uint8_t regval;
	mmapReg2->ReadRegister_Reg2(0xB4, &regval);usleep(100);

	if (value == 1)
	{
		uint8_t ocm_val;
		mmapOCM->ReadRegister_OCM(0x0, &ocm_val);usleep(100);
		ocm_val = 0x1;
		mmapOCM->WriteRegister_OCM(0x0, ocm_val);usleep(100);

		ocm_val = 0x3;
		mmapOCM->WriteRegister_OCM(0x0, ocm_val);usleep(100);

		ocm_val = 0x1;
		mmapOCM->WriteRegister_OCM(0x0, ocm_val);usleep(100);

		// switch to Linux:		    		// switch off bit 4 and 5.
		regval = (regval & 0xCF) | 0x10;	// switch on bit 4, i.e. "01" into bits 5:4
	}
	else if (value == 0)
	{
		uint8_t ocm_val;
		mmapOCM->ReadRegister_OCM(0x0, &ocm_val);usleep(100);
		ocm_val = 0x0;
		mmapOCM->WriteRegister_OCM(0x0, ocm_val);usleep(100);

		// switch to HDMI:
		regval = (regval & 0xCF);	// switch off bit 4 and 5.
	}

	mmapReg2->WriteRegister_Reg2(0xB4, regval);usleep(100);

	//printf("SetARMInputActive 0xB4 write finished\n\r");

	return 0;
}

int OCMTransfer::SetHDMISource(int hdmi)
{
	int error = 0;

	if (hdmi)
	{
		//de-activate ARM-Write Access (This enables HDMI input!)
		error = SetARMInputActive(0);

		//activate internal vsync:
		error |= VsyncSelectExt();
	}
	else
	{
        	error = SetInternalVsyncFrequency(60.0);
	        //activate internal vsync:
	        error |= VsyncSelectInt();
	        //activate ARM-Write Access (This disables HDMI input!)
	        error |= SetARMInputActive(1);

		if (error == 0)
		{
			// Transfer a blank (gray) image to clear the HDMI image after switching:
			uint8_t img[g_LCOS_Width*g_LCOS_Height]{0};
			SendPatternData(img);
		}
	}

	if (error)
		return -1;
	else
		return 0;
}

int OCMTransfer::GetARMInputActive(unsigned char *value)
{
	if (!value)		// if no reference given
		return -1;

	unsigned char regval = 0;
	mmapReg2->ReadRegister_Reg2(0xB4, &regval);usleep(100);

	if ( (regval & 0x30) != 0x10) // "01" in bit 5:4
	{
		*value = 0; // not active

		printf("getARMInputActive - > regval = %d\n", regval);

		return 0;
	}


	mmapOCM->ReadRegister_OCM(0x0, &regval);usleep(100);
	*value = regval & 0x1;


	return 0;
}

void OCMTransfer::MemcpyLoop(uint8_t* dest, const uint8_t* src, size_t n)
{
	//std::cerr << "Region = "<< static_cast<void*>(dest) << " size = " << n <<"\n";

    for (size_t i = 0; i < n; i++) {
        dest[i] = src[i];
    }
}

void OCMTransfer::TestOCMReadWrite(void)
{
	std::cout << "Test Writing to OCM" << std::endl;
	uint8_t *buf;
	int i;

	for(buf = ((uint8_t *)ocm+HEC7020_OCM_TRANS_SIZE/2), i =0; i <HEC7020_OCM_TRANS_SIZE/2; ++i, ++buf){
		*buf = 3;
	}

	//std::cout << "Test Reading to OCM" << std::endl;
	for(buf = ((uint8_t *)ocm+HEC7020_OCM_TRANS_SIZE/2), i =0; i <HEC7020_OCM_TRANS_SIZE/2; ++i, ++buf){
		if(*buf != 3)
			std::cout << "Read OCM failed" << std::endl;
	}

	std::cout << "Read OCM SUCCESS" << std::endl;
}
