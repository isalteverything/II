// ApplicationIo.h
//
// Board-specific data flow and hardware I/O

#ifndef ApplicationIoH
#define ApplicationIoH

#include "ModuleIo.h"
#include <ProcessEvents_Mb.h>
#include <VitaPacketStream_Mb.h>
#include <PacketStream_Mb.h>
#include <StopWatch_Mb.h>
#include <DataLogger_Mb.h>
#include <IniFile_Mb.h>
#include <BinView_Mb.h>
#include <Script_Mb.h>
#include <fstream>
#include <Application/IniSaver_App.h>

#include <Benchmark_Mb.h>
#include <SoftwareTimer_Mb.h>
#include <DataPlayer_Mb.h>

#include <Application/TriggerManager_App.h>
#include <Application/BinviewPlotter_App.h>
#include <Application/IniSaver_App.h>
#include <Application/FiclIo_App.h>
#include <Exception_Mb.h>
#include <matrix.h>
#include "mxGPUArray.h"
#include <mex.h>
class ApplicationIo;

// -----------------------------------
// Class ProcessinThread - For Parsing
// Added 6/2/2014 mpd for parsing
// -----------------------------------
/*
class ProcessingThread : public Innovative::Thread
{
typedef Innovative::Thread inherited;

public:
	// Ctors
	ProcessingThread(ApplicationIo * owner);

	// Member Fcns
	void Process(const std::string & filename, bool plot);

protected:
	// Initialize signalling objects
	virtual void Init();
	virtual void Execute();

private:
	// Fields
	ApplicationIo       *Owner;
	std::string         FileName;
	bool                Plot;

	// Data
	enum { sWork=inherited::sLast, sLast};
	Innovative::Event   eWork;
};
*/

//==============================================================================
//  CLASS ApplicationSettings
//==============================================================================

class ApplicationSettings  :  public Innovative::IniSaver
{
public:

    typedef std::vector<char>   BoolArray;
    typedef std::vector<int>    IntArray;
    typedef std::vector<float>  FloatArray;

    // Ctor
    ApplicationSettings();
    ~ApplicationSettings();

    //
    //  Board Settings
    int             Target;
    int             BusmasterSize;
    //float           LogicFailureTemperature;


	// Added for parsing mpd 
	bool            View;
    std::string     File;
    int             FileFormat;

	//  Config Data
    BoolArray       ActiveChannels;
    int             PacketSize;
    int             SampleClockSource;
    float           SampleRate;
    int             ExtClockSrcSelection;
    int             ReferenceClockSource;
    float           ReferenceRate;
    bool            TestCounterEnable;
    int             TestGenMode;
    BoolArray       AlertEnable;
    bool            DecimationEnable;
    int             DecimationFactor;

    //  Trigger
    int             ExternalTrigger;
    int             EdgeTrigger;
    int             Framed;
    int             FrameSize;
    int             ExtTriggerSrcSelection;
    int             DelayedTriggerPeriod;


    struct PulseD
    {
        bool   Enable;
        float  Period;
        float  Delay;
        float  Width;
        float  Delay_2;
        float  Width_2;
    };
    PulseD          Pulse;

    //  Logging
    bool            LoggerEnable;
    bool            PlotEnable;
    ii64            SamplesToLog;
    bool            OverwriteBdd;
    bool            AutoStop;
    bool            ForcePacketSize;

    //  Log Page Data
    struct LogD
    {
        std::vector<int> LogData;
        std::vector<int> IsrStatusData;
        std::vector<int> VppData;
    };

    LogD    Log;
    //  Eeprom
    FloatArray      Gain;
    FloatArray      Offset;
    float           PllCorrection;
    bool            Calibrated;

    bool            Help;
    std::string     ModuleName;
    std::string     ModuleRevision;
    bool            InitDdcs;

    int             Divider;
	std::string     Path;

    std::string     DebugScript;

private:

	// Data -- added for parsing
	Innovative::IniFile *      Ini;
	std::string                Root;
};

//===========================================================================
//  CLASS IUserInteface  -- Interface for callbacks to UI from App Logic
//===========================================================================

class IUserInterface : public IFicl_UserInterface
{
public:
    virtual ~IUserInterface()  {  }

	// Added for parsing
	virtual void UpdateProgress(int percent) = 0;

    virtual void  Log(const std::string & a_string) = 0;
    virtual void  GetSettings() = 0;
    virtual void  SetSettings() = 0;
    virtual void  AfterStreamAutoStop() = 0;
    virtual void  Warning(const std::string & a_string) = 0;
    virtual void  PeriodicStatus() = 0;
    virtual void  DisplayHelp() = 0;
};

//===========================================================================
//  CLASS ApplicationIo  -- Hardware Access and Application Io Class
//===========================================================================

class ApplicationIo : public FiclIo
{
    friend class ApplicationSettings;
//	friend class ProcessingThread;
    typedef std::vector<__int64>    IntArray;

public:
    //
    //  Member Functions
    ApplicationIo(IUserInterface * ui);
    ~ApplicationIo();

    ModuleIo &  ModIo()
        {  return Module;  }

    unsigned int BoardCount();
    void Open();
    bool IsOpen()
        {  return Opened;  }
    void Close();
    void StartStreaming();
    void StopStreaming();
    bool IsStreaming()
        {  return Timer.Enabled();  }

    void WriteRom();
    void ReadRom();

    void SoftwareAlert(unsigned int value);
    int  Temperature();
    bool PllLocked();

    void  VitaPostProcess();

    double BlockRate() const
        {  return FBlockRate;  }
    unsigned int     Channels() const
        {  return AnalogInChannels();  }

    ii64    SampleCount() const
        {  return FWordCount*SamplesPerWord;  }

    void    ExecuteDebugScript(const std::string & cmd)
        {  Execute(std::string("load ") + cmd);  }
    void    Help()
        {  Execute(Module.Help());  }
    unsigned int Peek(unsigned int addr)
        {  return Module().PeekLogic(addr);    }
    void Poke(unsigned int addr, unsigned int value)
        {  Module().PokeLogic(addr, value);  }

    void  FillLogs();
    unsigned int ExtractXPak(unsigned int isr_status)
        {   return (isr_status>>4)&0x3F;   }
    unsigned int ExtractRPak(unsigned int isr_status)
        {   return (isr_status>>10)&0x3F;   }       

    void ClockInfo();
	void ParseFile(int ohhgod);
	void doVpp();
	void testArray();
	bool DLC();
	void putMat(vector<short> *ch1, vector<short> *ch2);
	void setParameters(const char *param, double value);


    
public: // Pseudo-protected
    // Data
    ApplicationSettings     Settings;

	// Parsing
//	void Extract();

private:
    //  Random test comment
    //  Member Data
    ModuleIo                            Module;
    IUserInterface *                    UI;
    Innovative::VitaPacketStream        Stream;
    Innovative::TriggerManager          Trig;
    Innovative::SoftwareTimer           Timer;
    Innovative::StopWatch               RunTimeSW;
    Innovative::DataLogger              Logger;
    Innovative::BinviewPlotter          RtPlot;
    Innovative::BinView                 Graph;
    Innovative::BinView                 InGraph;
    Innovative::BinView                 OutGraph;
	Innovative::VitaPacketParser        Vpp;
	Innovative::DataPlayer              Player;
	short                               * arrch1;
	short                               * arrch2;
	mxArray                             * mxarrch1;
	mxArray                             * mxarrch2;
//	void *                              mxptrch1;
//	short *                              mxptrch2;
//	std::vector<short>                  asdfch1asdf;
//	std::vector<short>                  asdfch2asdf;
//	mxGPUArray                          *asdfgpuch1asdf;
//  mxGPUArray                          *asdfgpuch2asdf;
//	short                               *agpuch1a;
//	short                               *agpuch2a;
	int                                 cntrch1;
	int                                 cntrch2;
	int                                 gpuCount;

	// Data
	int                                 Channel;
	int                                 Device;
	int                                 Bits;
	int                                 DataFormat;

    //  App State variables
    bool                                Opened;
    bool                                StreamConnected;
    bool                                Stopped;
    //  App Status variables
    double                              FBlockRate;
    ii64                                FWordCount;
    int                                 SamplesPerWord;
    ii64                                WordsToLog;

    Innovative::AveragedRate            Time;
    Innovative::AveragedRate            BytesPerBlock;

//    Innovative::BinviewPlotter          Plot;

    //
    //  Member Functions
    void  HandleDataAvailable(Innovative::VitaPacketStreamDataEvent & Event);
    void  Handle_VPP_ImageAvailable(Innovative::VitaPacketParserImageAvailable & Event);

    void  HandleBeforeStreamStart(OpenWire::NotifyEvent & Event);
    void  HandleAfterStreamStart(OpenWire::NotifyEvent & Event);
    void  HandleAfterStreamStop(OpenWire::NotifyEvent & Event);

    void  HandleAfterStop(OpenWire::NotifyEvent & Event);

    void  HandleScriptCommand(Innovative::ProcessStatusEvent & Event);
    void  HandleScriptMessage(Innovative::ProcessStatusEvent & Event);
    void  HandleTimer(OpenWire::NotifyEvent & Event);
    void  HandleOnLog(Innovative::ProcessStatusEvent & Event);

    void  HandleDisableTrigger(OpenWire::NotifyEvent & Event);
    void  HandleExternalTrigger(OpenWire::NotifyEvent & Event);
    void  HandleSoftwareTrigger(OpenWire::NotifyEvent & Event);

	OpenWire::ThunkedEventHandler<Innovative::ProcessCompletionEvent> OnSplitComplete;
	OpenWire::ThunkedEventHandler<Innovative::ProcessProgressEvent> OnSplitProgress;
	void HandleSplitComplete(Innovative::ProcessCompletionEvent & Event);
	void HandleSplitProgress(Innovative::ProcessProgressEvent & Event);


	

    void  Log(const std::string & msg);

    OpenWire::ThunkedEventHandler<Innovative::ProcessStatusEvent>  OnLog;

    void  DoLogicStore();
    void  DoLogicFetch();
    void  DoPortStore();
    void  DoPortFetch();
    void  DoDelay();

    bool  IsDataLoggingCompleted();
    void  InitBddFile(Innovative::BinView & graph);

    void  DisplayLogicVersion();
    void  TallyBlock(size_t bytes);
};


#endif
