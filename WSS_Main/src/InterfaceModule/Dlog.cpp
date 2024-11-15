#include <fstream>
#include <cstring>

#include "Dlog.h"
using namespace std;

char log_path_mnt[] = "/mnt/log.txt";
char log_path_bck[] = "/mnt/log.bck";

//static int  Lod = 0;
#if 0
string FNmask[VariousF] =
{
	    "HEATER_1_TEMP",
	    "HEATER_2_TEMP",
	    "TEC_TEMP",
		"ADC_AD7689_ACCESS_FAILURE",
		"DAC_AD5624_ACCESS_FAILURE",
		"DAC_LTC2620_ACCESS_FAILURE",
		"TRANSFER_FAILURE",
		"WATCH_DOG_EVENT",
		"FIRMWARE_DOWNLOAD_FAILURE",
		"WSS611_ACCESS_FAILURE",
		"QDMA_FPGA_ACCESS_FAILURE",
		"QDMA_FLASH_ACCESS_FAILURE",
		"FLASH_PROGRAMMING_ERROR",
		"EEPROM_ACCESS_FAILURE",
		"EEPROM_CHECKSUM_FAILURE",
		"CALIB_FILE_MISMATCH",
		"CALIB_FILE_MISSING",
		"CALIB_FILE_CHECKSUM_ERROR",
		"FW_FILE_CHECKSUM_ERROR",
		"FPGA1_DOWNLOAD_FAILURE",
		"FPGA2_DOWNLOAD_FAILURE"
};
#endif
string FNmask[VariousF] =
{
	    "HEATER_1_TEMP",
	    "HEATER_2_TEMP",
		"ADC_AD7689_ACCESS_FAILURE",
		"WATCH_DOG_EVENT"
};

int Debug_logfile(FaultName nameIndex,char *buf)
{
	char linebuf[256];
	const char *Indexname;
	int Lod = 0;
	int val_rename = 0;
	char *ctempstr;
	char joint[] = " # ";

	fstream Wsslog(log_path_mnt,ios::in|ios::binary);
	if(!Wsslog.is_open())
	{
		cout<<"[error] opening log file failed"<<endl;
		return -1;
	}
	while(!Wsslog.eof())
      {
		Wsslog.getline(linebuf,200);
		Lod++;
      }

//	cout << "line of file was : " << Lod << endl;
	Wsslog.close();

	if(Lod >= MaxLine)
	{
		val_rename = rename(log_path_mnt,log_path_bck);
		if(!val_rename)
		{
            cout<< "[Event] log file backup successful" << endl;
		}
		else
	    {
			cout << "[error] Log file backup failed" << endl;
			return -1;
	    }
	}

	Indexname = FNmask[nameIndex].c_str();
    //cout << "test for class string for :"<< Indexname << endl;
	ctempstr = new char[strlen(buf) + strlen(Indexname) + 1];
	strcpy(ctempstr,Indexname);
	strcat(ctempstr,joint);
	strcat(ctempstr,buf);
	//printf("%s \n",ctempstr);
	//Extend_log(ctempstr);
	return 0;
}

int Fault_logcompress(FaultName nameIndex,MessRes *mess)
{
	const char *Indexname;
	char *log_str;
	char joint[] = "#";
	//char end[] ="\n";
	char numtrans[10]={'\0'};
	int Lod = 0;
	int val_rename = 0;
	char linebuf[256];
/*
	fstream Wsslog(log_path_mnt,ios::in|ios::binary);
	if(!Wsslog.is_open())
	{
		cout<<"[error] opening log file failed"<<endl;
		return -1;
	}
	while(!Wsslog.eof())
    {
		Wsslog.getline(linebuf,200);
		Lod++;
    }

	cout << "line of file was : " << Lod << endl;
	Wsslog.close();

	if(Lod >= MaxLine)
	{
		val_rename = rename(log_path_mnt,log_path_bck);
		if(!val_rename)
		{
            cout<< "[Event] log file backup successful" << endl;
		}
		else
	    {
			cout << "[error] Log file backup failed" << endl;
			return -1;
	    }
	}
*/
	Indexname = FNmask[nameIndex].c_str();
    //cout << "test for class string for :"<< Indexname << endl;
	log_str = new char[200];
	strcpy(log_str,Indexname);
	strcat(log_str,joint);
	strcat(log_str,((mess->Degraded)? "T" : "F"));
	strcat(log_str,joint);
	sprintf(numtrans,"%d",mess->DegradedCount);
	strcat(log_str,numtrans);
	strcat(log_str,joint);
	strcat(log_str,((mess->Raised)? "T" : "F"));
	strcat(log_str,joint);
	sprintf(numtrans,"%d",mess->RaisedCount);
	strcat(log_str,numtrans);
	strcat(log_str,joint);
	printf("%s \n",log_str);
	//Extend_log(log_str,nameIndex);
	Replace_log(log_str,nameIndex);
	return 0;
}
int Replace_log(char *buf,FaultName lineIndex)
{

	ifstream in;
	in.open(log_path_mnt);
	string strFileData = "";

	int line = 1;
	char tmpLineData[200] = {0};
	while(in.getline(tmpLineData, sizeof(tmpLineData)))
	{
		if (line == lineIndex + 1)
		{
			strFileData += CharToStr(buf);
			strFileData += "\n";
		}
		else
		{
			strFileData += CharToStr(tmpLineData);
			strFileData += "\n";
		}
		line++;
	}
	in.close();

	ofstream out;
	out.open(log_path_mnt);
	out.flush();
	out<<strFileData;
	out.close();
}

string CharToStr(char * contentChar)
{
	string tempStr;
	for (int i=0;contentChar[i]!='\0';i++)
	{
		tempStr+=contentChar[i];
	}
	return tempStr;
}

//int Extend_log(char *buf)
int Extend_log(char *buf, FaultName lineIndex)
{
	int fd = -1;
	ssize_t size = -1;

	fd = open(log_path_mnt,O_RDWR|O_CREAT|O_APPEND);
	//char buf[] = "test for wahat waht the tings~~~\n";

	if(-1 == fd)
	{
		printf("[Error] Log file open fialed!\n");
		return -1;
	}
	else
	{
		printf("Create/Open log %s success\n",log_path_mnt);
		flock(fd,LOCK_EX);
	}

	size = write(fd,buf,strlen(buf));
	printf("write %d bytes to file\n",size);
	sync();
	close(fd);
	flock(fd,LOCK_UN);

	return 0;
}
int Get_logitem(int faultNo, MessDis *mess)
{
	int Lod = 0;
	char linebuf[256] = {'\0'};
	char temp[256] = {'\0'};
	char intemp[4] = {'\0'};
	char *mask;
	int split[10] = {0};
	int i=0,j=0;

	fstream Wsslog(log_path_mnt,ios::in|ios::binary);
	if(!Wsslog.is_open())
	{
		cout<<"[error] opening log file failed"<<endl;
		return -1;
	}
	while(!Wsslog.eof())
    {
		memset(linebuf,0,256);
		Wsslog.getline(linebuf,256);
		Lod++;
		//cout<<"line no was"<<Lod<<endl;
		if (Lod == faultNo)
		{
		    break;
		}
    }
	Wsslog.close();

	printf("line buf was %s \n",linebuf);
	mask = linebuf;
	for(i = 0; *mask != '\0'; mask++, i++)
	{
		if(*mask == '#')
		{
			split[j] = i;
			j++;
		}
	}
	/*for(i = 0; i < 10; i++)
	{
		cout << split[i] <<endl;
	}*/
	mask = linebuf;
	strncpy(mess->name,linebuf,split[0]);
	//printf("mess->name was %s \n",mess->name);

	strcpy(temp,(mask+split[0]+1));
	//printf("temp buf was %s \n",temp);
	strncpy(mess->Degraded,temp,(split[1]-split[0]-1));
	//printf("mess->Degraded was %s \n",mess->Degraded);

	memset(temp,0,256);
	strcpy(temp,(mask+split[1]+1));
	//printf("temp buf was %s \n",temp);
	strncpy(mess->DegradedCount,temp,(split[2]-split[1]-1));
	//printf("mess->DegradedCount was %s \n",mess->DegradedCount);

    memset(temp,0,256);
    strcpy(temp,(mask+split[2]+1));
    //printf("temp buf was %s \n",temp);
    strncpy(mess->Raised,temp,(split[3]-split[2]-1));
    //printf("mess->Raised was %s \n",mess->Raised);


    memset(temp,0,256);
    strcpy(temp,(mask+split[3]+1));
    //printf("temp buf was %s \n",temp);
    strncpy(mess->RaisedCount,temp,(split[4]-split[3]-1));
    //printf("mess->RaisedCount was %s \n",mess->RaisedCount);
    //if(temp[0] == 'T')
    //{
    //	mess->Raised = true;
    //}
    //else
    //{
    //	mess->Raised = false;
    //}

    //memset(temp,0,256);
    //strcpy(temp,(mask+split[4]+1));
    //printf("temp buf was %s \n",temp);
    //strncpy(intemp,temp,(split[5]-split[4]-1));
    //printf("intemp temp buf was %s \n",intemp);
    //mess->RaisedCount = atoi(intemp);
    //printf("intemp temp interage was %d \n",mess->RaisedCount);

    //memset(temp,0,256);
    //strcpy(temp,(mask+split[5]+1));
    //printf("temp buf was %s \n",temp);
    //strncpy(mess->Debounce,temp,(split[6]-split[5]-1));
    //printf("mess->Debounce was %s \n",mess->Debounce);

    //memset(temp,0,256);
    //strcpy(temp,(mask+split[6]+1));
    //printf("temp buf was %s \n",temp);
    //strncpy(mess->FailCondition,temp,(split[7]-split[6]-1));
    //printf("mess->FailCondition was %s \n",mess->FailCondition);

    //memset(temp,0,256);
    //strcpy(temp,(mask+split[7]+1));
    //printf("temp buf was %s \n",temp);
    //strcpy(mess->DegradeCondition,temp);
    //printf("mess->DegradeCondition was %s \n",mess->DegradeCondition);

	return 0;
}
