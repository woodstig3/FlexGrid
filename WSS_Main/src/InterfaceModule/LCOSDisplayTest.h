/*
 * LcosDisplayTest.h
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#ifndef SRC_INTERFACEMODULE_LCOSDISPLAYTEST_H_
#define SRC_INTERFACEMODULE_LCOSDISPLAYTEST_H_

#include <stdint.h>
#include <string.h>
#include <sstream>
#include "MemoryMapping.h"


class LCOSDisplayTest {
public:
					LCOSDisplayTest();
	virtual 		~LCOSDisplayTest();

	static int		RunTest();
	void 			GetResult(std::string &output);

private:
	int 			AnalyseDisplayTest_Data(uint32_t first_data_in_read, uint32_t second_data_in_read);
	uint8_t 		DisplayBus_Test_Result[32];	// 32bits and 32 pins of LCOS.
	std::string 	outputStr;
};

#endif /* SRC_INTERFACEMODULE_LCOSDISPLAYTEST_H_ */
