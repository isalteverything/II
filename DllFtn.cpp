//---------------------------------------------------------------------------
// DllFtn.cpp
//
//    INNOVATIVE INTEGRATION CORPORATION PROPRIETARY INFORMATION
//  This software is supplied under the terms of a license agreement or nondisclosure
//  agreement with Innovative Integration Corporation and may not be copied or
//  disclosed except in accordance with the terms of that agreement.
//  Copyright (c) 2000..2005 Innovative Integration Corporation.
//  All Rights Reserved.
//---------------------------------------------------------------------------

//#include "stdafx.h"
#include "DllFtn.h"
#include "CustomDeviceDll.h"
#include <sstream>
#include <string>
#include <algorithm>
#include <ApplicationIo.h>
#include <Trace_Mb.h>
#include <mex.h>

//===========================================================================
//  CLASS UserInterface  -- Forward UI callbacks to WinDriver debug window
//===========================================================================

class UserInterface : public IUserInterface
{
public:
    UserInterface()
        {}

	// Parsing
	void UpdateProgress(int percent)
	    {  }

    void  UpdateLogicLoadProgress(int percent)
        {  }
    void  Log(const std::string & a_string)
	{
			Debug.Log(a_string); 
//			mexPrintf("Debug: %s\n",a_string.c_str());
	}
	void  Status(const std::string & a_string)
	{  
		Log(a_string);  
		mexPrintf("Status: %s\n",a_string.c_str());
	}
	void  LogicStatus(const std::string & a_string)
	{
			Log(a_string);  
//			mexPrintf("Logic Status: %s\n",a_string.c_str());
	}
	void  GetSettings()
		{  }
	void  SetSettings()
		{  }
	void  AfterStreamAutoStop()
		{  }
	void  Warning(const std::string & a_string)
	{
		Log(a_string);  
		mexPrintf("Warning: %s\n",a_string.c_str());
	}
	void  PeriodicStatus()
		{  }
	void  ExternalClockFrequency(float value)
		{  
        std::stringstream msg;
        msg << "ExternalClockFrequency: " << value; 
        Log(msg.str());  
        }

	void  DisplayHelp()
		{  }

	//  FICL User Interface Ftns
	void  FiclLog(const std::string & a_string)
		{  }
	std::string  FiclReadCommandString()
		{  return "";  }
	void  FiclCommandComplete()
		{  }
private:

    Innovative::Trace                   Debug;
};


typedef std::map<int, ApplicationIo *>    IoMap;
static IoMap Io;
static UserInterface UI;

//---------------------------------------------------------------------------

using namespace std;

//========================================================================
//  Configuration
//========================================================================

//---------------------------------------------------------------------------
//  DeviceOpen() -- Open the external device
//---------------------------------------------------------------------------

int EXPORT deviceOpen(int target)
{
    try
        {
        if (Io.find(target) == Io.end())
            {
            Io[target] = new ApplicationIo(&UI);
            Io[target]->Settings.Target = target;
            Io[target]->Open();
            }
        }
    catch (...)
        {
        return -1;
        }

    return 0;
}

//---------------------------------------------------------------------------
//  DeviceDeviceClose() -- Close external device
//---------------------------------------------------------------------------

int EXPORT deviceClose(int target)
{
    try
        {
        if (Io.find(target) != Io.end())
            {
            delete Io[target];
            Io.erase(target);
            }
        }
    catch (...)
        {
        return -1;
        }

    return 0;
}

int EXPORT boardCount()
{
	ApplicationIo *Io1 = new ApplicationIo(&UI);
	int Count = Io1->BoardCount();
	return Count;
}

int EXPORT startStream(int target)
{
	try 
	{
		Io[target]->StartStreaming();
	}
	catch (...)
	{
		return -1;
	}
	return 0;
}

int EXPORT stopStream(int target)
{
	Io[target]->StopStreaming();
	return 0;
}

int EXPORT Parse(int target,int ohhgod)
{
	Io[target]->ParseFile(ohhgod);
	return 0;
}

int EXPORT arrayTest(int target)
{
	Io[target]->testArray();
	return 0;
}

int EXPORT Vpp(int target)
{
	Io[target]->doVpp();
	return 0;
}

bool EXPORT dlc(int target)
{
	bool dd = Io[target]->DLC();
	return dd;
}

int EXPORT loadSettings(int target)
{
//	Io[target]->Settings.Load();
	ApplicationSettings();

	return 0;
}

int EXPORT setParams(int target, const char *param, double value)
{
	Io[target]->setParameters(param,value);
	return 0;
}