/*
 * LcosDisplayTest.cpp
 *
 *  Created on: Mar 9, 2023
 *      Author: Administrator
 */

#include "InterfaceModule/LCOSDisplayTest.h"

LCOSDisplayTest::LCOSDisplayTest()
{
	// TODO Auto-generated constructor stub


}

LCOSDisplayTest::~LCOSDisplayTest()
{
	// TODO Auto-generated destructor stub
}

int LCOSDisplayTest::RunTest()
{
	MemoryMapping m_mmap(MemoryMapping::REG);

	// 1- Read data from the 0x04 register, bit[3] = b'1 DAC or b'0 Display Write En
	uint8_t input = 0;
	m_mmap.ReadRegister_Reg2(0x04,&input);
	//printf("0x04 register =  %02x\n\r", input);

	// 2- Write data 0x05 to the 0x04 register
	input = 0x05;
	m_mmap.WriteRegister_Reg2(0x04, input);
	//printf("Write 0x04 register value =  %02x\n\r", input);

	// 3- Read data from the 0x06 register, the data is 0x0
	m_mmap.ReadRegister_Reg2(0x06,&input);
	//printf("0x06 register =  %02x\n\r", input);

	// 4- Write data 0x01 to 0x06 register; LCD_Data_oe=1
	input = 0x01;
	m_mmap.WriteRegister_Reg2(0x06, input);
	//printf("0x06 register value =  %04x\n\r", input);

	// 5- Read data from 0x9B register, the data is 0xA4
	m_mmap.ReadRegister_Reg2(0x9B,&input);
	//printf("0x9B register =  %04x\n\r", input);

	//6- Write data 0xA0 to 0x9B register
	input = 0xA0;
	m_mmap.WriteRegister_Reg2(0x9B, input);
	//printf("0x9B register value =  %04x\n\r", input);

	//7- Write data 0x80 to 0x2C register
	input = 0x80;
	m_mmap.WriteRegister_Reg2(0x2C, input);
	//printf("0x2C register value =  %04x\n\r", input);

	//8- Write data 0x81 to the 0x01 register
	input = 0x81;
	m_mmap.WriteRegister_Reg2(0x01, input);
	//printf("0x01 register value =  %04x\n\r", input);

	//9- Read data from 0x31/0x32/0x33/0x34 register
	uint8_t input31, input32, input33, input34;
	m_mmap.ReadRegister_Reg2(0x31,&input31);
	m_mmap.ReadRegister_Reg2(0x32,&input32);
	m_mmap.ReadRegister_Reg2(0x33,&input33);
	m_mmap.ReadRegister_Reg2(0x34,&input34);

	uint32_t first_data_in_read = (input34 << 24) | (input33 << 16) | (input32 << 8) | (input31);


	//10- Write data 0x82 to the 0x01 register
	input = 0x82;
	m_mmap.WriteRegister_Reg2(0x01, input);
	//printf("0x01 register value =  %04x\n\r", input);

	//11- Read data from 0x31/0x32/0x33/0x34 register
	m_mmap.ReadRegister_Reg2(0x31,&input31);
	m_mmap.ReadRegister_Reg2(0x32,&input32);
	m_mmap.ReadRegister_Reg2(0x33,&input33);
	m_mmap.ReadRegister_Reg2(0x34,&input34);

	uint32_t second_data_in_read = (input34 << 24) | (input33 << 16) | (input32 << 8) | (input31);

	//12- Write data 0x80 to the 0x01 register
	input = 0x80;
	m_mmap.WriteRegister_Reg2(0x01, input);
	//printf("0x01 register value =  %04x\n\r", input);


	//13- Write data 0x00 to 0x2C register
	input = 0x00;
	m_mmap.WriteRegister_Reg2(0x2C, input);
	//printf("0x2C register value =  %04x\n\r", input);


	//14- Write data 0xA4 to the 0x9B register
	input = 0xA4;
	m_mmap.WriteRegister_Reg2(0x9B, input);
	//printf("0x98 register value =  %04x\n\r", input);


	//15- Write data 0x00 to the 0x06 register
	input = 0x00;
	m_mmap.WriteRegister_Reg2(0x06, input);
	//printf("0x06 register value =  %04x\n\r", input);


	//16- Write data 0x05 to the 0x04 register
	input = 0x05;
	m_mmap.WriteRegister_Reg2(0x04, input);
	//printf("Write 0x04 register value =  %02x\n\r", input);

	AnalyseDisplayTest_Data(first_data_in_read, second_data_in_read);

	return (0);
}

int LCOSDisplayTest::AnalyseDisplayTest_Data(uint32_t first_data_in_read, uint32_t second_data_in_read)
{
	 std::ostringstream oss;
	//if first_data_in_read + second_data_in_read == 0xFFFFFFFF	// LCOS is all connected well
	// if first_data_in_read + second_data_in_read != 0xFFFFFFFF // LCOS pin has issue
	// any bit that is same in both first_data_in_read and second_data_in_read is the cause of the problem
	// e.g first_data_in_read = b'1010_1010_0000_0000_1111_1111_0101_1010
	//	   second_data_in_read= b'0101_0101_1111_1111_0000_0000_1010_1010
	//	first and second read both has last 4 bits same, means those four bits/PINS _1010 have isssue. D0-D3
	// TAKE XOR of first and second, if bits are different its OK(1), if bits are same its WRONG(0).

	uint32_t testResult = first_data_in_read ^ second_data_in_read;	// if bits different then result 1 otherwise 0

	// Loop through 32bits in result and update result in DisplayBus Test array.
	for(int i=0; i<32; i++)
	{
		DisplayBus_Test_Result[i] = ((testResult & (1 << i)) > 0)? 1:0;		// extracting bit information on testResult start from 0th bit to 31st it

		if(DisplayBus_Test_Result[i] == 0)
		{
			printf("D%02d: X\n", i);		// two space for character and one space for distance between pins
			oss << "D" << i << ": X" << "\n";

		}
		else
		{
			printf("D%02d: %d\n",i, DisplayBus_Test_Result[i]);		// For tick xil_printf("D%02d: \xE2\x9C\x93\n",i);
			oss << "D" << i << ": " << DisplayBus_Test_Result[i]  << "\n";
		}

	}

	outputStr = oss.str();

	return (0);
}

void LCOSDisplayTest::GetResult(std::string &output)
{
	output = outputStr;
}
