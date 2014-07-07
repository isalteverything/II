// ModuleIo.h
//
// Board-specific module operations

#include "ModuleIo.h"

using namespace Innovative;
using namespace std;

//---------------------------------------------------------------------------
//  TestModeStrings() -- return vector of strings for Test Mode Listbox
//---------------------------------------------------------------------------

std::vector<std::string>  TestModeStrings()
{
    std::vector<std::string>  List;

    List.push_back( std::string("Sawtooth") );
    List.push_back( std::string("Saw (Paced)") );

    return List;
}

//---------------------------------------------------------------------------
//  ExtClockSrcSelectStrings() -- return strings for Ext Src Select Listbox
//---------------------------------------------------------------------------

std::vector<std::string>  ExtClockSrcSelectStrings()
{
    std::vector<std::string>  List;

    List.push_back( std::string("Front Panel") );
    List.push_back( std::string("P16") );

    return List;
}

//---------------------------------------------------------------------------
//  ExtTriggerSrcSelectStrings() -- return strings for Trigger Src Select LB
//---------------------------------------------------------------------------

std::vector<std::string>  ExtTriggerSrcSelectStrings()
{
    std::vector<std::string>  List;

    List.push_back( std::string("Front Panel") );
    List.push_back( std::string("P16") );

    return List;
}

//===========================================================================
//  CLASS ModuleIo  -- Hardware Access and Application Io Class
//===========================================================================
//---------------------------------------------------------------------------
//  constructor for class ModuleIo
//---------------------------------------------------------------------------

ModuleIo::ModuleIo()
{
}

//---------------------------------------------------------------------------
//  destructor for class ModuleIo
//---------------------------------------------------------------------------

ModuleIo::~ModuleIo()
{
}

//---------------------------------------------------------------------------
//  ModuleIo::FiclConnectTo() --  Connect to FICL system
//---------------------------------------------------------------------------

void  ModuleIo::FiclConnectTo(FiclSystem & ficl)
{
    // do nothing if FICL is not supported by the board

    //  Attach board specific commands to FICL
    ficl.ConnectTarget(FiclTarget());
}

//---------------------------------------------------------------------------
//  ModuleIo::Log() --  Log message thunked to owner
//---------------------------------------------------------------------------

void  ModuleIo::Log(const std::string & msg)
{
    ProcessStatusEvent e(msg);
    OnLog.Execute(e);
}

//---------------------------------------------------------------------------
//  ModuleIo::Help() --  module-specific help command
//---------------------------------------------------------------------------

std::string ModuleIo::Help() const
{
    return "cr ?system cr ?X6-400M";
}

//---------------------------------------------------------------------------
//  ModuleIo::ConfigureGraphs() --  Configure Loggers and Graphs
//---------------------------------------------------------------------------

void  ModuleIo::ConfigureGraphs(unsigned int /*idx*/, Innovative::DataLogger & /*logr*/,
                                   Innovative::BinView & graph)
{

    graph.SampleRate(static_cast<float>(Module.Clock().FrequencyActual() / 1.0e3f));

	const int bits = Module.Input().Info().Bits();

	const int limit = 1 << (bits-1);

	graph.Time().LowerLimit(-limit);
	graph.Time().UpperLimit(limit);
	graph.Time().Break(1);

	graph.Time().SeamSize(1024);           // seams don't mean much
    graph.Time().AnnotateSeams(false);

    graph.Fft().LowerLimit(-150);
    graph.Fft().ScaleYType(BinViewOptions::FftOptions::sLog);
    graph.Fft().ScaleXType(BinViewOptions::FftOptions::sLog);
    graph.Fft().CriteriaThreshold(75);

    graph.Fft().Points( BinViewOptions::FftOptions::p16384);

    graph.Fft().Window(BinViewOptions::FftOptions::wBlackmann);
    graph.Fft().Average(1);
    graph.Text().DataFormat("%11.3f");
    graph.System().UpdateRate(BinViewOptions::SystemOptions::ms1000);

    graph.Leap(1024);
    graph.SignificantBits(bits);

    graph.Polarity(BinView::pSigned);

    graph.DataSpan(300);
    graph.DataType(BinView::tInt);
    graph.Units("mV");
    graph.Samples(1000);

    // One channel per file
    graph.Channels(1);
    graph.Devices(1);

    graph.System().Source(BinViewOptions::SystemOptions::sPacketFile);
    graph.NullDataValue(.12345e-19f);

    SpanInfo span = Module.Input().Info().Span();
    graph.InputSpan(2000);
    graph.ScalingEnabled(true);


}

//---------------------------------------------------------------------------
//  ModuleIo::SetInputSoftwareTrigger() --  Set or Clear Software Trigger
//---------------------------------------------------------------------------

void  ModuleIo::SetInputSoftwareTrigger(bool state)
{
    Module.Input().SoftwareTrigger(state);
}

//---------------------------------------------------------------------------
//  ModuleIo::SetTestConfiguration() --  Configure Test Setup
//---------------------------------------------------------------------------

void  ModuleIo::SetTestConfiguration( bool enable, int mode_idx )
{
    // Optionally enable ramp generator
    Module.Input().TestModeEnabled(enable, mode_idx);
}

//---------------------------------------------------------------------------
//  ModuleIo::SetInputPacketDataSize() --  Set Velocia packet data size
//---------------------------------------------------------------------------

void  ModuleIo::SetInputPacketDataSize( unsigned int data_size )
{
    // Load all velo stream sizes to the our data size
    Module.Velo().LoadAll_VeloDataSize(data_size);
}

//---------------------------------------------------------------------------
//  AlertStrings() -- return vector of strings for Alert Listbox
//---------------------------------------------------------------------------

std::vector<std::string>  AlertStrings()
{
    std::vector<std::string>  List;

    List.push_back( std::string("Timestamp") );
    List.push_back( std::string("Software") );
    List.push_back( std::string("Temperature") );
    List.push_back( std::string("Fifo Overflow") );
    List.push_back( std::string("Trigger") );
    List.push_back( std::string("Input Overrange") );

    return List;
}

//---------------------------------------------------------------------------
//  ModuleIo::HookAlerts() --  Hook Alerts
//---------------------------------------------------------------------------

void  ModuleIo::HookAlerts()
{
    Module.Alerts().OnTimeStampRolloverAlert.SetEvent(this, &ModuleIo::HandleTimestampRolloverAlert);
    Module.Alerts().OnSoftwareAlert.SetEvent(this, &ModuleIo::HandleSoftwareAlert);
    Module.Alerts().OnWarningTemperature.SetEvent(this, &ModuleIo::HandleWarningTempAlert);
    Module.Alerts().OnInputOverflow.SetEvent(this, &ModuleIo::HandleInputFifoOverrunAlert);
    Module.Alerts().OnTrigger.SetEvent(this, &ModuleIo::HandleTriggerAlert);
    Module.Alerts().OnInputOverrange.SetEvent(this, &ModuleIo::HandleInputOverrangeAlert);
}

//---------------------------------------------------------------------------
//  ModuleIo::ConfigureAlerts() --  Configure Alerts
//---------------------------------------------------------------------------

void  ModuleIo::ConfigureAlerts(std::vector<char> & AlertEnable)
{
    enum IUsesX6Alerts::AlertType Alert[] = {
        IUsesX6Alerts::alertTimeStampRollover, IUsesX6Alerts::alertSoftware,
        IUsesX6Alerts::alertWarningTemperature,
        IUsesX6Alerts::alertInputOverflow,
        IUsesX6Alerts::alertTrigger, IUsesX6Alerts::alertInputOverrange };

    for (unsigned int i = 0; i < AlertEnable.size(); ++i)
        Module.Alerts().AlertEnable(Alert[i], AlertEnable[i] ? true : false);
}

//---------------------------------------------------------------------------
//  ModuleIo::SetPulseTriggerConfiguration() --  Configure Pulse Trigger
//---------------------------------------------------------------------------

void  ModuleIo::SetPulseTriggerConfiguration(bool enable, float period,
                                const PulseSettingArray & Delays, const PulseSettingArray & Widths )
{
    // Optionally enable ramp generator
    Module.Input().Pulse().Reset();
    Module.Input().Pulse().Enabled(enable);

    for (unsigned int i=0; i<Delays.size(); i++)
        {
        Module.Input().Pulse().AddEvent(static_cast<unsigned int>(period),
                                        static_cast<unsigned int>(Delays[i]),
                                        static_cast<unsigned int>(Widths[i]));
        }
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//  Alert Handlers
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//------------------------------------------------------------------------------
//  ModuleIo::HandleTimestampRolloverAlert() --
//------------------------------------------------------------------------------

void  ModuleIo::HandleTimestampRolloverAlert(Innovative::AlertSignalEvent & event)
{
    std::stringstream msg;
    msg << "Time stamp rollover 0x" << std::hex << event.Argument
        << " after " << std::dec << Elapsed(event.TimeStamp);
    Log(msg.str());
}

//------------------------------------------------------------------------------
//  ModuleIo::HandleSoftwareAlert() --
//------------------------------------------------------------------------------

void  ModuleIo::HandleSoftwareAlert(Innovative::AlertSignalEvent & event)
{
    std::stringstream msg;
    msg << "Software alert 0x" << std::hex << event.Argument
        << " after " << std::dec << Elapsed(event.TimeStamp);
    Log(msg.str());
}

//------------------------------------------------------------------------------
//  ModuleIo::HandleWarningTempAlert() --
//------------------------------------------------------------------------------

void  ModuleIo::HandleWarningTempAlert(Innovative::AlertSignalEvent & event)
{
    // Clear warning condition (reading clears)
    //Module.Thermal().LogicWarningTemperature();

    std::stringstream msg;
    msg << "Temp warning alert 0x" << std::hex << event.Argument
        << " after " << std::dec << Elapsed(event.TimeStamp);

    Log(msg.str());
}

//------------------------------------------------------------------------------
//  ModuleIo::HandleInputFifoOverrunAlert() --
//------------------------------------------------------------------------------

void  ModuleIo::HandleInputFifoOverrunAlert(Innovative::AlertSignalEvent & event)
{
    std::stringstream msg;
    msg << "Input FIFO overrun 0x" << std::hex << event.Argument
        << " after " << std::dec << Elapsed(event.TimeStamp);
    Log(msg.str());

    //Module.Input().AcknowledgeAlert();
}

//------------------------------------------------------------------------------
//  ModuleIo::HandleTriggerAlert() --
//------------------------------------------------------------------------------

void  ModuleIo::HandleTriggerAlert(Innovative::AlertSignalEvent & event)
{
    std::string triggerType;
    switch (event.Argument & 0x3)
        {
        case 0:  triggerType = "? ";  break;
        case 1:  triggerType = "Input ";  break;
        case 2:  triggerType = "Output ";  break;
        case 3:  triggerType = "Input and Output ";  break;
        }
    std::stringstream msg;
    msg << "Trigger 0x" << std::hex << event.Argument
        << " Type: " <<  triggerType
        << " after " << std::dec << Elapsed(event.TimeStamp);
    Log(msg.str());
}

//------------------------------------------------------------------------------
//  ModuleIo::HandleInputOverrangeAlert() --
//------------------------------------------------------------------------------

void  ModuleIo::HandleInputOverrangeAlert(Innovative::AlertSignalEvent & event)
{
    std::stringstream msg;
    msg << "Input overrange 0x " << std::hex << event.Argument
        << " after " << std::dec << Elapsed(event.TimeStamp);
    Log(msg.str());

    //Module.Input().AcknowledgeAlert();
}

//------------------------------------------------------------------------------
//  ModuleIo::Elapsed() --  Display timestamp as elapsed MCLKs
//------------------------------------------------------------------------------

std::string ModuleIo::Elapsed(size_t timestamp)
{
    stringstream msg;
    msg << timestamp << " master clocks";
    return msg.str();
}

