#include "J1Sys.h"
#include <wx/msgdlg.h>
#include <wx/regex.h>
#include <wx/xml/xml.h>
#include "models/Model.h"
#include <log4cpp/Category.hh>
#include "outputs/OutputManager.h"
#include "outputs/Output.h"
#include "models/ModelManager.h"
#include <wx/sstream.h>
#include "UtilFunctions.h"
#include "ControllerUploadData.h"

// This code has been tested with
// ECG-P12S App Version 3.3
// ECG-P2 App Version 2.9b

// P12R - 1 universe per output
//      - 0 serial ports
//      - can receive 12 universes  
// P12S/P12D - 1 universe per output on 3.3
//           - 2 universes per output on 3.4
//           - 2 serial ports
//           - can receive 26 universes

static std::string J1SYS_P12S = "J1Sys-P12S";
static std::string J1SYS_P12R = "J1Sys-P12R";
static std::string J1SYS_P12D = "J1Sys-P12D";
static std::string J1SYS_P2 = "J1Sys P2";

static std::string J1SYS_MODEL_P12S = "ECG-P12S";
static std::string J1SYS_MODEL_P12R = "ECG-P12R";
static std::string J1SYS_MODEL_P12D = "ECG-P12D";

class J1SysControllerRules : public ControllerRules
{
    float _firmware = 0.0;
    int _outputs = 0;
    std::string _model = "";
public:
    J1SysControllerRules(int outputs, float firmware, const std::string& model) : 
        ControllerRules(), _outputs(outputs), _firmware(firmware), _model(model) {}
    virtual ~J1SysControllerRules() {}
    
    virtual const std::string GetControllerId() const override {
        if (_outputs == 2) {
            return J1SYS_P2;
        }
        if (_outputs == 12)
        {
            if (_model == J1SYS_MODEL_P12S)
            {
                return J1SYS_P12S;
            }
            if (_model == J1SYS_MODEL_P12R)
            {
                return J1SYS_P12R;
            }
            return J1SYS_P12D;
        }

        return J1SYS_P12R;
    }
    virtual const std::string GetControllerManufacturer() const override {
        return "J1Sys";
    }

    virtual bool SupportsLEDPanelMatrix() const override {
        return false;
    }

    virtual int GetMaxPixelPortChannels() const override 
    { 
        if (_outputs == 2) return 9999999; // we will detect this in a different way 
        if (_outputs == 12)
        {
            if (GetControllerId() == J1SYS_P12R)
            {
                return 170 * 3;
            }
            if (_firmware >= 3.4)
            {
                return 2 * 170 * 3;
            }
            return 170 * 3;
        }
        return 0;
    }
    virtual int GetMaxPixelPort() const override { return _outputs; }
    virtual int GetMaxSerialPortChannels() const override { if (_outputs == 12) return 512; return 0; }
    virtual int GetMaxSerialPort() const override 
    { 
        if (_outputs == 12)
        {
            if (GetControllerId() == J1SYS_P12R)
                return 0;
            return 2;
        }
        return 0; 
    }
    virtual bool IsValidPixelProtocol(const std::string protocol) const override
    {
        wxString p(protocol);
        p = p.Lower();
        return (p == "ws2811" || p == "tm180x" || p == "tm18xx" || p == "ws2801" || p == "tls3001" || p == "lpd6803");
    }
    virtual bool IsValidSerialProtocol(const std::string protocol) const override
    {
        wxString p(protocol);
        p = p.Lower();
        return (p == "renard" || p == "dmx");
    }
    virtual bool SupportsMultipleProtocols() const override { return true; }
    virtual bool SupportsSmartRemotes() const override { return false; }
    virtual bool SupportsMultipleInputProtocols() const override { return false; }
    virtual bool AllUniversesSameSize() const override { return false; }
    virtual std::set<std::string> GetSupportedInputProtocols() const override {
        std::set<std::string> res = {"E131", "ARTNET"};
        return res;
    };
    virtual bool UniversesMustBeSequential() const override { return false; }
};

J1Sys::J1Sys(const std::string& ip)
{
    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    _ip = ip;
    _outputs = 0;

    logger_base.debug("J1Sys upload tested to work with:");
    logger_base.debug("    ECG-P2 App Version 2.9b");
    logger_base.debug("    ECG-P12S App Version 3.3");

    _http.SetMethod("GET");
    _connected = _http.Connect(_ip);

    if (_connected)
    {
        std::string page = GetURL("/sysinfo.htm");
        if (page != "")
        {
            static wxRegEx versionregex("(App Version:\\<\\/b\\>\\<\\/td\\>\\<td\\>.nbsp;\\<\\/td\\>\\<td\\>)([^\\<]*)\\<", wxRE_ADVANCED | wxRE_NEWLINE);
            if (versionregex.Matches(wxString(page)))
            {
                _version = versionregex.GetMatch(wxString(page), 2).ToStdString();
                logger_base.debug("Connected to J1Sys controller version %s.", (const char *)_version.c_str());
            }
            static wxRegEx modelregex("(document\\.getElementById\\(.titleRight.\\)\\.innerHTML = .)([^\"]*)\"", wxRE_ADVANCED | wxRE_NEWLINE);
            if (modelregex.Matches(wxString(page)))
            {
                _model = modelregex.GetMatch(wxString(page), 2).ToStdString();
                logger_base.debug("     model %s.", (const char *)_model.c_str());
                static wxRegEx outputsregex("([0-9]+)", wxRE_ADVANCED);
                if (outputsregex.Matches(wxString(_model)))
                {
                    _outputs = wxAtoi(outputsregex.GetMatch(wxString(_model), 1));
                    logger_base.debug("     outputs %d.", _outputs);
                }
            }
        }
        else
        {
            _http.Close();
            _connected = false;
            logger_base.error("Error connecting to J1Sys controller on %s.", (const char *)_ip.c_str());
        }
    }
    else
    {
        logger_base.error("Error connecting to J1Sys controller on %s.", (const char *)_ip.c_str());
    }
}

J1Sys::~J1Sys()
{
    _http.Close();
}

std::string J1Sys::GetURL(const std::string& url, bool logresult)
{
    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    wxString res;

    if (!_http.IsConnected())
    {
        _connected = _http.Connect(_ip);
    }

    _http.SetMethod("GET");
    wxInputStream *httpStream = _http.GetInputStream(wxString(url));
    logger_base.debug("Making request to J1Sys '%s'.", (const char *)url.c_str());

    if (_http.GetError() == wxPROTO_NOERR)
    {
        wxStringOutputStream out_stream(&res);
        httpStream->Read(out_stream);

        if (logresult)
        {
            logger_base.debug("Response from J1Sys '%s' : %d.", (const char *)res.c_str(), _http.GetError());
        }
    }
    else
    {
        DisplayError(wxString::Format("Unable to connect to J1Sys '%s'.", url).ToStdString());
        res = "";
    }

    wxDELETE(httpStream);
    return res.ToStdString();
}

std::string J1Sys::PutURL(const std::string& url, const std::string& request, bool logresult)
{
    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    wxString res;

    if (!_http.IsConnected())
    {
        _connected = _http.Connect(_ip);
    }

    _http.SetMethod("POST");
    _http.SetUser("admin");
    _http.SetPostText("application/x-www-form-urlencoded", request);
    wxInputStream *httpStream = _http.GetInputStream(wxString(url));
    logger_base.debug("Making request to J1Sys '%s'.", (const char *)url.c_str());
    logger_base.debug("    With data '%s'.", (const char *)request.c_str());

    int httpres = _http.GetError();

    if (httpres == wxPROTO_NOERR)
    {
        wxStringOutputStream out_stream(&res);
        httpStream->Read(out_stream);

        if (logresult)
        {
            logger_base.debug("Response from J1Sys '%s'.", (const char *)res.c_str());
        }
    }
    else
    {
        DisplayError(wxString::Format("Unable to connect to J1Sys '%s' => %d.", url, httpres).ToStdString());
    }
    _http.SetPostText("", "");

    wxDELETE(httpStream);
    return res.ToStdString();
}

bool J1Sys::SetInputUniverses(OutputManager* outputManager, std::list<int>& selected)
{
    wxASSERT(_outputs != 0);

    bool e131 = false;
    bool artnet = false;

    // Get universes based on IP
    std::list<Output*> outputs = outputManager->GetAllOutputs(_ip, selected);

    for (auto it = outputs.begin(); it != outputs.end(); ++it)
    {
        if ((*it)->GetType() == "E131")
        {
            e131 = true;
        }
        else if ((*it)->GetType() == "ArtNet")
        {
            artnet = true;
        }
    }

    if (_outputs == 2)
    {
        if (outputs.size() > 8)
        {
            DisplayError(wxString::Format("Attempt to upload %d universes to j1Sys P2 controller but only 8 are supported.", (int)outputs.size()).ToStdString());
            return false;
        }
    }
    else if (_outputs == 12)
    {
        int maxUniverses = 12;
        if (_model != J1SYS_MODEL_P12R && wxAtof(_version) >= 3.4)
        {
            maxUniverses = 26;
        }
        if (outputs.size() > maxUniverses)
        {
            DisplayError(wxString::Format("Attempt to upload %d universes to j1Sys P12 controller but only %d are supported.", (int)outputs.size(), maxUniverses).ToStdString());
            return false;
        }
    }

    for (auto o:  outputs)
    {
        if (o->GetChannels() > 510)
        {
            DisplayError(wxString::Format("Attempt to upload universe %d to j1Sys controller of size %ld but maximum is 510.", o->GetUniverse(), o->GetChannels()).ToStdString());
            return false;
        }
    }

    std::string request = wxString::Format("an=0&e1en=%d&anen=%d", (e131) ? 1 : 0, (artnet) ? 1 : 0).ToStdString();
    std::string res = PutURL("/protect/ipConfig.htm", request);
    if (res != "")
    {
        return true;
    }

    return false;
}

bool J1Sys::SetOutputs(ModelManager* allmodels, OutputManager* outputManager, std::list<int>& selected, wxWindow* parent)
{
    if (!SetInputUniverses(outputManager, selected))
    {
        return false;
    }

    //ResetStringOutputs(); // this shouldnt be used normally
    //ResetSerialOutputs(); // this shouldnt be used normally

    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    logger_base.debug("J1Sys Outputs Upload: Uploading to %s", (const char*)_ip.c_str());

    std::string check;
    UDController cud(_ip, _ip, allmodels, outputManager, &selected, check);

    J1SysControllerRules rules(_outputs, wxAtof(_version), _model);
    bool success = cud.Check(&rules, check);

    cud.Dump();

    logger_base.debug(check);

    if (success && cud.GetMaxPixelPort() > 0)
    {
        // one per config row
        std::vector<J1SysOutput> j1SysOutputs(_outputs * GetBankSize());

        ReadCurrentConfig(j1SysOutputs);
        logger_base.debug("Existing config:");
        DumpConfig(j1SysOutputs);

        for (int pp = 1; pp <= _outputs; pp++)
        {
            if (cud.HasPixelPort(pp))
            {
                UDControllerPort* port = cud.GetControllerPixelPort(pp);
                long lastEnd = -1;
                int output = (pp - 1) * GetBankSize();
                int bankStart = (pp - 1) * GetBankSize();

                for (auto m : port->GetModels())
                {
                    if (lastEnd != -1 && m->GetStartChannel() != lastEnd + 1)
                    {
                        output++;
                        lastEnd = -1;
                    }

                    j1SysOutputs[output].protocol = EncodeStringPortProtocol(m->GetProtocol());
                    j1SysOutputs[output].speed = DecodeProtocolSpeed(m->GetProtocol());

                    if (lastEnd == -1)
                    {
                        int channels = m->Channels();
                        while (channels > 0)
                        {
                            if (output >= bankStart + GetBankSize())
                            {
                                DisplayError("Controller " + _ip + " too many outputs required for port " + wxString::Format("%d", pp) + ".");
                                logger_base.debug("Erroneous config:");
                                DumpConfig(j1SysOutputs);
                                return false;
                            }

                            j1SysOutputs[output].active = true;
                            for (int i = output % GetBankSize() + 1; i < GetBankSize(); i++)
                            {
                                j1SysOutputs[bankStart + i].active = false;
                                j1SysOutputs[bankStart + i].universe = 0;
                                j1SysOutputs[bankStart + i].startChannel = 0;
                                j1SysOutputs[bankStart + i].pixels = 0;
                            }
                            long sc;
                            auto o = outputManager->GetOutput(m->GetStartChannel() + m->Channels() - channels, sc);
                            j1SysOutputs[output].universe = o->GetUniverse();
                            j1SysOutputs[output].startChannel = sc;
                            if (channels < o->GetChannels() - sc + 1)
                            {
                                j1SysOutputs[output].pixels = channels / 3;
                                channels = 0;
                            }
                            else
                            {
                                j1SysOutputs[output].pixels = (o->GetChannels() - sc + 1) / 3;
                                channels -= o->GetChannels() - sc + 1;
                                output++;
                            }
                        }
                        lastEnd = m->GetEndChannel();
                    }
                    else
                    {
                        int channels = m->Channels();
                        while (channels > 0)
                        {
                            if (output >= bankStart + GetBankSize())
                            {
                                DisplayError("Controller " + _ip + " too many outputs required for port " + wxString::Format("%d", pp) + ".");
                                logger_base.debug("Erroneous config:");
                                DumpConfig(j1SysOutputs);
                                return false;
                            }

                            long sc;
                            auto o = outputManager->GetOutput(m->GetStartChannel() + m->Channels() - channels, sc);
                            if (j1SysOutputs[output].universe == 0)
                            {
                                j1SysOutputs[output].universe = o->GetUniverse();
                                j1SysOutputs[output].startChannel = sc;
                                j1SysOutputs[output].active = true;
                                for (int i = output % GetBankSize() + 1; i < GetBankSize(); i++)
                                {
                                    j1SysOutputs[bankStart + i].active = false;
                                    j1SysOutputs[bankStart + i].universe = 0;
                                    j1SysOutputs[bankStart + i].startChannel = 0;
                                    j1SysOutputs[bankStart + i].pixels = 0;
                                }
                                j1SysOutputs[output].protocol = j1SysOutputs[(pp - 1) * GetBankSize()].protocol;
                                j1SysOutputs[output].speed = j1SysOutputs[(pp - 1) * GetBankSize()].speed;
                            }
                            if (channels < o->GetChannels() - j1SysOutputs[output].startChannel + 1 - j1SysOutputs[output].pixels * 3)
                            {
                                j1SysOutputs[output].pixels += channels / 3;
                                channels = 0;
                            }
                            else
                            {
                                j1SysOutputs[output].pixels = (o->GetChannels() - j1SysOutputs[output].startChannel + 1) / 3;
                                channels -= o->GetChannels() - j1SysOutputs[output].startChannel + 1 - j1SysOutputs[output].pixels * 3;
                                output++;
                            }
                        }
                        lastEnd = m->GetEndChannel();
                    }
                }

                // make sure every row has a copy of the bank protocol and speed
                for (auto i = 1; i < GetBankSize(); i++)
                {
                    j1SysOutputs[bankStart + i].protocol = j1SysOutputs[bankStart].protocol;
                    j1SysOutputs[bankStart + i].speed = j1SysOutputs[bankStart].speed;
                }
            }
            else
            {
                // unused port
            }
        }

        int port = 0;
        for (auto& j : j1SysOutputs)
        {
            j.port = port;
            port++;
        }

        logger_base.debug("Uploading pixel config:");
        DumpConfig(j1SysOutputs);

        logger_base.debug("Building pixel upload:");
        std::string requestString;
        for (auto& j : j1SysOutputs)
        {
            if (requestString != "")
                requestString += "&";
            requestString += BuildStringPort(j.active, j.port, j.protocol, j.speed, j.startChannel, j.universe, j.pixels, parent);
        }

        if (requestString != "")
        {
            std::string res = PutURL("/protect/stringConfig.htm", requestString);
            if (res == "")
            {
                success = false;
            }
        }
    }
 
    if (success && cud.GetMaxSerialPort() > 0)
    {
        std::vector<J1SysSerialOutput> j1SysOutputs(rules.GetMaxSerialPort());

        ReadCurrentSerialConfig(j1SysOutputs);
        logger_base.debug("Existing config:");
        DumpConfig(j1SysOutputs);

        for (int sp = 1; sp <= cud.GetMaxSerialPort(); sp++)
        {
            if (cud.HasSerialPort(sp))
            {
                UDControllerPort* port = cud.GetControllerSerialPort(sp);
                j1SysOutputs[sp - 1].active = true;
                j1SysOutputs[sp - 1].protocol = EncodeSerialPortProtocol(port->GetProtocol());
                if (j1SysOutputs[sp - 1].protocol == 'D' && j1SysOutputs[sp - 1].speed < 5)
                {
                    j1SysOutputs[sp - 1].speed = 5;
                }
                j1SysOutputs[sp - 1].universe = port->GetUniverse();

                long sc;
                auto o = outputManager->GetOutput(port->GetStartChannel(), sc);
                long sc2;
                auto o2 = outputManager->GetOutput(port->GetEndChannel(), sc2);

                if (o != o2)
                {
                    DisplayError("Controller " + _ip + " serial port "+ wxString::Format("%d", sp) +"requires more than 1 universe.");
                    logger_base.debug("Erroneous config:");
                    DumpConfig(j1SysOutputs);
                    return false;
                }

                if (sc != 1)
                {
                    DisplayError("Controller " + _ip + " serial port " +wxString::Format("%d",sp)+ "does not start on channel 1 of universe " +
                        wxString::Format("%d", port->GetUniverse()) + ". It starts at " +
                        wxString::Format("%d", port->GetStartChannel()) + ".");
                    logger_base.debug("Erroneous config:");
                    DumpConfig(j1SysOutputs);
                    return false;
                }
            }
        }

        logger_base.debug("Uploading serial config:");
        DumpConfig(j1SysOutputs);

        logger_base.debug("Building serial upload:");
        std::string requestString;
        for (auto& j : j1SysOutputs)
        {
            if (requestString != "")
                requestString += "&";
            requestString += BuildSerialPort(j.active, j.port+1, j.protocol, j.speed, j.universe, parent);
        }

        if (requestString != "")
        {
            std::string res = PutURL("/protect/portConfig.htm", requestString);
            if (res == "")
            {
                success = false;
            }
        }
    }

    if (success) Reboot();

    return success;
}

void J1Sys::DumpConfig(const std::vector<J1SysOutput>& outputs) const
{
    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    for (auto j : outputs)
    {
        logger_base.debug("   Port %c, Active: %d, Protocol: %c, Speed %d, Universe %d, StartChannel %d, Pixels %d", j.port + 65, j.active, j.protocol, j.speed, j.universe, j.startChannel, j.pixels);
    }
}

void J1Sys::DumpConfig(const std::vector<J1SysSerialOutput>& outputs) const
{
    static log4cpp::Category& logger_base = log4cpp::Category::getInstance(std::string("log_base"));
    for (auto j : outputs)
    {
        logger_base.debug("   Port %c, Active: %d, Protocol: %c, Speed %d, Universe %d", j.port + 65, j.active, j.protocol, j.speed, j.universe);
    }
}

void J1Sys::ReadCurrentConfig(std::vector<J1SysOutput>& j)
{
    std::string config = GetURL("/protect/stringConfig.htm");

    if (!config.empty())
    {
        for (auto i = 0; i < j.size(); i++)
        {
            j[i].port = i;
            wxString activeRegex = wxString::Format("sA%c[^>]*checked", i + 65);
            wxRegEx ar(activeRegex);
            j[i].active = ar.Matches(wxString(config));

            if (i % GetBankSize() == 0)
            {
                wxString protocolRegex = wxString::Format("sT%c>[^#]*selected>([^<]*)", i + 65);
                wxRegEx pr(protocolRegex);
                if (pr.Matches(wxString(config)))
                {
                    j[i].protocol = EncodeStringPortProtocol(pr.GetMatch(wxString(config), 1));
                }
                wxString speedRegex = wxString::Format("sB%c[^>]*value=\"([^\"]*)\"", i + 65);
                wxRegEx sr(speedRegex);
                if (sr.Matches(wxString(config)))
                {
                    j[i].speed = wxAtoi(sr.GetMatch(wxString(config), 1));
                }
            }
            else
            {
                j[i].protocol = j[i / GetBankSize() * GetBankSize()].protocol;
                j[i].speed = j[i / GetBankSize() * GetBankSize()].speed;
            }

            wxString universeRegex = wxString::Format("sU%c[^>]*value=\"([0-9]*)", i + 65);
            wxRegEx ur(universeRegex);
            if (ur.Matches(wxString(config)))
            {
                j[i].universe = wxAtoi(ur.GetMatch(wxString(config), 1));
            }
            wxString startChannelRegex = wxString::Format("sS%c[^>]*value=\"([0-9]*)", i + 65);
            wxRegEx scr(startChannelRegex);
            if (scr.Matches(wxString(config)))
            {
                j[i].startChannel = wxAtoi(scr.GetMatch(wxString(config), 1));
            }
            wxString pixelsRegex = wxString::Format("sC%c[^>]*value=\"([0-9]*)", i + 65);
            wxRegEx pxr(pixelsRegex);
            if (pxr.Matches(wxString(config)))
            {
                j[i].pixels = wxAtoi(pxr.GetMatch(wxString(config), 1));
            }
        }
    }
}

void J1Sys::ReadCurrentSerialConfig(std::vector<J1SysSerialOutput>& j)
{
    std::string config = GetURL("/protect/portConfig.htm");

    if (!config.empty())
    {
        for (auto i = 0; i < j.size(); i++)
        {
            j[i].port = i;
            wxString activeRegex = wxString::Format("pA%d[^>]*checked", i + 1);
            wxRegEx ar(activeRegex);
            j[i].active = ar.Matches(wxString(config));

            wxString protocolRegex = wxString::Format("pP%d.+?value=\\\"(.)\\\" selected", i + 1);
            wxRegEx pr(protocolRegex, wxRE_ADVANCED);
            if (pr.Matches(wxString(config)))
            {
                j[i].protocol = pr.GetMatch(wxString(config), 1)[0];
            }
            wxString speedRegex = wxString::Format("pB%d.+?value=\\\"([0-9]+)\\\" selected", i + 1);
            wxRegEx sr(speedRegex, wxRE_ADVANCED);
            if (sr.Matches(wxString(config)))
            {
                j[i].speed = wxAtoi(sr.GetMatch(wxString(config), 1));
            }

            wxString universeRegex = wxString::Format("pU%d[^>]*value=\\\"([0-9]*)", i + 1);
            wxRegEx ur(universeRegex);
            if (ur.Matches(wxString(config)))
            {
                j[i].universe = wxAtoi(ur.GetMatch(wxString(config), 1));
            }
        }
    }
}

int J1Sys::GetBankSize() const
{
    if (_outputs == 2) return 4;
    return 1;
}

char J1Sys::EncodeStringPortProtocol(std::string protocol) const
{
    wxString p(protocol);
    p = p.Lower();

    if (p == "ws2811") return '4';
    if (p == "tm180x") return '2';
    if (p == "tm18xx") return '2';
    if (p == "ws2801") return '1';
    if (p == "tls3001") return '3';
    if (p == "lpd6803") return '0';

    return -1;
}

char J1Sys::EncodeSerialPortProtocol(std::string protocol) const
{
    wxString p(protocol);
    p = p.Lower();

    if (p == "dmx") return 'D';
    if (p == "renard") return 'R';

    return -1;
}

int J1Sys::DecodeProtocolSpeed(std::string protocol) const
{
    wxString p(protocol);
    p = p.Lower();

    if (p == "ws2811") return 3600;
    if (p == "ws2801") return 750;
    if (p == "dmx") return 5;
    if (p == "renard") return 3;
    return 0;
}

std::string J1Sys::BuildStringPort(bool active, int string, char protocol, int speed, int startChannel, int universe, int pixels, wxWindow* parent) const
{
    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));

    int out = 65 + string;

    logger_base.debug("     Output String %d, Protocol %c Universe %d StartChannel %d Pixels %d",
        string, protocol, universe, startChannel, pixels);

    return wxString::Format("sA%c=%d&sT%c=%c&sB%c=%d&sU%c=%d&sS%c=%d&sC%c=%d",
        out, active ? 1 : 0,
        out, protocol,
        out, speed,
        out, universe,
        out, startChannel,
        out, pixels).ToStdString();
}

std::string J1Sys::BuildSerialPort(bool active, int port, char protocol, int speed, int universe, wxWindow* parent) const
{
    static log4cpp::Category &logger_base = log4cpp::Category::getInstance(std::string("log_base"));

    logger_base.debug("     Output Serial %d, Protocol %c Universe %d",
        port, protocol, universe);

    return wxString::Format("pA%d=%d&pP%d=%c&pB%d=%d&pU%d=%d",
        port, active ? 1 : 0,
        port, protocol,
        port, speed,
        port, universe).ToStdString();
}

void J1Sys::ResetStringOutputs()
{
    PutURL("/protect/stringConfig.htm","");
}

void J1Sys::ResetSerialOutputs()
{
    if (_outputs == 12)
    {
        PutURL("/protect/portConfig.htm", "");
    }
}

void J1Sys::Reboot()
{
    GetURL("/protect/reboot.htm?");
}
