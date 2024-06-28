/*
 * ParametersStructure.h
 *
 *  Created on: Feb 7, 2023
 *      Author: mib_n
 */

#ifndef SRC_PATTERNCALIBMODULE_PARAMETERSSTRUCTURE_H_
#define SRC_PATTERNCALIBMODULE_PARAMETERSSTRUCTURE_H_


struct Aopt_Kopt_ThreadArgs
{
	int 			port;
	double 			freq;

	double 			result_Aopt;
	double 			result_Kopt;
};

struct Aatt_Katt_ThreadArgs
{
	int 			port;
	double 			freq;
	double 			ATT;

	double 			result_Aatt;
	double 			result_Katt;
};

struct Sigma_ThreadArgs
{
	int 			port;
	double 			freq;
	double 			temperature;

	double 			result_Sigma;
};

struct Pixel_Pos_ThreadArgs
{
	double 			f1;
	double 			f2;
	double 			fc;
	double 			temperature;

	double 			result_F1_PixelPos;
	double 			result_F2_PixelPos;
	double 			result_FC_PixelPos;
};

#endif /* SRC_PATTERNCALIBMODULE_PARAMETERSSTRUCTURE_H_ */
