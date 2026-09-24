#include <vector>

#include "Logging/LogManager.hpp"
#include "Configuration/Config.hpp"
#include "Tools/Process.hpp"
#include "Tools/Registry.hpp"
#include "Tools/Unicode.hpp"

#include "App/Launchers.hpp"
#include "App/GamingExperience.hpp"
#include "App/ExitFSE.hpp"
#include "App/Constants.hpp"


namespace AnyFSE::App::ExitFSE
{

    HANDLE RegisterWaitingMutex();
    bool IsMutexExists();
    bool IsExternalDisplayActive();

    static Logger log = LogManager::GetLogger("ExitFSE");


    HANDLE RegisterWaitingMutex()
    {
        HANDLE hMutex = CreateMutex(NULL, TRUE, App::Constants::WaitingExitMutex);

        if (GetLastError() == ERROR_ALREADY_EXISTS)
        {
            log.Debug("Another instance of AnyFSE is already wait launcher exiting, exiting\n");
            if (hMutex)
            {
                CloseHandle(hMutex);
                return NULL;
            }
        }
        return hMutex;
    }

    bool IsMutexExists()
    {
        HANDLE hMutex = OpenMutex(SYNCHRONIZE, FALSE, App::Constants::WaitingExitMutex);

        if (hMutex)
        {
            CloseHandle(hMutex);
            return true;
        }

        return false;
    }

    bool WaitHomeAppExit()
    {
        if (!Config::ExitFSEOnHomeExit || !GamingExperience::IsFullscreenMode() || IsMutexExists() || !Launchers::IsLauncherActiveOrMinimized() )
        {
            log.Trace("Skip WaitHomeAppExit");
            return false;
        }

        HANDLE hMutex = RegisterWaitingMutex();
        if (!hMutex)
        {
            log.Trace("Can't register RegisterWaitingMutex");
            return false;
        }

        log.Debug("Option to monitor home app finish");

        Launchers::WaitLauncherExit();
        if (Config::ExitFSEOnHomeExit)
        {
            DWORD start = GetTickCount();
            GamingExperience::ExitFSEMode();

            if (GetTickCount() - start < 50)
            {
                log.Trace("GamingExperience::ExitFSEMode() Completed in: %d msec", GetTickCount() - start);

                bool wasGamebar = false;
                bool isGamebar = false;
                log.Debug("Waiting ExitFSE loop");
                while(GamingExperience::IsFullscreenMode() && (isGamebar || !wasGamebar))
                {
                    Sleep(500);
                    std::wstring activeProcess = Unicode::to_lower(Process::GetWindowProcessName(WindowFromPoint(POINT{1, 1})));
                    log.Trace("Waiting ExitFSE: Active process: %s", Unicode::to_string(activeProcess).c_str());
                    isGamebar = activeProcess == L"gamebar.exe";
                    wasGamebar |= isGamebar;
                }
            }
            if (!GamingExperience::IsFullscreenMode())
            {
                Sleep(2000);
            }
            log.Trace("Waiting ExitFSE: Complete, mode is %s", GamingExperience::IsFullscreenMode() ? "FSE" : "Desktop");
        }
        CloseHandle(hMutex);
        return true;
    }

    bool WaitExitFSEMode()
    {
        if (!Config::ExitFSEOnHomeExit || !IsMutexExists() || Launchers::IsLauncherActiveOrMinimized())
        {
            return false;
        }

        log.Trace("Check wait mutex");
        HANDLE hMutex = OpenMutex(SYNCHRONIZE, FALSE, App::Constants::WaitingExitMutex);
        if (hMutex)
        {
            log.Trace("Waiting ExitFSE");
            WaitForSingleObject(hMutex, INFINITE);
            CloseHandle(hMutex);
            hMutex = NULL;
            return !GamingExperience::IsFullscreenMode();
        }

        return false;
    }

    bool IsExternalDisplayActive()
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
        }

        if (result != ERROR_SUCCESS)
        {
            log.Debug("Can't query display configuration, error %d", result);
            return false;
        }

        bool external = false;
        for (const DISPLAYCONFIG_PATH_INFO &path : paths)
        {
            const DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY technology = path.targetInfo.outputTechnology;
            const bool builtIn = technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL
                || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED
                || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;

            // OTHER and INDIRECT_VIRTUAL targets are skipped. Most virtual display drivers report HDMI and still count.
            const bool ignored = !path.targetInfo.targetAvailable
                || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_OTHER
                || technology == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL;

            log.Debug("Active display: output technology %d, available %d", (int)technology, (int)path.targetInfo.targetAvailable);

            if (!builtIn && !ignored)
            {
                external = true;
            }
        }

        return external;
    }

    bool ExitWhenDocked()
    {
        // Check once per session, and only when Windows starts in full screen experience
        if (!Config::SmartDockedMode
            || GlobalFindAtom(App::Constants::DockedCheckAtomName)
            || !Registry::ReadBool(App::Constants::GamingHomeAppRegKey, App::Constants::StartupToGamingHomeRegValue)
            || !GamingExperience::IsFullscreenMode())
        {
            return false;
        }

        GlobalAddAtom(App::Constants::DockedCheckAtomName);

        if (!IsExternalDisplayActive())
        {
            return false;
        }

        log.Debug("External display is active, leaving full screen experience");
        GamingExperience::ExitFSEMode();

        // Wait while the exit confirmation is shown and up to 3 seconds after it is closed.
        // Give up after 10 seconds if it never appears, and after 60 seconds in any case.
        DWORD start = GetTickCount();
        DWORD lastGamebar = 0;
        bool wasGamebar = false;
        while (GamingExperience::IsFullscreenMode() && GetTickCount() - start < 60000
            && (wasGamebar ? GetTickCount() - lastGamebar < 3000 : GetTickCount() - start < 10000))
        {
            Sleep(250);
            std::wstring pointProcess = Unicode::to_lower(Process::GetWindowProcessName(WindowFromPoint(POINT{1, 1})));
            std::wstring foregroundProcess = Unicode::to_lower(Process::GetWindowProcessName(GetForegroundWindow()));
            if (pointProcess == L"gamebar.exe" || foregroundProcess == L"gamebar.exe")
            {
                wasGamebar = true;
                lastGamebar = GetTickCount();
            }
        }

        if (GamingExperience::IsFullscreenMode())
        {
            log.Warn("Full screen experience is still active, starting home app");
            return false;
        }

        // The next full screen experience enter in this session is the first launch again (startup apps)
        GlobalDeleteAtom(GlobalFindAtom(App::Constants::PackageAtomName));
        return true;
    }
}
