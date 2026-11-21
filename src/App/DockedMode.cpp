#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Devices.Display.h>

#include <windows.h>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "Logging/LogManager.hpp"
#include "Configuration/Config.hpp"
#include "Tools/Process.hpp"
#include "Tools/Registry.hpp"
#include "Tools/Unicode.hpp"

#include "App/Constants.hpp"
#include "App/DockedMode.hpp"
#include "App/GamingExperience.hpp"

#pragma comment(lib, "windowsapp.lib")

namespace c = AnyFSE::App::Constants;
namespace wdd = winrt::Windows::Devices::Display;

namespace AnyFSE::App::DockedMode
{
    static Logger log = LogManager::GetLogger("DockedMode");

    // A panel whose EDID reports a smaller diagonal is the handheld's own screen, whatever connector type its driver reports.
    static constexpr double BuiltInPanelMaxDiagonalInches = 10.0;

    enum class DisplayKind { BuiltIn, External, Ignored };

    struct DisplayInfo
    {
        // QueryDisplayConfig / DisplayConfigGetDeviceInfo
        LUID adapterId = {};
        UINT32 targetId = 0;
        DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
        DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY baseTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER;
        bool hasBaseTechnology = false;
        bool available = false;
        UINT32 statusFlags = 0;
        UINT32 width = 0;
        UINT32 height = 0;
        std::wstring gdiName;
        std::wstring friendlyName;
        std::wstring monitorPath;
        std::wstring adapterPath;

        // Windows.Devices.Display.DisplayMonitor
        bool hasMonitor = false;
        std::string monitorError;
        wdd::DisplayMonitorConnectionKind connectionKind = wdd::DisplayMonitorConnectionKind::Internal;
        wdd::DisplayMonitorPhysicalConnectorKind connector = wdd::DisplayMonitorPhysicalConnectorKind::Unknown;
        wdd::DisplayMonitorUsageKind usageKind = wdd::DisplayMonitorUsageKind::Standard;
        std::wstring monitorName;
        bool hasPhysicalSize = false;
        double widthInches = 0;
        double heightInches = 0;

        DisplayKind kind = DisplayKind::Ignored;
        const char *reason = "";
    };

    static const char *BoolText(bool value)
    {
        return value ? "yes" : "no";
    }

    static const char *TechnologyName(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology)
    {
        // DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_USB_TUNNEL (USB4 docks) is missing from older SDK headers
        if ((int)technology == 17)
        {
            return "DisplayPort USB tunnel";
        }

        switch (technology)
        {
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER:                return "Other";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15:                 return "HD15 (VGA)";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SVIDEO:               return "S-Video";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPOSITE_VIDEO:      return "Composite video";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_COMPONENT_VIDEO:      return "Component video";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI:                  return "DVI";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI:                 return "HDMI";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS:                 return "LVDS";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_D_JPN:                return "D-JPN";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDI:                  return "SDI";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL: return "DisplayPort external";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED: return "DisplayPort embedded (eDP)";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EXTERNAL:         return "UDI external";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:         return "UDI embedded";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_SDTVDONGLE:           return "SDTV dongle";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST:             return "Miracast";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_WIRED:       return "Indirect wired";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL:     return "Indirect virtual";
            case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:             return "Internal";
            default:                                                   return "Unknown";
        }
    }

    static const char *ConnectionKindName(wdd::DisplayMonitorConnectionKind kind)
    {
        switch (kind)
        {
            case wdd::DisplayMonitorConnectionKind::Internal: return "Internal";
            case wdd::DisplayMonitorConnectionKind::Wired:    return "Wired";
            case wdd::DisplayMonitorConnectionKind::Wireless: return "Wireless";
            case wdd::DisplayMonitorConnectionKind::Virtual:  return "Virtual";
            default:                                          return "Unknown";
        }
    }

    static const char *ConnectorName(wdd::DisplayMonitorPhysicalConnectorKind connector)
    {
        switch (connector)
        {
            case wdd::DisplayMonitorPhysicalConnectorKind::HD15:        return "HD15 (VGA)";
            case wdd::DisplayMonitorPhysicalConnectorKind::AnalogTV:    return "Analog TV";
            case wdd::DisplayMonitorPhysicalConnectorKind::Dvi:         return "DVI";
            case wdd::DisplayMonitorPhysicalConnectorKind::Hdmi:        return "HDMI";
            case wdd::DisplayMonitorPhysicalConnectorKind::Lvds:        return "LVDS";
            case wdd::DisplayMonitorPhysicalConnectorKind::Sdi:         return "SDI";
            case wdd::DisplayMonitorPhysicalConnectorKind::DisplayPort: return "DisplayPort";
            default:                                                    return "Unknown";
        }
    }

    static const char *UsageKindName(wdd::DisplayMonitorUsageKind usage)
    {
        switch (usage)
        {
            case wdd::DisplayMonitorUsageKind::Standard:       return "Standard";
            case wdd::DisplayMonitorUsageKind::HeadMounted:    return "Head mounted";
            case wdd::DisplayMonitorUsageKind::SpecialPurpose: return "Special purpose";
            default:                                           return "Unknown";
        }
    }

    static const char *KindName(DisplayKind kind)
    {
        switch (kind)
        {
            case DisplayKind::BuiltIn:  return "BUILT-IN";
            case DisplayKind::External: return "EXTERNAL";
            default:                    return "IGNORED";
        }
    }

    static bool IsBuiltInTechnology(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology)
    {
        return technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL
            || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED
            || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
    }

    static bool IsVirtualTechnology(DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology)
    {
        return technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER
            || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL;
    }

    static bool IsVirtualAdapter(const std::wstring &adapterPath)
    {
        // Real GPUs are enumerated on PCI (DisplayLink on USB). Virtual display drivers such as Parsec, Sunshine/Apollo or
        // the Virtual Display Driver are root or software devices, and they often report HDMI as output technology.
        const std::wstring path = Unicode::to_upper(adapterPath);
        for (const wchar_t *prefix : c::VirtualAdapterPathPrefixes)
        {
            if (path.rfind(prefix, 0) == 0)
            {
                return true;
            }
        }
        return false;
    }

    template <typename T>
    static LONG GetDeviceInfo(T &info, DISPLAYCONFIG_DEVICE_INFO_TYPE type, LUID adapterId, UINT32 id)
    {
        info = {};
        info.header.type = type;
        info.header.size = sizeof(T);
        info.header.adapterId = adapterId;
        info.header.id = id;
        return DisplayConfigGetDeviceInfo(&info.header);
    }

    static bool QueryActiveDisplays(std::vector<DisplayInfo> &displays)
    {
        std::vector<DISPLAYCONFIG_PATH_INFO> paths;
        std::vector<DISPLAYCONFIG_MODE_INFO> modes;
        LONG result = ERROR_INSUFFICIENT_BUFFER;

        // Display topology may change between GetDisplayConfigBufferSizes and QueryDisplayConfig
        for (int attempt = 0; attempt < 3 && result == ERROR_INSUFFICIENT_BUFFER; attempt++)
        {
            UINT32 pathCount = 0;
            UINT32 modeCount = 0;
            result = GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount);
            if (result != ERROR_SUCCESS || pathCount == 0)
            {
                break;
            }

            paths.resize(pathCount);
            modes.resize(modeCount);
            result = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), NULL);
            paths.resize(pathCount);
            modes.resize(modeCount);
        }

        if (result != ERROR_SUCCESS)
        {
            log.Warn("Can't query the display configuration, error %ld", result);
            return false;
        }

        for (const DISPLAYCONFIG_PATH_INFO &path : paths)
        {
            DisplayInfo info;
            info.adapterId = path.targetInfo.adapterId;
            info.targetId = path.targetInfo.id;
            info.technology = path.targetInfo.outputTechnology;
            info.available = path.targetInfo.targetAvailable != FALSE;
            info.statusFlags = path.targetInfo.statusFlags;

            const UINT32 modeIndex = path.sourceInfo.modeInfoIdx;
            if (modeIndex < modes.size() && modes[modeIndex].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE)
            {
                info.width = modes[modeIndex].sourceMode.width;
                info.height = modes[modeIndex].sourceMode.height;
            }

            DISPLAYCONFIG_SOURCE_DEVICE_NAME sourceName;
            if (GetDeviceInfo(sourceName, DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, path.sourceInfo.adapterId, path.sourceInfo.id) == ERROR_SUCCESS)
            {
                info.gdiName = sourceName.viewGdiDeviceName;
            }

            DISPLAYCONFIG_TARGET_DEVICE_NAME targetName;
            if (GetDeviceInfo(targetName, DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, info.adapterId, info.targetId) == ERROR_SUCCESS)
            {
                info.friendlyName = targetName.monitorFriendlyDeviceName;
                info.monitorPath = targetName.monitorDevicePath;
            }

            DISPLAYCONFIG_TARGET_BASE_TYPE baseType;
            if (GetDeviceInfo(baseType, DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_BASE_TYPE, info.adapterId, info.targetId) == ERROR_SUCCESS)
            {
                info.baseTechnology = baseType.baseOutputTechnology;
                info.hasBaseTechnology = true;
            }

            DISPLAYCONFIG_ADAPTER_NAME adapterName;
            if (GetDeviceInfo(adapterName, DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME, info.adapterId, 0) == ERROR_SUCCESS)
            {
                info.adapterPath = adapterName.adapterDevicePath;
            }

            displays.push_back(info);
        }

        return true;
    }

    static void ReadDisplayMonitors(std::vector<DisplayInfo> &displays)
    {
        // DisplayMonitor is a Windows Runtime API. Query it on an own MTA thread, independent of the caller's apartment.
        std::thread worker([&displays]()
        {
            try
            {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
            }
            catch (const winrt::hresult_error &error)
            {
                for (DisplayInfo &info : displays)
                {
                    info.monitorError = "Windows Runtime is not available: " + Unicode::to_string(error.message().c_str());
                }
                return;
            }

            for (DisplayInfo &info : displays)
            {
                if (info.monitorPath.empty())
                {
                    info.monitorError = "no monitor device path";
                    continue;
                }

                try
                {
                    wdd::DisplayMonitor monitor = wdd::DisplayMonitor::FromInterfaceIdAsync(winrt::hstring(info.monitorPath.c_str())).get();
                    if (!monitor)
                    {
                        info.monitorError = "monitor not found";
                        continue;
                    }

                    info.hasMonitor = true;
                    info.connectionKind = monitor.ConnectionKind();
                    info.connector = monitor.PhysicalConnector();
                    info.usageKind = monitor.UsageKind();
                    info.monitorName = monitor.DisplayName().c_str();

                    auto size = monitor.PhysicalSizeInInches();
                    if (size)
                    {
                        info.hasPhysicalSize = true;
                        info.widthInches = size.Value().Width;
                        info.heightInches = size.Value().Height;
                    }
                }
                catch (const winrt::hresult_error &error)
                {
                    char code[16];
                    sprintf_s(code, "0x%08X ", (unsigned int)error.code());
                    info.monitorError = code + Unicode::to_string(error.message().c_str());
                }
            }

            winrt::uninit_apartment();
        });

        worker.join();
    }

    static void Classify(DisplayInfo &info)
    {
        const bool builtInTechnology = IsBuiltInTechnology(info.technology)
            || (info.hasBaseTechnology && IsBuiltInTechnology(info.baseTechnology));
        const bool virtualTechnology = IsVirtualTechnology(info.technology)
            || (info.hasBaseTechnology && IsVirtualTechnology(info.baseTechnology));
        const double diagonal = info.hasPhysicalSize
            ? std::sqrt(info.widthInches * info.widthInches + info.heightInches * info.heightInches) : 0;

        if (!info.available)
        {
            info.kind = DisplayKind::Ignored;
            info.reason = "target is not available";
        }
        else if (builtInTechnology)
        {
            info.kind = DisplayKind::BuiltIn;
            info.reason = "built-in output technology";
        }
        else if (info.hasMonitor && info.connectionKind == wdd::DisplayMonitorConnectionKind::Internal)
        {
            info.kind = DisplayKind::BuiltIn;
            info.reason = "Windows reports an internal connection";
        }
        else if (virtualTechnology || (info.hasMonitor && info.connectionKind == wdd::DisplayMonitorConnectionKind::Virtual))
        {
            info.kind = DisplayKind::Ignored;
            info.reason = "virtual display";
        }
        else if (IsVirtualAdapter(info.adapterPath))
        {
            info.kind = DisplayKind::Ignored;
            info.reason = "display of a virtual display adapter";
        }
        else if (info.technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST
            || (info.hasMonitor && info.connectionKind == wdd::DisplayMonitorConnectionKind::Wireless))
        {
            info.kind = DisplayKind::Ignored;
            info.reason = "wireless display, not a dock";
        }
        else if (diagonal > 0 && diagonal < BuiltInPanelMaxDiagonalInches)
        {
            info.kind = DisplayKind::BuiltIn;
            info.reason = "panel smaller than 10 inches, treated as built-in despite its connector type";
        }
        else
        {
            info.kind = DisplayKind::External;
            info.reason = "external connection";
        }
    }

    static void LogDisplay(size_t index, const DisplayInfo &info)
    {
        log.Info("Display %zu: \"%s\" on %s, %ux%u",
            index, Unicode::to_string(info.friendlyName).c_str(), Unicode::to_string(info.gdiName).c_str(), info.width, info.height);

        log.Info("  Output technology: %s (%d), base: %s, target available: %s, status flags: 0x%X",
            TechnologyName(info.technology), (int)info.technology,
            info.hasBaseTechnology ? TechnologyName(info.baseTechnology) : "not reported",
            BoolText(info.available), info.statusFlags);

        log.Info("  Monitor: %s", Unicode::to_string(info.monitorPath).c_str());
        log.Info("  Adapter: %s (LUID %08lX:%08lX, target %u)",
            Unicode::to_string(info.adapterPath).c_str(), info.adapterId.HighPart, info.adapterId.LowPart, info.targetId);

        if (info.hasMonitor)
        {
            if (info.hasPhysicalSize)
            {
                const double diagonal = std::sqrt(info.widthInches * info.widthInches + info.heightInches * info.heightInches);
                log.Info("  DisplayMonitor: \"%s\", connection: %s, connector: %s, usage: %s, size: %.1f x %.1f in (%.1f in diagonal)",
                    Unicode::to_string(info.monitorName).c_str(), ConnectionKindName(info.connectionKind), ConnectorName(info.connector),
                    UsageKindName(info.usageKind), info.widthInches, info.heightInches, diagonal);
            }
            else
            {
                log.Info("  DisplayMonitor: \"%s\", connection: %s, connector: %s, usage: %s, size: not reported",
                    Unicode::to_string(info.monitorName).c_str(), ConnectionKindName(info.connectionKind), ConnectorName(info.connector),
                    UsageKindName(info.usageKind));
            }
        }
        else
        {
            log.Info("  DisplayMonitor: unavailable (%s)", info.monitorError.c_str());
        }

        log.Info("  => %s: %s", KindName(info.kind), info.reason);
    }

    bool IsExternalDisplayActive()
    {
        // The detection before 0.90.18 used these two values. HORZSIZE is not the physical width on modern Windows,
        // so they are logged only to compare with the classification below.
        HDC hdc = GetDC(NULL);
        const int horzSize = hdc ? GetDeviceCaps(hdc, HORZSIZE) : 0;
        if (hdc)
        {
            ReleaseDC(NULL, hdc);
        }
        log.Info("Legacy values for comparison: SM_CMONITORS=%d, HORZSIZE=%d mm", GetSystemMetrics(SM_CMONITORS), horzSize);

        std::vector<DisplayInfo> displays;
        if (!QueryActiveDisplays(displays))
        {
            return false;
        }

        ReadDisplayMonitors(displays);

        int builtIn = 0;
        int external = 0;
        int ignored = 0;

        for (size_t i = 0; i < displays.size(); i++)
        {
            Classify(displays[i]);
            LogDisplay(i + 1, displays[i]);

            switch (displays[i].kind)
            {
                case DisplayKind::BuiltIn:  builtIn++;  break;
                case DisplayKind::External: external++; break;
                default:                    ignored++;  break;
            }
        }

        log.Info("Active displays: %zu (built-in %d, external %d, ignored %d) => %s",
            displays.size(), builtIn, external, ignored, external ? "DOCKED" : "not docked");

        return external > 0;
    }

    bool ExitWhenDocked()
    {
        if (!Config::SmartDockedMode)
        {
            log.Debug("Smart Docked Mode is off");
            return false;
        }

        if (GlobalFindAtom(c::DockedCheckAtomName))
        {
            log.Info("Docked check already ran in this Windows session, skipping");
            return false;
        }

        // Check once per session, and only when Windows starts in full screen experience
        const bool startupToFse = Registry::ReadBool(c::GamingHomeAppRegKey, c::StartupToGamingHomeRegValue);
        const bool fullscreen = GamingExperience::IsFullscreenMode();
        log.Info("Docked check: Windows starts in full screen experience: %s, full screen experience is active: %s",
            BoolText(startupToFse), BoolText(fullscreen));

        if (!startupToFse || !fullscreen)
        {
            log.Info("Docked check skipped: it runs only on the first launch after Windows started in full screen experience");
            return false;
        }

        GlobalAddAtom(c::DockedCheckAtomName);

        if (!IsExternalDisplayActive())
        {
            log.Info("Staying in full screen experience");
            return false;
        }

        log.Info("External display is active, leaving full screen experience");
        GamingExperience::ExitFSEMode();

        // Wait while the exit confirmation is shown and up to 3 seconds after it is closed.
        // Give up after 10 seconds if it never appears, and after 60 seconds in any case.
        const DWORD start = GetTickCount();
        DWORD lastGamebar = 0;
        bool wasGamebar = false;
        bool isGamebar = false;
        while (GamingExperience::IsFullscreenMode() && GetTickCount() - start < 60000
            && (wasGamebar ? GetTickCount() - lastGamebar < 3000 : GetTickCount() - start < 10000))
        {
            Sleep(250);
            std::wstring pointProcess = Unicode::to_lower(Process::GetWindowProcessName(WindowFromPoint(POINT{1, 1})));
            std::wstring foregroundProcess = Unicode::to_lower(Process::GetWindowProcessName(GetForegroundWindow()));
            const bool gamebar = pointProcess == c::GameBarExe || foregroundProcess == c::GameBarExe;
            if (gamebar != isGamebar)
            {
                log.Info("Game Bar exit confirmation %s after %lu ms", gamebar ? "is shown" : "is closed", GetTickCount() - start);
                isGamebar = gamebar;
            }
            if (gamebar)
            {
                wasGamebar = true;
                lastGamebar = GetTickCount();
            }
        }

        if (GamingExperience::IsFullscreenMode())
        {
            log.Warn("Full screen experience is still active after %lu ms (exit confirmation %s), starting the Home app",
                GetTickCount() - start, wasGamebar ? "was cancelled" : "never appeared");
            return false;
        }

        log.Info("Left full screen experience after %lu ms", GetTickCount() - start);

        // The next full screen experience enter in this session is the first launch again (startup apps)
        GlobalDeleteAtom(GlobalFindAtom(c::PackageAtomName));
        return true;
    }
}
