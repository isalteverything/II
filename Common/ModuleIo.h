// ModuleIo.h
//
// Board-specific module operations

#ifndef ModuleIoH
#define ModuleIoH

#include <X6_1000M_Mb.h>
#include <Ficl/FiclSystem_Mb.h>
#include <DataLogger_Mb.h>
#include <BinView_Mb.h>

//===========================================================================
//  Information Functions
//===========================================================================
//  NOTE - these are often used in constructors so cannot depend on objects
//         without due care taken
//
//   General Info
inline std::string ModuleNameStr()
            {  return  "X6-1000M R2-in PCIe Module";  }
//
//   Analog In Info
inline int    AnalogInChannels()
          {  return 2;  }
inline int    AnalogInAlerts()
          {  return 6;  }
inline float  MaxInRateMHz()
          {  return 1000.0;  }     // In MHz

//
//   Analog Out Info

inline int    AnalogOutChannels()
        {  return 0;  }

//
//   Application Feature Presence  -- used to hide GUI Portions

inline bool   HasClockMux()
        {  return true; }
inline bool   HasProgrammableReference()
        {  return true; }
inline bool   HasTestModeControl()
        {  return true; }
inline bool   HasFiclSupport()
        {  return true; }
inline bool   HasLowSpeedAnalogIn()
        {  return true; }
inline bool   HasExtClockSrcSelectMux()
        {  return true; }
inline bool   HasExtTriggerSrcSelectMux()
        {  return true; }
inline bool   HasPulseTrigger()
        {  return true; }


typedef std::vector<std::string> StringArray;
StringArray  ExtClockSrcSelectStrings();
StringArray  ExtTriggerSrcSelectStrings();
StringArray  TestModeStrings();

StringArray  AlertStrings();

//===========================================================================
//  CLASS ModuleIo  -- Hardware Access and Application Io Class
//===========================================================================

class ModuleIo
{
public:
    ModuleIo();
    ~ModuleIo();

    //  Module Aliases
    Innovative::X6_1000M_R2in & operator() ()
        {  return Module;  }
    Innovative::X6_1000M_R2in & Ref()
        {  return Module;  }
    Innovative::IFiclTarget *  FiclTarget()
            {   return &Module;  }

    OpenWire::ThunkedEventHandler<Innovative::ProcessStatusEvent>  OnLog;

    //
    //  App System Methods
    void  FiclConnectTo(Innovative::FiclSystem & ficl);
    std::string Help() const;

    //
    //  Module Methods
    void  HookAlerts();

    void  PreOpen();

    void  ConfigureGraphs(unsigned int idx,
                          Innovative::DataLogger & logr, Innovative::BinView & bv);
    void  ModifyBddFile(Innovative::BinView & bv)  // Board specific BDD changes
            {}

    void  ConfigureAlerts(std::vector<char> & AlertEnable);
    void  SetInputPacketDataSize(unsigned int size);
    void  SetInputSoftwareTrigger(bool state);
    void  SetTestConfiguration( bool enable, int mode_idx );
    typedef std::vector<float> PulseSettingArray;
    void  SetPulseTriggerConfiguration(bool enable, float period,
                                 const PulseSettingArray & Delays,
                                 const PulseSettingArray & Widths );

protected:
    void  Log(const std::string & msg);

private:
    Innovative::X6_1000M_R2in  Module;

    void  HandleTimestampRolloverAlert(Innovative::AlertSignalEvent & event);
    void  HandleSoftwareAlert(Innovative::AlertSignalEvent & event);
    void  HandleWarningTempAlert(Innovative::AlertSignalEvent & event);
    void  HandleInputFifoOverrunAlert(Innovative::AlertSignalEvent & event);
    void  HandleTriggerAlert(Innovative::AlertSignalEvent & event);
    void  HandleInputOverrangeAlert(Innovative::AlertSignalEvent & event);

    std::string Elapsed(size_t timestamp);

};
#endif
