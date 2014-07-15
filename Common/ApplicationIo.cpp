// ApplicationIo.cpp
//
// Board-specific data flow and hardware I/O
// Copyright 2006 Innovative Integration
//---------------------------------------------------------------------------

#define _WIN32_WINNT 0x0601             // Change this to the appropriate value to target other versions of Windows.

#include <Malibu_Mb.h>                  // II general malibu utils
#include <Analysis_Mb.h>                // II data analysis library utils
#include <IppMemoryUtils_Mb.h>          // IPP (fast calculation) memory support (unused)
#include <IppSupport_Mb.h>              // IPP (fast calculation) support (unused)
#include <IppIntegerDG_Mb.h>            // IPP (fast calculation) Datagram support (unused)
#include "ApplicationIo.h"              // MPD main header file
#include <SystemSupport_Mb.h>           // II system support utils
#include <StringSupport_Mb.h>           // II string support utils
#include <limits>                       // C standcard ?
#include <cmath>                        // C standard complex math support
#include <numeric>                      // C standard numeric support
#include <Random_Mb.h>                  // II ?
#include <Exception_Mb.h>               // II Execption handler
#include <FileSupport_Mb.h>             // II disk IO support
#include <DeviceEnum_Mb.h>              // II used for fnc: 'deviceCount'
#include <Buffer_Mb.h>                  // II Buffer utils
#include <BufferHeader_Mb.h>            // II Buffer header utils
#include <BufferDatagrams_Mb.h>         // II Datagram utils
#include <Utility_Mb.h>                 // II general utils
#include <string>                       // C standard string support 
#include <sstream>                      // C standard string stream support
#include <cstdio>                       // C standard IO utils
#include <iostream>                     // C standard IO stream utils
#include <Magic_Mb.h>

// Stuff for parsing
#include <VitaPacketFileDataSet_Mb.h>   // Parse binary disk file
#include <PacketFileDataSet_Mb.h>       // Parse binary disk file
#include <FileDataSet_Mb.h>             // Parse binary disk file
#include <vector>                       // stl::vector object
#include <cuda.h>                       // General CUDA utils
#include <cuda_runtime.h>               // Copy to GPU (gpuMemCpy)

// Stuff for matlab 
#include "mex.h"                        // General MEX utils (matlab fncs)
#include <MatlabMatrix_Mb.h>            // II matlab utils (unused)
#include "mxGPUArray.h"                 // Matlab GPU utils

// Parallel 
#include <omp.h>                        // Easy parfor loops (unused)

// X6 specific stuff
#include <X6Idrom_Mb.h>                 // II X6 specific utils
#include <ppl.h>                        // Parallel (unused)

using namespace Innovative;
using namespace std;
using namespace concurrency;

//===========================================================================
//  CLASS ApplicationIo  -- Hardware Access and Application I/O Class
//===========================================================================
//---------------------------------------------------------------------------
//  constructor for class ApplicationIo
//---------------------------------------------------------------------------

ApplicationIo::ApplicationIo(IUserInterface * ui)
    : FiclIo(ui), UI(ui),
      Opened(false), StreamConnected(false), Stopped(true),
      FBlockRate(0.0f), FWordCount(0), SamplesPerWord(1),
      WordsToLog(0), Time(6), BytesPerBlock(6)
{
    TraceVerbosity(Trace::vNormal);

	OnSplitComplete.SetEvent(this, &ApplicationIo::HandleSplitComplete);
	OnSplitProgress.SetEvent(this, &ApplicationIo::HandleSplitProgress);
	
	OnLog.SetEvent(this, &ApplicationIo::HandleOnLog);
    Module.OnLog.SetEvent(this, &ApplicationIo::HandleOnLog);

    // Use IPP performance memory functions.
	Init::UsePerformanceMemoryFunctions();
    //
    //  Set up Loggers and graphs
    std::stringstream ss;
    ss << Settings.Path << "Data.bin";

    Logger.FileName(ss.str());
    Graph.BinFile(Logger.FileName());
	Graph.System().ServerSlotName("Stream");
    Timer.Interval(1000);

// Suppress anoying startup message
	/*
    std::stringstream msg;
    msg << "Be sure to read the help file for info on this program";
    msg << " located in the root of the this example folder.  ";
    Log(msg.str());
	*/

// Old stuff used for parsing
	/*
	handler to OnImageAvailable event
	Vpp.OnImageAvailable.SetEvent(this, &ApplicationIo::Handle_VPP_ImageAvailable);
	*/

	if (Settings.Help)
	{
        UI->DisplayHelp();
		Settings.Help = false;
	}

// Detect presence of GPU. If a = 0, matlab failed to detect a GPU and will store/analyze
//	data on system memory. If a = 1 Matlab uses first detected GPU for storing/analyzing 
//	data. If another GPU is added to system, the if condition should be changed to:
// if (gpuCount >=1)

	mxArray *a;
	a = mxCreateLogicalScalar(NULL);
	mexCallMATLAB(1,&a,0,NULL,"gpuDeviceCount");
	gpuCount = mxGetScalar(a);

	if (gpuCount >= 1)
	{
		mxInitGPU();
	}


}

//---------------------------------------------------------------------------
//  destructor for class ApplicationIo
//---------------------------------------------------------------------------

ApplicationIo::~ApplicationIo()
{
	free(arrch1);
	free(arrch2);
	Close();
}

//---------------------------------------------------------------------------
//  ApplicationIo::BoardCount() -- Query number of installed boards
//---------------------------------------------------------------------------

unsigned int ApplicationIo::BoardCount()
{
    return static_cast<unsigned int>(Module().BoardCount());
}

//---------------------------------------------------------------------------
//  ApplicationIo::Open() -- Open Hardware & set up callbacks
//---------------------------------------------------------------------------

void  ApplicationIo::Open()
{
    UI->GetSettings();
    //
    //  Configure Trigger Manager Event Handlers
    Trig.OnDisableTrigger.SetEvent(this, &ApplicationIo::HandleDisableTrigger);
    Trig.OnExternalTrigger.SetEvent(this, &ApplicationIo::HandleExternalTrigger);
    Trig.OnSoftwareTrigger.SetEvent(this, &ApplicationIo::HandleSoftwareTrigger);

    Trig.DelayedTrigger(true);   // trigger delayed after start
    //
    //  Configure Module Event Handlers
    Module().OnBeforeStreamStart.SetEvent(this, &ApplicationIo::HandleBeforeStreamStart);
    Module().OnBeforeStreamStart.Synchronize();
    Module().OnAfterStreamStart.SetEvent(this, &ApplicationIo::HandleAfterStreamStart);
    Module().OnAfterStreamStart.Synchronize();
    Module().OnAfterStreamStop.SetEvent(this, &ApplicationIo::HandleAfterStreamStop);
    Module().OnAfterStreamStop.Synchronize();

    //  Alerts
    Module.HookAlerts();

    //  Configure Stream Event Handlers
    Stream.OnVeloDataAvailable.SetEvent(this, &ApplicationIo::HandleDataAvailable);
    Stream.DirectDataMode(false);

    Stream.OnAfterStop.SetEvent(this, &ApplicationIo::HandleAfterStop);
    Stream.OnAfterStop.Synchronize();

    Stream.RxLoadBalancing(false);
    Stream.TxLoadBalancing(false);

    Timer.OnElapsed.SetEvent(this, &ApplicationIo::HandleTimer);
    Timer.OnElapsed.Thunk();

    // Insure BM size is a multiple of four MB
    const int Meg = 1024 * 1024;
    const int BmSize = std::max(Settings.BusmasterSize/4, 1) * 4;
    Module().IncomingBusMasterSize(BmSize * Meg);
    Module().OutgoingBusMasterSize(1 * Meg);
    Module().Target(Settings.Target);

    //  Open Device
    try
        {
        Module().Open();

        std::stringstream msg;
        msg << "Bus master size: " << BmSize << " MB";
        Log(msg.str());
        }

    catch(Innovative::MalibuException & e)
        {
        UI->Log("Module Device Open Failure:");
        UI->Log(e.what());
        return;
        }

    catch(...)
        {
        UI->Log("Module Device Open Failure: Unknown Exception");
        Opened = false;
        return;
        }

    Module().Reset();
    UI->Log("Module Device opened successfully...");
    Opened = true;


    //  Connect Stream
    Stream.ConnectTo(&(Module.Ref()));
    StreamConnected = true;
    UI->Log("Stream Connected...");

	DisplayLogicVersion();

// Initializes two arrays (declared in ApplicationIo.h) for storing channelized data 
//	as well as two counters

	int const sz = Settings.SamplesToLog/2;
	arrch1 = new short [sz];
	arrch2 = new short [sz];

	cntrch1 = 0;
	cntrch2 = 0;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++++++++++++ NOT WORKING +++++++++++++++++++++++++++++++++++//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//

// This section of code configured two mxArrays to store data directly instead of using an
// intermediate C array. Never got it to work properly. In theory this technique could be
// more efficient.
/*
	cntrch1 = 0;
	cntrch2 = 0;
   	int rows = Settings.FrameSize;
	int cols = Settings.SamplesToLog/(rows);

	mwSize const dims[2] = {rows,cols};
	mwSize const dim = 2;

	mxarrch1 = mxCreateNumericArray(dim,dims,mxINT16_CLASS,mxREAL);
	mxarrch2 = mxCreateNumericArray(dim,dims,mxINT16_CLASS,mxREAL);

//	mxarrch1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
//	mxarrch2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
*/


// This section of code configured two mxGPUArrays to store data directly instead of using an
// intermediate C array. Never got it to work properly. In theory this technique could be
// more efficient.
/*
	int rows = Settings.FrameSize;
	int cols = Settings.SamplesToLog/(2*rows);

	mwSize const dims[2] = {rows,cols};
	mwSize const dim = 2;
	asdfgpuch1asdf = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);
	asdfgpuch2asdf = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);
	short * agpuch1a = (short *) mxGPUGetData(asdfgpuch1asdf);
	short * agpuch2a = (short *) mxGPUGetData(asdfgpuch2asdf);
*/
//	int rows = Settings.FrameSize;
//	int cols = Settings.SamplesToLog/(2*rows);

//	mwSize const dims[2] = {rows,cols};
//	mwSize const dim = 2;
//	asdfgpuch1asdf = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);
//	asdfgpuch2asdf = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);

//	int cntrch1 = 0;
//	int cntrch2 = 0;
	
//	int rows = Settings.FrameSize;
//	int cols = Settings.SamplesToLog/(2*rows);

//	mxarrch1 = mxCreateNumericArray(dim,dims,mxINT16_CLASS,mxREAL);
//	mxarrch1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREALa);
//	short * mxptrch1 = (short *) mxGetData(ApplicationIo::mxarrch1);

//	mxarrch2 = mxCreateNumericArray(dim,dims,mxINT16_CLASS,mxREAL);
//	mxarrch2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
//	short * mxptrch2 = (short *) mxGetData(mxarrch2);
//	Log("arrays created");
//	mexPrintf("ptrch1: %x\n",mxptrch1);
//	mexPrintf("ptrch2: %x\n",mxptrch2);

/*
	int rows = Settings.FrameSize;
	int cols = Settings.SamplesToLog/(2*rows);

	mwSize const dims[2] = {rows,cols};
	mwSize const dim = 2;
//	asdfgpuch1asdf = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);
//	asdfgpuch2asdf = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);

	int cntrch1 = 0;
	int cntrch2 = 0;
	
//	int rows = Settings.FrameSize;
//	int cols = Settings.SamplesToLog/(2*rows);

//	mxarrch1 = mxCreateNumericArray(dim,dims,mxINT16_CLASS,mxREAL);
	mxarrch1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
	mxptrch1 = (short *) mxGetData(mxarrch1);

//	mxarrch2 = mxCreateNumericArray(dim,dims,mxINT16_CLASS,mxREAL);
	mxarrch2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
	mxptrch2 = (short *) mxGetData(mxarrch2);
	Log("arrays created");

	mexPrintf("ptrch1: %x\n",mxptrch1);
	mexPrintf("ptrch2: %x\n",mxptrch2);
*/
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
}

//---------------------------------------------------------------------------
//  ApplicationIo::Close() -- Close Hardware & set up callbacks
//---------------------------------------------------------------------------

void  ApplicationIo::Close()
{
    if (!Opened)
        return;

    Stream.Disconnect();
    StreamConnected = false;

    Module().Close();
    Opened = false;

    UI->Log("Stream Disconnected...");
}

//---------------------------------------------------------------------------
//  ApplicationIo::StartStreaming() --  Initiate data flow
//---------------------------------------------------------------------------

void  ApplicationIo::StartStreaming()
{
    if (!StreamConnected)
        {
        Log("Stream not connected! -- Open the boards");
        return;
        }
	cntrch1 = 0;
	cntrch2 = 0;


    //  Set up Parameters for Data Streaming
    //  First have UI get settings into our settings store
	UI->GetSettings();
	if (Settings.SampleRate*1.e6 > Module().Input().Info().MaxRate())
        {
        Log("Sample rate too high.");
        StopStreaming();
        UI->AfterStreamAutoStop();
        return;
        }

	if (Settings.Framed)
		{
        // Granularity is firmware limitation
        int framesize = Module().Input().Info().TriggerFrameGranularity();
        if (Settings.FrameSize % framesize)
            {
            std::stringstream msg;
            msg << "Error: Frame count must be a multiple of " << framesize;
            Log(msg.str());
            UI->AfterStreamAutoStop();
            return;
            }
        }

    FWordCount = 0;
    SamplesPerWord = Module().Input().Info().SamplesPerWord();
    WordsToLog = Settings.SamplesToLog / SamplesPerWord;

    FBlockRate = 0;

    // Disable triggers initially  (done by library)
    Trig.DelayedTriggerPeriod(Settings.DelayedTriggerPeriod);
    Trig.ExternalTrigger(Settings.ExternalTrigger == 1); 
    Trig.AtConfigure();

    //  Channel Enables
    Module().Input().ChannelDisableAll();
    for (unsigned int i = 0; i < Module().Input().Channels(); ++i)
        {
        bool active = Settings.ActiveChannels[i] ? true : false;
        if (active==true)
            Module().Input().ChannelEnabled(i, true);
        }

	// Route ext clock source
	IX6ClockIo::IIClockSelect cks[] = { IX6ClockIo::cslFrontPanel, IX6ClockIo::cslP16 };
    Module().Clock().ExternalClkSelect(cks[Settings.ExtClockSrcSelection]);

    // Route reference.
	IX6ClockIo::IIReferenceSource ref[] = { IX6ClockIo::rsExternal, IX6ClockIo::rsInternal };
    Module().Clock().Reference(ref[Settings.ReferenceClockSource]);
    Module().Clock().ReferenceFrequency(Settings.ReferenceRate * 1e6);

	// Route clock
    IX6ClockIo::IIClockSource src[] = { IX6ClockIo::csExternal, IX6ClockIo::csInternal };
    Module().Clock().Source(src[Settings.SampleClockSource]);
    Module().Clock().Frequency(Settings.SampleRate * 1e6);

    // Readback Frequency
    double freq_actual = Module().Clock().FrequencyActual();
    {
    std::stringstream msg;
    msg << "Actual PLL Frequency: " << freq_actual ;
    Log(msg.str());
    }

    int ActiveChannels = Module().Input().ActiveChannels();
    if (!ActiveChannels)
        {
        Log("Error: Must enable at least one channel");
        UI->AfterStreamAutoStop();
        return;
        }

    //  Always preconfigure
    Stream.Preconfigure();


    //  Velocia Packet Size
    Module.SetInputPacketDataSize(Settings.PacketSize);
    Module().Velo().ForceVeloPacketSize(Settings.ForcePacketSize);

    //  Input Test Generator Setup
    Module.SetTestConfiguration( Settings.TestCounterEnable, Settings.TestGenMode );

    // Set Decimation Factor
    int factor = Settings.DecimationEnable ? Settings.DecimationFactor : 0;
    Module().Input().Decimation(factor);

    // Frame Triggering and other Trigger Config
    Module().Input().Trigger().FramedMode((Settings.Framed)? true : false);
    Module().Input().Trigger().Edge((Settings.EdgeTrigger)? true : false);
    Module().Input().Trigger().FrameSize(Settings.FrameSize);
    // Route External Trigger source
    IX6IoDevice::AfeExtSyncOptions syncsel[] = { IX6IoDevice::essFrontPanel, IX6IoDevice::essP16 };
    Module().Input().Trigger().ExternalSyncSource( syncsel[ Settings.ExtTriggerSrcSelection ] );

    //  Pulse Trigger Config
    {
		std::vector<float> Delays;
		std::vector<float> Widths;
		//  ...push first delay
		Delays.push_back(Settings.Pulse.Delay);  Widths.push_back(Settings.Pulse.Width);
		//  ...do we push delay 2?
		if (Settings.Pulse.Delay_2!=0 && Settings.Pulse.Width_2!=0)
		{  Delays.push_back(Settings.Pulse.Delay_2);  Widths.push_back(Settings.Pulse.Width_2 ); }
		
		//  ...add to module configuration
		Module.SetPulseTriggerConfiguration(Settings.Pulse.Enable,
			Settings.Pulse.Period,
			Delays, Widths);
    }

	//  Alert Config
	Module.ConfigureAlerts(Settings.AlertEnable);

    //  Start Loggers on active channels

	if (Settings.PlotEnable)
		{
			Graph.Quit();
		}

	if (Settings.LoggerEnable || Settings.PlotEnable)
		{
			Logger.Start();
		}

	Trig.AtStreamStart();

    //  Start Streaming
    Stopped = false;
    Stream.Start();

	//  RunTimeSW.Start();
    Log("Stream Mode started");

    Timer.Enabled(true);

}

//---------------------------------------------------------------------------
//  ApplicationIo::StopStreaming() --  Terminate data flow
//---------------------------------------------------------------------------

void  ApplicationIo::StopStreaming()
{
    if (!StreamConnected)
        {
        Log("Stream not connected! -- Open the boards");
        return;
        }

	//  Stop Streaming
    Stream.Stop();
    Stopped = true;
    Timer.Enabled(false);

    Trig.AtStreamStop();
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  Data Flow Event Handlers
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//---------------------------------------------------------------------------
//  ApplicationIo::HandleDataAvailable() --  Handle received packet
//---------------------------------------------------------------------------

void  ApplicationIo::HandleDataAvailable(VitaPacketStreamDataEvent & Event)
{
	if (Stopped)
        return;

	VeloBuffer Packet;

	Event.Sender->Recv(Packet);	

//	int const sz = Settings.SamplesToLog/2;
//	arrch1 = new short [sz];
//	arrch2 = new short [sz];


//	FWordCount += Packet.SizeInInts();
//	IntegerDG Packet_DG(Packet);
//    TallyBlock(Packet_DG.size()*sizeof(int));
//    if (Settings.LoggerEnable)
//		if (WordsToLog==0 || (FWordCount < WordsToLog))
//		{
//			Logger.LogWithHeader(Packet);
			//
			//  Extract the packet from the Incoming Queue...
			// Attempt to parse in "real time"
	VitaCursor currentVita(Packet,0);
	unsigned short vitsize(0);
	int vitsid(0);
//			short * agpuch1a = (short *) mxGPUGetData(asdfgpuch1asdf);
//			short * agpuch2a = (short *) mxGPUGetData(asdfgpuch2asdf);
//			mexPrintf("address is: %x \n",agpuch1a);
//			mexPrintf("address is: %x \n",agpuch2a);
	for (;;)
	{

		if (!currentVita.isValid())
		{
			mxArray *dd;
			dd = mxCreateDoubleScalar(1);
			mexPutVariable("base","notok",dd);
			mxDestroyArray(dd);
			Log("broke since current vita is not valid!:");
			break;
		}
	
		vitsize = currentVita.PacketSize();
		vitsid = currentVita.Sid();
		AccessDatagram<short> DG(currentVita.Data());

// Check Vitas for channelization
		if (vitsid != 256 && vitsid !=257)
		{
			break;
		};
		
		if (vitsid == 256)
		{
			if (Settings.LoggerEnable)
				if (WordsToLog==0 || (FWordCount < WordsToLog))
				{
//					mexPrintf("ptr: %x\n",mxptrch1);
//					Log("cntrch1: "+IntToString(cntrch1));
					FWordCount += DG.SizeInInts();
					memcpy(&(arrch1[cntrch1]),&DG[0],DG.size()*sizeof(short));
//	GPU direct		cudaMemcpy(agpuch1a+cntrch1*sizeof(short),&DG[0],DG.size()*sizeof(short),cudaMemcpyHostToDevice);
//	Vector			asdfch1asdf.insert(asdfch1asdf.end(),DG.begin(),DG.end());
					cntrch1 += DG.SizeInElements();
					TallyBlock(DG.size()*sizeof(short));

				}
				
				
				/*
				for (size_t j = 0; j<DG.SizeInElements(); ++j)
				{
				asdfch1asdf.push_back(DG[j]);
				}
				*/
				
		}
		
		if (vitsid == 257)
		{
			if (Settings.LoggerEnable)
				if (WordsToLog==0 || (FWordCount < WordsToLog))
				{
//					Log("cntrch2: "+IntToString(cntrch2));
					FWordCount += DG.SizeInInts();
//	Vector			asdfch2asdf.insert(asdfch2asdf.end(),DG.begin(),DG.end());
//	GPU direct		cudaMemcpy(agpuch2a+cntrch2*sizeof(short),&DG[0],DG.size()*sizeof(short),cudaMemcpyHostToDevice);
					memcpy(&(arrch2[cntrch2]),&DG[0],DG.size()*sizeof(short));
					TallyBlock(DG.size()*sizeof(short));	
					cntrch2 += DG.SizeInElements();
					
				}
		}
		currentVita.advance();
	}
}


//---------------------------------------------------------------------------
//  ApplicationIo::HandleSplitComplete() --  
//---------------------------------------------------------------------------
void ApplicationIo::HandleSplitComplete(Innovative::ProcessCompletionEvent & Event)
{
	UI->UpdateProgress(100);
	UI->Log("Extraction Complete");
}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleSplitProgres() --  
//---------------------------------------------------------------------------
void ApplicationIo::HandleSplitProgress(Innovative::ProcessProgressEvent & Event)
{
	UI->UpdateProgress(Event.Percent);
}


//---------------------------------------------------------------------------
//  ApplicationIo::Extract()
//---------------------------------------------------------------------------
// Outdated function for use with Vita Packet Parser
/*
void ApplicationIo::Extract()
{
	UI->GetSettings();
	Task.Process(Settings.File, true);

	mexPrintf("Parsing Process Underway...");
}
*/

//---------------------------------------------------------------------------
//  ApplicationIo::TallyBlock() --  Finish processing of received packet
//---------------------------------------------------------------------------

void  ApplicationIo::TallyBlock(size_t bytes)
{
	double Period = Time.Differential();
    double AvgBytes = BytesPerBlock.Process(static_cast<double>(bytes));
    if (Period)
		FBlockRate = AvgBytes / (Period*1.0e6);
	//    if (FBlockRate > 100.0)
	//        while (1==1)
	//            ;
    //
    //  Stop streaming when both Channels have passed their limit
    if (Settings.AutoStop && IsDataLoggingCompleted() && !Stopped)
        {
        // Stop counter and display it
        StopStreaming();
        Log("Stream Mode Stopped automatically");
//        Log(std::string("Elasped (S): ") + FloatToString(elapsed));
        }

}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleBeforeStreamStart() --  Pre-streaming init event
//---------------------------------------------------------------------------

void ApplicationIo::HandleBeforeStreamStart(OpenWire::NotifyEvent & /*Event*/)
{
}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleAfterStreamStart() --  Post streaming init event
//---------------------------------------------------------------------------

void ApplicationIo::HandleAfterStreamStart(OpenWire::NotifyEvent & /*Event*/)
{
    Log(std::string("Analog I/O started"));
}

//---------------------------------------------------------------------------
//  ApplicationIo::Handle_VPP_ImageAvailable() -- Parser callback
//---------------------------------------------------------------------------
//  Added by MPD to channelize data
//  Outdated function for use with vita packet parser
/*
void ApplicationIo::Handle_VPP_ImageAvailable(Innovative::VitaPacketParserImageAvailable & event)
{
	// Create Vita Packet
	VitaBuffer Vita;
	//  Fill with Image data
	event.Image.CopyDataTo( Vita );

	//  ...Process Vita Packet Data
	DataSet


}
*/



//---------------------------------------------------------------------------
//  ApplicationIo::HandleAfterStreamStop() --  Post stream termination event
//---------------------------------------------------------------------------
//  Hardware only commands here please!
//  Data may arrived after this handler is called!

void ApplicationIo::HandleAfterStreamStop(OpenWire::NotifyEvent & /*Event*/)
{
    // Disable external triggering initially
    Module.SetInputSoftwareTrigger(false);
    Module().Input().Trigger().External(false);
	Log("stopped");

}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleAfterStop() --  Post stream termination event
//---------------------------------------------------------------------------

void ApplicationIo::HandleAfterStop(OpenWire::NotifyEvent & /*Event*/)
{

    //
    //  Stop Loggers on active Channels
    if (Settings.LoggerEnable || Settings.PlotEnable)
        {
        Logger.Stop();
        InitBddFile(Graph);

		if (Logger.Logged() && Settings.PlotEnable)
			Graph.Plot();
        }
    Log(std::string("Analog I/O Stopped"));
	
	int rows = Settings.FrameSize;
//	int cols = asdfch1asdf.size()/rows;
	int cols = Settings.SamplesToLog/(2*rows);

// Reset counters 
	Log("Cntrch1: " + IntToString(cntrch1));
	Log("Cntrch2: " + IntToString(cntrch2));

	cntrch1 = 0;
	cntrch2 = 0;

	if (gpuCount >= 1)
	{
		// If matlab detects a GPU, move data to gpu for processing

		mwSize const dims[2] = {rows,cols};
		mwSize const dim = 2;

		mxGPUArray *ga1;
//		const mxGPUArray *ga1;
		ga1 = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);
		short * gpuArrayCh1 = (short *) mxGPUGetData(ga1);
//		cudaMemcpy(gpuArrayCh1,&(asdfch1asdf)[0],asdfch1asdf.size()*sizeof(short),cudaMemcpyHostToDevice);
		cudaMemcpy(gpuArrayCh1,&arrch1[0],Settings.SamplesToLog*sizeof(short)/2,cudaMemcpyHostToDevice);
//		ga1 = mxGPUCreateFromMxArray(mxarrch1);

		mxGPUArray *ga2;
//		const mxGPUArray *ga2;
		ga2 = mxGPUCreateGPUArray(dim,dims,mxINT16_CLASS,mxREAL,MX_GPU_DO_NOT_INITIALIZE);
		short * gpuArrayCh2 = (short *) mxGPUGetData(ga2);
//	    cudaMemcpy(gpuArrayCh2,&(asdfch2asdf)[0],asdfch2asdf.size()*sizeof(short),cudaMemcpyHostToDevice);
	    cudaMemcpy(gpuArrayCh2,&arrch2[0],Settings.SamplesToLog*sizeof(short)/2,cudaMemcpyHostToDevice);
//		ga2 = mxGPUCreateFromMxArray(mxarrch2);

//		mexPutVariable("base","gch1",mxarrch1);
//		mexPutVariable("base","gch2",mxarrch2);
//		mxGPUDestroyGPUArray(ga1);
//		mxGPUDestroyGPUArray(ga2);



		mxArray *fromgpu1;
		fromgpu1 = mxGPUCreateMxArrayOnGPU(ga1);
		mexPutVariable("base","gch1",fromgpu1);

		mxArray *fromgpu2;
		fromgpu2 = mxGPUCreateMxArrayOnGPU(ga2);
		mexPutVariable("base","gch2",fromgpu2);

		mxDestroyArray(fromgpu1);
		mxDestroyArray(fromgpu2);
		mxGPUDestroyGPUArray(ga1);
		mxGPUDestroyGPUArray(ga2);
	
		
/*
		mxArray *fromgpu1;
		fromgpu1 = mxGPUCreateMxArrayOnGPU(asdfgpuch1asdf);
		mexPutVariable("base","gch1",fromgpu1);
		mxArray *fromgpu2;
		fromgpu2 = mxGPUCreateMxArrayOnGPU(asdfgpuch2asdf);
		mexPutVariable("base","gch2",fromgpu2);
*/
		//		mxDestroyArray(fromgpu1);
//		mxDestroyArray(fromgpu2);
//		mxGPUDestroyGPUArray(asdfgpuch1asdf);
//		mxGPUDestroyGPUArray(asdfgpuch2asdf);



	}
	
	else
	{
		// If a gpu is not detected store data in cpu memory
		mxArray *myarraych1;
		mxArray *myarraych2;
		
		myarraych1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
		myarraych2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
		
		Log("Copy to array");
		short *start_of_arptrch1 = (short *)mxGetData(myarraych1);
		short *start_of_arptrch2 = (short *)mxGetData(myarraych2);

//		memcpy(start_of_arptrch1, &(asdfch1asdf)[0], asdfch1asdf.size()*sizeof(short));
//		memcpy(start_of_arptrch2, &(asdfch2asdf)[0], asdfch2asdf.size()*sizeof(short));

		memcpy(start_of_arptrch1, &arrch1[0], Settings.SamplesToLog*sizeof(short)/2);
		memcpy(start_of_arptrch2, &arrch2[0], Settings.SamplesToLog*sizeof(short)/2);

		mexPutVariable("base","ch1d",myarraych1);
		mexPutVariable("base","ch2d",myarraych2);
		mxDestroyArray(myarraych1);
//		asdfch1asdf.clear();
		mxDestroyArray(myarraych2);
//		asdfch2asdf.clear();

	}

// Old technique for the GPU
/*
		mxArray *myarraych1;
		mxArray *myarraych2;
		int rows = Settings.FrameSize;
		int cols = asdfch1asdf.size()/rows;
		
		myarraych1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
		myarraych2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
		
		Log("Copy to array");
		short *start_of_arptrch1 = (short *)mxGetData(myarraych1);
		short *start_of_arptrch2 = (short *)mxGetData(myarraych2);
		

		memcpy(start_of_arptrch1, &(asdfch1asdf)[0], asdfch1asdf.size()*sizeof(short));
		memcpy(start_of_arptrch2, &(asdfch2asdf)[0], asdfch2asdf.size()*sizeof(short));

		mxGPUArray const *gpu1;
		mxGPUArray const *gpu2;
		gpu1 = mxGPUCreateFromMxArray(myarraych1);
		gpu2 = mxGPUCreateFromMxArray(myarraych2);

		mxArray *fromgpu1;
		mxArray *fromgpu2;

		fromgpu1 = mxGPUCreateMxArrayOnGPU(gpu1);
		fromgpu2 = mxGPUCreateMxArrayOnGPU(gpu2);
		
		mexPutVariable("base","gch1d",fromgpu1);
		mexPutVariable("base","gch2d",fromgpu2);
		mxDestroyArray(myarraych1);
		asdfch1asdf.clear();
		mxDestroyArray(myarraych2);
		asdfch2asdf.clear();
		mxGPUDestroyGPUArray(gpu1);
		mxGPUDestroyGPUArray(gpu2);
		mxDestroyArray(fromgpu1);
		mxDestroyArray(fromgpu2);

	}
	*/

// Creates flag that appears in matlab workspace when new data is avaliable.
// Once this variable appears, data is ready to analyzed. If acquiring data in
// a loop, condition data analysis on presence of this variable, and delete when
// analysis is complete.
	mxArray *b;
	b = mxCreateDoubleScalar(1);
	mexPutVariable("base","ok",b);
	mxDestroyArray(b);

//	asdfch1asdf.clear();
//	asdfch2asdf.clear();  	


    Log(std::string("Analog I/O Stopped"));
    UI->AfterStreamAutoStop();

}

//  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  ApplicationIo::Parse
//  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void ApplicationIo::doVpp()
// Outdated function for parsing a datastream using vita packet parser (VPP).
{
	/*

	Log("Opening File");
	RunTimeSW.Start();
	// Open File to read.

	VeloBuffer in;
//	in = new VeloBuffer;



    Player.FileName(Settings.File);
	Player.Start();
	Log("Playing started");
	vector<int16_T> ch1;
	vector<int16_T> ch2;
	size_t size = 1;
	while (size)
	{
		size = 	Player.PlayWithHeader(in)*sizeof(int);
//		Log(IntToString(size));
		if (!size)
			break;
	
		Log(std::string("size: " + IntToString(size)));

		Log(std::string("Full size of in in ints: ") + IntToString(in.FullSizeInInts()));

	
// Look at individual vita buffers

		VitaCursor currentVita(in,0);
		int i (0);
		unsigned short vitsize(0);
		int vitsid(0);
	
//	while (i<100)
		for (;;)
		{
			if (!currentVita.isValid())
				break;
			vitsize = currentVita.PacketSize();
			vitsid = currentVita.Sid();
			AccessDatagram<int16_T> DG(currentVita.Data());
			//		if (vitsid !=(256 || 257))
			//			return;

			if (vitsid == 256)
			{
				for (size_t j = 0; j<DG.SizeInElements(); ++j)
				{
					ch1.push_back(DG[j]);
				}
			}

			if (vitsid == 257)
			{
				for (size_t j = 0; j<DG.SizeInElements(); ++j)
				{
					ch2.push_back(DG[j]);
				}
			}

//		AccessDatagram<int16_T> DG(currentVita.Data());

			currentVita.advance();
//		++i;
//		Log("Done with vitas");
		}
	
	Log("Define array");
	mxArray *myarraych1; 
	mxArray *myarraych2; 
//	ch1.resize(ch1.size()/2);
//	ch2.resize(ch2.size()/2);
	Log("initialize array");
	int rows = Settings.FrameSize;
	int cols = ch1.size()/rows;

//	myarray = mxCreateNumericMatrix(ch1.size(),2,mxINT16_CLASS,mxREAL);
	myarraych1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
	myarraych2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
	Log("Copy to array");
	int16_T *start_of_arptrch1 = (int16_T *)mxGetData(myarraych1);
	int16_T *start_of_arptrch2 = (int16_T *)mxGetData(myarraych2);
	memcpy(start_of_arptrch1, &(ch1)[0], ch1.size()*sizeof(int16_T));
	memcpy(start_of_arptrch2, &(ch2)[0], ch2.size()*sizeof(int16_T));
	Log(IntToString(ch1.size()));
	Log(IntToString(ch2.size()));
	Log("Mex Put Variable");
	mexPutVariable("base","ch1",myarraych1);
	mexPutVariable("base","ch2",myarraych2);
    mxDestroyArray(myarraych1);
    mxDestroyArray(myarraych2);
	}

	//	mexPutVariable("base","ch",CH1);
//	double Etime = RunTimeSW.Elapsed();
//	Log(std::string("Elasped setup (S): ") + FloatToString(Etime));

*/
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Put vectors in matlab array
// Added by MPD
// This function takes the parsed data (from handle data available)
// and puts it in the matlab workspace
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void ApplicationIo::putMat(vector<short>* ch1, vector<short>* ch2)
// Outdated function for testing mexfunctions, specifically mexPutVariable.
{


	/*
	mxArray *myarraych1; 
	mxArray *myarraych2; 
	int rows = Settings.FrameSize;
	int cols = ch1->size()/rows;
	myarraych1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
	myarraych2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
	Log("Copy to array");
	int16_T *start_of_arptrch1 = (int16_T *)mxGetData(myarraych1);
	int16_T *start_of_arptrch2 = (int16_T *)mxGetData(myarraych2);
	memcpy(start_of_arptrch1, ch1, ch1->size()*sizeof(int16_T));
	memcpy(start_of_arptrch2, ch2, ch2->size()*sizeof(int16_T));
	Log(IntToString(ch1->size()));
	Log(IntToString(ch2->size()));
	Log("Mex Put Variable");
	mexPutVariable("base","ch1",myarraych1);
	mexPutVariable("base","ch2",myarraych2);
    mxDestroyArray(myarraych1);
    mxDestroyArray(myarraych2);
*/
}

//  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Channelize data
//  Added by MPD
//  Added handle OnSplitComplete.SetEvent to AppIo constructor
//  Added HandleSplitComplete method to AppIo.h/.cpp
//  +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
void ApplicationIo::ParseFile(int ohhgod)
// Outdated function to parse a binary data file written to disk

{
/*
	RunTimeSW.Start();
	InGraph.BinFile(Settings.File);
	InGraph.Restore();

    // Open input file as PacketFileDataSet
    DataSet * in = 0;

	in = new VitaPacketFileDataSet;

	in->FileName(Settings.File);
	in->Type(static_cast<DataSet::IIDataType>(DataSet::IIDataType::Int));
    in->Bits(InGraph.SignificantBits());
    in->Channels(InGraph.Channels());
    in->Devices(InGraph.Devices());
    in->Polarity(static_cast<DataSet::IIPolarity>(InGraph.Polarity()));

    in->Enabled(true);
    if (!in->Enabled())
    {
        delete in;
        return;
    }

	int devices = in->Devices()-1;
    int ChannelsPerDevice = in->Channels()/devices;


    // Force parsing of entire packet set, to insure
    // determination of all embedded packet IDs
    in->Channel(InGraph.Channels());
	Sample s = in->Element(in->Samples()-1);

	int rows = Settings.FrameSize;
	int cols = in->Samples()/rows;
	int iter = rows*cols;
	double Elapsed = RunTimeSW.Elapsed();
	Log(std::string("Elasped setup (S): ") + FloatToString(Elapsed));
	// Define functor for parallel loop

	int *testarray = new int[5];
	delete[] testarray;

	switch(ohhgod)
	{
	case 0:
		{
			Timer.Enabled(true);
			RunTimeSW.Start();
			// Try one matrix per channel
			mxArray *ch1;
			ch1 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
			int16_T *ch1mat = (int16_T *) mxGetData(ch1);
			in->Channel(0);
			double elapsed0 = RunTimeSW.Stop();
			Log(std::string("Elasped setup2 (S): ") + FloatToString(elapsed0));

			RunTimeSW.Start();

			for (int s=0; s<1; ++s)
			{
				Sample samp = in->Element(s);
//				ch1mat[s] = samp.Int();
			}
			double elapsed1 = RunTimeSW.Stop();
			Log(std::string("ch1 for loop (S): ") + FloatToString(elapsed1));

			mexPutVariable("base","ch1",ch1);
			mxDestroyArray(ch1);


			RunTimeSW.Start();
			mxArray	*ch2;
			ch2 = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
			int16_T *ch2mat = (int16_T *) mxGetData(ch2);
			in->Channel(1);
			for (int s=0; s<iter; ++s)
			{
				Sample samp = in->Element(s);
//				ch2mat[s] = samp.Int();
			}
			mexPutVariable("base","ch2",ch2);
			mxDestroyArray(ch2);
			double elapsed2 = RunTimeSW.Stop();
			Log(std::string("ch2 for loop (S): ") + FloatToString(elapsed2));
			break;
		}

	case 1:
		{
			int channel;
			mxArray *ch;
			ch = mxCreateNumericMatrix(rows,cols*2,mxINT16_CLASS,mxREAL);
			int16_T *chmat = (int16_T *) mxGetData(ch);
			
			for (int s=0; s<iter; ++s)
			{
				for (channel=0; channel< ChannelsPerDevice; ++channel)
				{
					in->Channel(channel);
					Sample samp = in->Element(s);
					chmat[s] = samp.Int();
				}
			}
			mexPutVariable("base","ch",ch);
			mxDestroyArray(ch);
			break;
		}

	case 2:
		{
			int channel;
			mxArray *ch;
			ch = mxCreateNumericMatrix(rows,cols*2,mxINT16_CLASS,mxREAL);
			int16_T *chmat = (int16_T *) mxGetData(ch);
			for (channel=0; channel<ChannelsPerDevice; ++channel)
			{
				in->Channel(channel);
				for (int s=0; s< iter; ++s)
				{
					Sample samp = in->Element(s);
					chmat[s] = samp.Int();
				}
			}
			mexPutVariable("base","CH",ch);
			mxDestroyArray(ch);
			break;
		}

	case 3:
		{
		// Try one matrix per channel
			MatlabMatrix CH1(rows,cols);
			double * Datach1 = reinterpret_cast<double*>(CH1.DataPtr());
			in->Channel(0);
			for (int s=0; s<iter; ++s)
			{
				Sample samp = in->Element(s);
				Datach1[s] = samp.Int();
			}
			mxArray* matdat = reinterpret_cast<mxArray*>(Datach1);
			
			mexPutVariable("base","ch1",matdat);
			mxDestroyArray(matdat);
		
			MatlabMatrix CH2(rows,cols,mxINT16_CLASS,mxREAL);
			int16_t *Datach2 = reinterpret_cast<int16_t *>(CH2.DataPtr());
			in->Channel(1);
			for (int s=0; s<iter; ++s)
			{
				Sample samp = in->Element(s);
				Datach2[s] = samp.Int();
			}
			mxArray* matdat1 = reinterpret_cast<mxArray*>(Datach2);
			mexPutVariable("base","ch2",matdat1);
			mxDestroyArray(matdat);
			break;
		}

	case 4:
		{
			in->ScanToEnd();
			break;
		}
		case 5:
		{
			in->Channel(0);
			size_t (*temparray) = new size_t [3000];

			// Try OMP
//			int tempiter = iter;
			mexPrintf("made vector\n");

			Sample k = in->Element(in->Samples()-1);
			int fart = 0;
			for(int fart = 0; fart<iter;++fart)
			{
				Sample samp = in->Element(fart);
				temparray[fart] = samp.Int();
			}
			
			delete temparray;
			break;
		}
	}

	in->Enabled(false);
	ProcessCompletionEvent e(0);
	OnSplitComplete.Execute(e);
	delete in;
*/
}

//---------------------------------------------------------------------------
// Function to set some of the card parameters
// Added by MPD
//---------------------------------------------------------------------------
void ApplicationIo::setParameters(const char *param, double value)
{
	if (!_strcmpi(param,"frameSize"))
	{
		Settings.FrameSize = value;
	}	

	else if (!_strcmpi(param,"sampleRate"))
	{
		Settings.SampleRate = value;
	}	
	
	else if (!_strcmpi(param,"samplesToLog"))
	{
		Settings.SamplesToLog = value;
	}	
	else if (!_strcmpi(param,"clock"))
	{
		Settings.ReferenceClockSource = value;
	}

}

//---------------------------------------------------------------------------
// Test function for mxCreateNumericArray
//---------------------------------------------------------------------------
void ApplicationIo::testArray()
// Test function for playing with mxArrays in C. (unused)
{
	int rows = 10;
	int cols = 2;
	mxArray *matr;
	matr = mxCreateNumericMatrix(rows,cols,mxINT16_CLASS,mxREAL);
	int16_T *matrxx = (int16_T *) mxGetData(matr);
//	matrxx = mxGetPr(matr);
	const int data[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22};
	for (int sample = 0; sample < 10; ++sample)
	{
		for (int channel = 0; channel <2; ++channel)
		{
			matrxx[rows*channel+sample] = data[channel+sample];
		}
	}
	mexPutVariable("base","test",matr);
	mxDestroyArray(matr);
}



//---------------------------------------------------------------------------
//  ApplicationIo::HandleTimer() --  Per-second status timer event
//---------------------------------------------------------------------------

void ApplicationIo::HandleTimer(OpenWire::NotifyEvent & /*Event*/)
{
//    unsigned int DigIn = DioData();

    // Display status
    UI->PeriodicStatus();

//    DioData(~DigIn);
    Trig.AtTimerTick();
}

//---------------------------------------------------------------------------
//  ApplicationIo::VitaPostProcess() --  Post processing for Vita packets
//---------------------------------------------------------------------------
// Unused function for post-processing vita packets
void  ApplicationIo::VitaPostProcess()
{
}

//---------------------------------------------------------------------------
//  ApplicationIo::Log() --  Log message thunked to main thread
//---------------------------------------------------------------------------

void  ApplicationIo::Log(const std::string & msg)
{
    ProcessStatusEvent e(msg);
    OnLog.Execute(e);
	mexPrintf("%s\n",msg.c_str());
}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleOnLog() --
//---------------------------------------------------------------------------

void ApplicationIo::HandleOnLog(Innovative::ProcessStatusEvent & Event)
{
    UI->Log(Event.Message);
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  Support Functions
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

//---------------------------------------------------------------------------
//  ApplicationIo::IsDataLoggingCompleted() --check if done logging
//---------------------------------------------------------------------------

bool  ApplicationIo::IsDataLoggingCompleted()
{
    if (WordsToLog==0)
        return false;
    else
        return FWordCount >= WordsToLog;
}

bool ApplicationIo::DLC()
{
	if (WordsToLog==0)
		return false;
	else
		return FWordCount >= WordsToLog;
}

//---------------------------------------------------------------------------
//  ApplicationIo::SoftwareAlert() --  Issue specified software alert
//---------------------------------------------------------------------------

void ApplicationIo::SoftwareAlert(unsigned int value)
{
    Log(std::string("Posting SW alert..."));
    Module().Alerts().SoftwareAlert(value);
}

//---------------------------------------------------------------------------
//  ApplicationIo::Temperature() --  Query current module temperature
//---------------------------------------------------------------------------

int ApplicationIo::Temperature() 
{
    return Module().Thermal().LogicTemperature();
}

//---------------------------------------------------------------------------
//  ApplicationIo::PllLocked() --  Query current PLL lock status
//---------------------------------------------------------------------------

bool  ApplicationIo::PllLocked()
{
    return Module().Clock().Locked();
}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleDisableTrigger() --  Trigger Manager Trig OFF
//------------------------------------------------------------------------------

void  ApplicationIo::HandleDisableTrigger(OpenWire::NotifyEvent & /*Event*/)
{
    //  Module.SetInputSoftwareTrigger(false);   
	//  all done in library?
    Module().Input().Trigger().External(false);
}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleSoftwareTrigger() --  Trigger Manager Trig OFF
//------------------------------------------------------------------------------

void  ApplicationIo::HandleSoftwareTrigger(OpenWire::NotifyEvent & /*Event*/)
{
    Module.SetInputSoftwareTrigger(true);
}

//---------------------------------------------------------------------------
//  ApplicationIo::HandleExternalTrigger() --  Enable External trigger
//------------------------------------------------------------------------------

void  ApplicationIo::HandleExternalTrigger(OpenWire::NotifyEvent & /*Event*/)
{
    Module().Input().Trigger().External(true);
}

//------------------------------------------------------------------------------
//  ApplicationIo::DisplayLogicVersion() --  Log version info
//------------------------------------------------------------------------------

void  ApplicationIo::DisplayLogicVersion()
{
    std::stringstream msg;
    msg << std::hex << "Logic Version: " << Module().Info().FpgaLogicVersion()
        << ", Hdw Variant: " << Module().Info().FpgaHardwareVariant()
        << ", Revision: " << Module().Info().PciLogicRevision()
        << ", Subrevision: " << Module().Info().FpgaLogicSubrevision();
	Log(msg.str());

    std::stringstream msg2;
    msg2 << std::hex << "Board Family: " << Module().Info().PciLogicFamily()
        << ", Type: " << Module().Info().PciLogicType()
        << ", Board Revision: " << Module().Info().PciLogicPcb()
        << ", Chip: " << Module().Info().FpgaChipType();
    Log(msg2.str());

    
    msg.str("");
  	msg << "PCI Express Lanes: " << Module().Debug()->LaneCount();
  	if (Module().Debug()->IsGen2Capable())
  		msg << " Gen 2";
  	else
  		msg << " Gen 1 only";
  	Log(msg.str());
}

//---------------------------------------------------------------------------
//  ApplicationIo::InitBddFile() --  Set up BDD File for an output file
//---------------------------------------------------------------------------

void ApplicationIo::InitBddFile(BinView & graph)
{
    PathSpec spec(graph.BinFile());
    spec.Ext(".bdd");
    if (FileExists(spec.Full()) && !Settings.OverwriteBdd)
        return;


    remove(spec.Full().c_str());

    // Make a new BDD
    const int bits = Module().Input().Info().Bits();

    const int limit = 1 << (bits-1);

    graph.Time().LowerLimit(-limit);
    graph.Time().UpperLimit(limit);
    graph.Time().Break(1);
    graph.Time().SeamSize((Module().Velo().VeloPacketSize(0)-4)*sizeof(int));
    graph.Time().AnnotateSeams(true);
    
    graph.Fft().LowerLimit(-150);
    graph.Fft().ScaleYType(BinViewOptions::FftOptions::sLog);
    graph.Fft().ScaleXType(BinViewOptions::FftOptions::sLog);
    graph.Fft().CriteriaThreshold(75);
    int pktsize = std::min(Settings.PacketSize, 0x10000);
    BinViewOptions::FftOptions::IIPoints pts(static_cast<BinViewOptions::FftOptions::IIPoints>(
        TrailingZeroCount(pktsize)-7));
    graph.Fft().Points(pts);
    graph.Fft().Window(BinViewOptions::FftOptions::wBlackmann);
    graph.Fft().Average(1);
    
    graph.Text().DataFormat("%11.3f");
    graph.System().UpdateRate(BinViewOptions::SystemOptions::ms1000);
//    graph.System().ServerSlotName("BinView");
    graph.Leap(Settings.PacketSize);
    graph.SignificantBits(bits);
    graph.Polarity(Module().Input().Info().Span().IsUnipolar() ? BinView::pUnsigned : BinView::pSigned);
    graph.DataSpan(300);
    graph.DataType(BinView::tInt);
    graph.Units("mV");
    graph.Samples(1000);
    graph.SampleRate(static_cast<float>(Module().Clock().FrequencyActual() / 1.0e3f));

	

    graph.Channels(Module().Input().ActiveChannels());

    graph.Devices(Module().Input().ActiveChannels());

    graph.System().Source(BinViewOptions::SystemOptions::sVita);
    graph.NullDataValue(.12345e-19f);
    graph.InputSpan((float)Module().Input().Info().Span().Delta());
    graph.ScalingEnabled(true);

    //  Board Specific Modifications
    Module.ModifyBddFile(graph);

    graph.Save();
}

//---------------------------------------------------------------------------
//  ApplicationIo::WriteRom() --  Write rom using relevant settings
//---------------------------------------------------------------------------

void ApplicationIo::WriteRom()
{
    //  System Page Operations
    Module().IdRom().System().Name(Settings.ModuleName);
    Module().IdRom().System().Revision(Settings.ModuleRevision);
    Module().IdRom().System().Updated(true);  // T if data has been loaded
    Module().IdRom().System().StoreToRom();

    //  AdcCal Page Operations
    for (unsigned int ch = 0; ch < Channels(); ++ch)
        {
			Module().Input().Cal().Gain(ch, Settings.Gain[ch]);
			Module().Input().Cal().Offset(ch, Settings.Offset[ch]);
        }

    Module().Input().Cal().Calibrated(Settings.Calibrated);
    Module().IdRom().AdcCal().StoreToRom();
// test
}

//---------------------------------------------------------------------------
//  ApplicationIo::ReadRom() --  Read rom and update relevant settings
//---------------------------------------------------------------------------

void ApplicationIo::ReadRom()
{
    //  System Page Operations
    Module().IdRom().System().LoadFromRom();

    Settings.ModuleName = Module().IdRom().System().Name();
    Settings.ModuleRevision = Module().IdRom().System().Revision();
    //  Can use 'Updated' to check if data valid

    Module().IdRom().AdcCal().LoadFromRom();

    for (unsigned int ch = 0; ch < Channels(); ++ch)
        {
			Settings.Gain[ch] = Module().Input().Cal().Gain(ch);
			Settings.Offset[ch] = Module().Input().Cal().Offset(ch);
        }

    Settings.Calibrated = Module().Input().Cal().Calibrated();
}

//---------------------------------------------------------------------------
//  ApplicationIo::FillLogs() -- Fill Busmaster Logs from trace info
//---------------------------------------------------------------------------

void  ApplicationIo::FillLogs()
{
    // Fill Logs vector from Log
    Module().Debug()->DumpLog(Settings.Log.LogData);

    //  Fill ISR vector
    Module().Debug()->DumpIsrStatus(Settings.Log.IsrStatusData);

}

//---------------------------------------------------------------------------
//  ApplicationIo::ClockInfo() -- Dump clock info
//---------------------------------------------------------------------------

void  ApplicationIo::ClockInfo()
{
    Log("Clock output data");
    for (unsigned int i=0; i<Module().RawClockDevice().Pll().Outputs(); i++)
        {
        std::stringstream msg;
        msg << std::hex << "Out # " << i << ": "
            << " Divider: "   << Module().RawClockDevice().Pll().OutputDivider(i)
            << ", Out Freq: " << Module().RawClockDevice().Pll().OutputFrequency(i)
            << ", Actual: "   << Module().RawClockDevice().Pll().OutputFrequencyActual(i) ;
        Log(msg.str());
        }

}

//==============================================================================
//  CLASS ApplicationSettings
//==============================================================================
//------------------------------------------------------------------------
//  ApplicationSettings::ApplicationSettings() --  Ctor, load from INI file
//------------------------------------------------------------------------

ApplicationSettings::ApplicationSettings()
    : IniSaver("C:\\Users\\drdoofus\\Desktop\\tempdata\\Settings.ini",false,true), 
	ActiveChannels(AnalogInChannels()),
	AlertEnable(AnalogInAlerts(), false),
	Gain(AnalogInChannels()),
	Offset(AnalogInChannels())


{
	// Parsing
    Install( ToIni("File",       File, std::string("C:\\Users\\drdoofus\\Desktop\\tempdata\\Data.bin")) );
    Install( ToIni("View",       View, false) );
    Install( ToIni("FileFormat", FileFormat, 0) );

    //  Board Settings
    Install( ToIni("Target",                    Target,          0)  );
    Install( ToIni("BMSize",                    BusmasterSize,   16)  );
    //Install( ToIni("LogicFailureTemperature",   LogicFailureTemperature,   85.0f)  );

    //  Config Data
    Install( ToIni("ActiveChannels", "Ch", ActiveChannels,    char(11) ));
    Install( ToIni("PacketSize",           PacketSize,        0x10000)  );
    Install( ToIni("SampleClockSource",    SampleClockSource, 1)  );
    Install( ToIni("SampleRate",           SampleRate,        static_cast<float>(MaxInRateMHz()))  );
    Install( ToIni("ExtClockSrcSelection", ExtClockSrcSelection, 0)  );
    Install( ToIni("ReferenceClockSource", ReferenceClockSource, 1)  );
    Install( ToIni("ReferenceRate",        ReferenceRate,       10.0f)  );
    Install( ToIni("TestCounterEnable",    TestCounterEnable,   false)  );
    Install( ToIni("TestGenMode",          TestGenMode,         0)  );
    Install( ToIni("AlertEnable", "Alert", AlertEnable,         char(0) ));
    Install( ToIni("DecimationEnable",     DecimationEnable,    false)  );
    Install( ToIni("DecimationFactor",     DecimationFactor,    1)  );

    //  Trigger
    Install( ToIni("ExternalTrigger",        ExternalTrigger,     0)  );
    Install( ToIni("EdgeTrigger",            EdgeTrigger,         0)  );
    Install( ToIni("Framed",                 Framed,              0)  );
    Install( ToIni("FrameSize",              FrameSize,           0x4000)  );
    Install( ToIni("ExtTriggerSrcSelection", ExtTriggerSrcSelection, 0)  );
    Install( ToIni("PulseEnable",            Pulse.Enable,         false)  );
    Install( ToIni("PulsePeriod",            Pulse.Period,         10.0e6f)  );
    Install( ToIni("PulseDelay",             Pulse.Delay,          0.0f)  );
    Install( ToIni("PulseWidth",             Pulse.Width,          1.0e6f)  );
    Install( ToIni("Pulse2Delay",            Pulse.Delay_2,          0.0f)  );
    Install( ToIni("Pulse2Width",            Pulse.Width_2,          0.0f)  );
    Install( ToIni("TriggerDelay",           DelayedTriggerPeriod,     2)  );

    //  Streaming
    Install( ToIni("LoggerEnable",           LoggerEnable,                true)  );
    Install( ToIni("PlotEnable",             PlotEnable,                  false)  );
    Install( ToIni("OverwriteBdd",           OverwriteBdd,                true)  );
    Install( ToIni("SamplesToLog",           SamplesToLog,                (ii64)0100000u)  );
    Install( ToIni("AutoStop",               AutoStop,                    true)  );
    Install( ToIni("ForcePacketsize",        ForcePacketSize,             false)  );

    Install( ToIni("Help",             Help,  true) );

    Install( ToIni("Debug Script",     DebugScript,  std::string("")) );

    Load();

    //  Sanity Check on Target
    if (Target < 0)
        Target = 0;

    //  Sanity Check on Active Channels
    //   (Make sure at least one active channel)
    // Insure at least one active channel, by default
    int count = 0;
    for (unsigned int i = 0; i < ActiveChannels.size(); ++i)
		count += ActiveChannels[i];
    ActiveChannels[0] = !count ? true : ActiveChannels[0];

}

//------------------------------------------------------------------------
//  ApplicationSettings::~ApplicationSettings() --  Dtor, save to INI file
//------------------------------------------------------------------------

ApplicationSettings::~ApplicationSettings()
{

    //  Sanity Check on Target
    if (Target < 0)
        Target = 0;

    Save();
}



