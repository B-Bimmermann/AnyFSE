# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AnyFSE is a Windows C++17 application that registers as the Home application for the Windows Gaming Full Screen Experience (FSE, "Xbox mode"). It launches a user-configured game launcher (Playnite, Steam Big Picture, LaunchBox BigBox, Armoury Crate SE, RetroBat, Kodi, etc.) instead of the Xbox app. The binaries are installed to `C:\Program Files\AnyFSE`, and the installer registers `AppxManifest.xml` from there as an identity package with external content, so Windows can start `AnyFSE.exe` as a packaged app.

**Upstream:** Artem Shpynov, [github.com/ashpynov/AnyFSE](https://github.com/ashpynov/AnyFSE) (primary: releases, issues). [Codeberg](https://codeberg.org/ashpynov/AnyFSE) is a mirror; the Support page links both, and the updater and online installer try GitHub first and fall back to Codeberg. Git remote `upstream` points to the GitHub repo.
**This fork:** [B-Bimmermann/AnyFSE](https://github.com/B-Bimmermann/AnyFSE). `main` mirrors upstream; the fork's work is on branch `smart-display`. See [Fork Maintenance](#fork-maintenance).

**Read `AGENTS.md` first.** It holds the upstream rules for agents: how to build (below) and the C++ style:
- Lines may be up to 160 characters. Do not wrap code that is readable at that width.
- Put path, registry, protocol, executable and service name literals in `src/App/Constants.hpp`, not at call sites.
- Access the registry only through `Tools::Registry` (alias `Registry::`).
- Use the module namespace aliases. Prefer `namespace c = AnyFSE::App::Constants;` over `using` declarations.

## Build System

**Toolchain:** Visual Studio 2022 / MSBuild, MSVC v143, Windows SDK 10.0.26100.0, x64 only. Build from a VS developer environment (`VsDevCmd.bat`). `build.md` has the full setup, including certificates and publisher renaming (it was AI-generated upstream; check it against the code).

**Output paths:** binaries, payload ZIP and installers go to `build\{Configuration}\`. Intermediates go to per-project folders under `build\objs\...`, and the installer payload is staged in `build\{Configuration}\staging`, which is deleted after a successful build.

### Build Commands

`.vscode/tasks.json` is the source of truth. Read it before building, run the task's command through its `windows.options.shell` (`cmd.exe /C VsDevCmd.bat &&`), run `dependsOn` tasks first, and report a missing or broken task instead of substituting another command. Main tasks (there are also `Clean Debug`, `Package Debug`, `Build AnyFSE.Installer Debug` and a coverage task):

| Task label | Runs | Output |
|---|---|---|
| `Build AnyFSE Debug` / `Build AnyFSE Release` | `msbuild AnyFSE.sln -target:AnyFSE;AnyFSE_Settings;AnyFSE_ACSEFilterHook;AnyFSE_ACSEFilterInjector` | exe + DLLs, no installer |
| `Build AnyFSE.Installer Offline` | `msbuild AnyFSE.Installer.vcxproj -property:Offline=Offline` (Release) | `AnyFSE.Installer.Offline.<ver>-<rev>.exe` (payload ZIP embedded), `AnyFSE.<ver>.zip`, `AnyFSE.<ver>-<rev>.pdb.zip` |
| `Build AnyFSE.Installer Release` / `Debug` | `msbuild AnyFSE.Installer.vcxproj` | online `AnyFSE.Installer.exe` + `AnyFSE.<ver>.zip` |
| `Build AnyFSE.Installer Offline and check with VirusTotal` | `scripts\Test-VirusTotal.ps1` (needs `VIRUSTOTAL_API_KEY`) | scan result |
| `Build AnyFSE.Uninstaller Release` / `Debug` | `msbuild AnyFSE.Uninstaller.vcxproj` | `unins000.exe` |

The installer project builds the runtime binaries and the signed uninstaller through its project references; no task has to run first.

### Code Signing

- **Binaries only:** Release builds of `AnyFSE`, `AnyFSE.Settings`, `ACSEFilterHook` and `ACSEFilterInjector` are signed after the build. `unins000.exe` and the installer are signed in every configuration (`BuildSigned` default target). Signing uses the certificate in `Cert:\CurrentUser\My` whose subject is exactly `CN=$(BinarySigningCN)` and that is code-signing, has a private key and is currently valid. The one that expires last wins. Override with `/p:BinarySigningCN=...`.
- **No package signing:** since upstream 0.90.26 there is no APPX and no package certificate. The `Publisher` in `AppxManifest.xml` (and in `AnyFSE.manifest`) only defines the identity and is independent of `BinarySigningCN`.
- `AnyFSE.Temp.cer` and `Artem.Shpynov.cer` in the repo root are leftovers and are not used by the build or the installer.
- Signing targets run `powershell` (Windows PowerShell 5.1) from MSBuild `Exec`.

### Version Management

`AnyFSE.Version.props` sets `AssemblyVersion` (currently **0.90.26**), `VersionRevision`, `AssemblyCompany`, `AssemblyCopyright` and `BinarySigningCN` (`Artem Shpynov`). Every project imports it, and the values reach the code as `VER_*` preprocessor defines. A successful installer build (`BuildSigned`) increments `VersionRevision` in this file. In the fork, do not commit that change, so that rebases stay clean. The registered manifest version stays `1.0.0.0`; `Constants::FolderRegistrationVersion` (`0.90.26`) tells the installer when an older APPX-based registration has to be removed first.

### CI / GitHub Actions (fork)

`.github/workflows/build.yml`: the **build** job runs on every push and on PRs to `main`; the **package** job on every push (not on PRs). The workflow lives only on the `smart-display` branch, see [Fork Maintenance](#fork-maintenance). All `run` steps default to `shell: cmd`, and `WindowsSdkDir` is set as a workflow environment variable (locally the VS developer shell sets it). The signing targets start Windows PowerShell 5.1, which cannot load the `Cert:` drive when it inherits PowerShell 7's `PSModulePath` from a `pwsh` parent. Certificate steps use `shell: powershell` explicitly.

- **build** job (matrix Debug/Release): for Release it creates a throwaway one-day `CN=AnyFSE-CI` code-signing certificate, so the job needs no secrets and also works for PRs. It builds the solution targets `AnyFSE;AnyFSE_Settings;AnyFSE_ACSEFilterHook;AnyFSE_ACSEFilterInjector` with `/p:BinarySigningCN=AnyFSE-CI` and `/p:WindowsTargetPlatformVersion=10.0.26100.0`, and uploads the four binaries as `AnyFSE-{Configuration}`.
- **package** job (after build):
  - Imports `SIGNING_CERTIFICATE` into `Cert:\CurrentUser\My` (`CN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`, workflow variable `SIGNING_CN`).
  - Runs the `Build AnyFSE.Installer Offline` command with `/p:BinarySigningCN=<SIGNING_CN>`.
  - Uploads the `AnyFSE-Installer` artifact: offline installer, payload ZIP and `.pdb.zip`.
  - Only the **offline** installer is useful for the fork. The online installer downloads `AnyFSE.<ver>.zip` from ashpynov's GitHub/Codeberg releases, so it would install upstream binaries.
- Upstream's `mirror-codeberg.yml` is skipped outside `ashpynov/AnyFSE`.

**Secrets:**
- `SIGNING_CERTIFICATE`: Base64-encoded PFX with private key, subject `CN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`, Code Signing EKU.
- `SIGNING_CERTIFICATE_PASSWORD`: the PFX password.

### Setting Up the Signing Certificate (fork)

The certificate only signs the fork's binaries. Its CN is the old package Publisher CN for historical reasons; the identity in `AppxManifest.xml`, the family name `ArtemShpynov.AnyFSE_by4wjhxmygwn4` and `Constants.hpp` stay upstream's.

```powershell
# 1. Code-signing certificate; the CN must equal SIGNING_CN in build.yml
$cert = New-SelfSignedCertificate -Type CodeSigningCert `
    -Subject "CN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762" `
    -FriendlyName "AnyFSE fork code signing" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -KeyAlgorithm RSA -KeyLength 4096 -HashAlgorithm SHA256 `
    -KeyExportPolicy Exportable -NotAfter (Get-Date).AddYears(5)

# 2. Export the PFX and store it as GitHub secrets (gh prompts for the password value)
$password = Read-Host "PFX password" -AsSecureString
Export-PfxCertificate -Cert $cert -FilePath AnyFSE-signing.pfx -Password $password
[Convert]::ToBase64String([IO.File]::ReadAllBytes("AnyFSE-signing.pfx")) | gh secret set SIGNING_CERTIFICATE
gh secret set SIGNING_CERTIFICATE_PASSWORD

# 3. Never commit the PFX
Remove-Item AnyFSE-signing.pfx
```

Local fork builds with this certificate: add `/p:BinarySigningCN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`.

## Architecture

### Install and Process Model

AnyFSE is **not** a single self-contained process. It has no always-on service of its own: the ACSE injector below is optional. The parts are:

- **Identity package:** `AppxManifest.xml` (with `CustomCapability.SCCD`, `Assets` and a generated `resources.pri`) is copied to `C:\Program Files\AnyFSE` and registered there with `PackageManager.RegisterPackageByUriAsync` (`DeveloperMode`, `ForceUpdateFromAnyVersion`, `ExternalLocationUri` = install folder; `Tools::Packages::InstallPackage`). Release EXEs embed `AnyFSE.manifest`, so direct launches also get the package identity. There is no APPX file and no certificate.
- **Installer** (`AnyFSE.Installer.exe`, requires admin; `/s` silent, `/autoupdate` for the updater flow), `AppInstaller_Install.cpp`:
  1. Enables the Gaming Handheld device form, then unpacks the payload ZIP to `%TEMP%\AnyFSE_install`. The offline build reads the ZIP embedded as a resource; the online build downloads it.
  2. Stops the ACSE injector service and kills `AnyFSE.exe`, then loads the existing config.
  3. If an `InstallLocation` is registered, removes the old version and its files.
  4. Temporarily enables Developer Mode if it is off, and restores the ASUS Optimization service if needed.
  5. Removes an older APPX-based registration (`Packages::GetPackageVersion` < `FolderRegistrationVersion`), copies the files and registers the manifest.
  6. Saves the config again, writes `HKLM\...\Uninstall\AnyFSE` and registers the elevated scheduled task.
  7. Restarts the ACSE injector service if it was running, disables Developer Mode again if it was off, and starts the HID listener.
- **Uninstaller** (`unins000.exe`; `/s` silent, `/u` update), `AppUninstaller.cpp`:
  1. Deletes the task, disables the injector service, kills `AnyFSE.exe` and removes the HID listener autorun value.
  2. Outside update mode, restores the device form.
  3. Removes the package registration (`/u` keeps the package data) and deletes the files and the uninstall key.
- **Home app / settings:** `AnyFSE.exe`, started by Windows as the FSE Home app, from the Jump List (`/FSE`, `/Settings`) or via `anyfse://`. Settings UI lives in `AnyFSE.Settings.dll`.
- **Background HID/hotkey listener:** `AnyFSE.exe /HidListener` (started via `anyfse://HidListener` and the HKCU `Run` value `AnyFSE Hotkeys`) asks the elevated task to start `AnyFSE.exe /HidListenerJob`, which runs the listener. It runs only when FSE hotkeys (`/Hotkeys/Enable`) or ROG Ally button handling (`/AllyHid/Enable` on a supported device) are on. Window class `HIDListener`.
- **Elevated helper:** the scheduled task `\AnyFSE` (highest run level, interactive token) runs `AnyFSE.exe /task`. `Tools::Elevated::Call(name)` signals `Local\AnyFSE.Task.Command.<name>` and runs the task in the caller's session. The elevated instance runs the matching handler registered in `CmdLine::Elevated()`: `StartLauncher`, `StartupApps`, `EnableGamingHandheld`, `RestoreGamingPC` or `ElevatedHidListener`. This avoids runtime UAC prompts for "run as admin" launchers and startup apps and for the `DeviceForm` handheld switch.
- **ACSE Filter Injector** (optional, ROG Ally): a Windows service `ACSEFilterInjector` (`AnyFSE.ACSEFilterInjector.exe --service`; `--create-service` / `--remove-service` from the Ally page). It injects `AnyFSE.ACSEFilterHook.dll` into `AsusOptimization.exe`, where an IAT hook on `ReadFile` filters ASUS-specific HID key reports so AnyFSE can handle the Armoury Crate / Command Center buttons.

### Solution Projects

| Output | Project (solution name) | Description |
|---|---|---|
| `AnyFSE.exe` | `AnyFSE.vcxproj` (`AnyFSE`) | Home app: launcher start, splash, HID listener, elevated task handler |
| `AnyFSE.Settings.dll` | `AnyFSE.Settings.vcxproj` (`AnyFSE.Settings`) | Settings UI, loaded dynamically by `AnyFSE.exe` |
| `unins000.exe` | `AnyFSE.Uninstaller.vcxproj` (`Uninstaller`) | Uninstaller |
| `AnyFSE.ACSEFilterHook.dll` | `AnyFSE.ACSEFilterHook.vcxproj` | Hook DLL injected into ASUS Optimization |
| `AnyFSE.ACSEFilterInjector.exe` | `AnyFSE.ACSEFilterInjector.vcxproj` | Injector Windows service |
| `AnyFSE.Installer[.Offline.*].exe`, `AnyFSE.{ver}.zip` | `AnyFSE.Installer.vcxproj` | Installer + payload ZIP (miniz for extraction) |

**Dependencies:** `AnyFSE.Installer.vcxproj` has project references to AnyFSE, Settings, ACSEFilterHook, ACSEFilterInjector and the Uninstaller (`BuildSigned`). The payload is an explicit `ApplicationFiles` list in its `CreateInstallerArchives` target: the 5 runtime binaries, plus `AppxManifest.xml`, `CustomCapability.SCCD`, `Assets\*`, `localization\*.json` and the `resources.pri` generated by MakePri. Update that list when adding or renaming a runtime binary.

### Application Flow

`WinMain()` in `src/App/Main.cpp`; the command-line handlers are in `src/App/CmdLine.cpp`, helpers such as `IsFirstLaunch()` in `src/App/App.cpp`:

```
Config::Load() → LogManager
Early exit when one of these returns true (in this order):
  CmdLine::Elevated        "/task": run the registered handler (StartLauncher, StartupApps, EnableGamingHandheld,
                           RestoreGamingPC, ElevatedHidListener)
  CmdLine::HidListenerJob  "/HidListenerJob": Ally::HIDListener() loop (hotkeys / Ally buttons)
  CmdLine::HidListener     "/HidListener": start the listener job through the elevated task
  !App::ApiIsAvailable     FSE API missing → error dialog
  RestoreEnterFSEConfirmation(), Ally::EnsureListener(), Localization::Initialize(), JumpList::RegisterJumpList()
  CmdLine::FSE             /FSE, /FSENow, /FSEReboot → EnterFSEMode(Ask | Now | Reboot) unless already in FSE
  CmdLine::Settings        AsSettings() → ShowSettings() (loads AnyFSE.Settings.dll)
  Window::IsRegistered()   main window class "AnyFSE" exists → another instance is active
[fork] App::IsFirstLaunch() && DockedMode::ExitWhenDocked() → exit (Smart Docked Mode)
isRestarting = !IsFirstLaunch() && IsRestarted()   (Playnite restart detection)
First launch in FSE → App::RunStartupApps()
Launcher active / minimized (or in tray for tray-aggressive launchers) → FocusLauncher(); WaitHomeAppExit() → exit
ExitFSE::WaitExitFSEMode()? → waits for an instance that is leaving FSE; exit if FSE ended
Launcher.OnBoot; StartLauncher(AsAdmin) unless the splash video defers it
MainWindow::Run() loop; WaitHomeAppExit() decides whether a launcher restart re-runs the splash
GamingExperience::PreventPopuppers()
```

`IsFirstLaunch()` sets the global atom `PackageAtomName` on the first Home-app launch of the Windows session.

`AsSettings()` returns true when any of these holds:
- There are no arguments.
- There is no config file.
- `/Settings` was passed.
- The launcher is `None` or `Native` (another installed gaming home app such as Xbox).
- `HKCU\...\GamingConfiguration\GamingHomeApp` is not AnyFSE's `AppUserModelId`.
- The launcher executable is missing.

### Application Identity

`AppxManifest.xml` (static, no MakeAppx; registration version fixed at `1.0.0.0`):
- **Identity:** `ArtemShpynov.AnyFSE`, Publisher `CN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`, x64. The family name is `ArtemShpynov.AnyFSE_by4wjhxmygwn4` (`Constants::PackageFamilyName`, `AppUserModelId` = `<PFN>!App`). `AnyFSE.manifest` must match name, publisher and application id.
- **Application:** `AnyFSE.exe` with `uap10:TrustLevel="mediumIL"` and `uap10:RuntimeBehavior="win32App"`, listed in Start (`AppListEntry="default"`).
- **Extensions:** the `windows.gamingApp` app extension makes it selectable as the FSE Home app; there is also the `anyfse://` protocol.
- **Capabilities:** `runFullTrust`, `unvirtualizedResources`, and the custom capability `Microsoft.appCategory.gamingHome_8wekyb3d8bbwe`, authorized by `CustomCapability.SCCD`.
- **Properties:** `AllowExternalContent`, file system and registry write virtualization disabled.
- **Target:** `Windows.Desktop` with MinVersion `10.0.26100.8039` and MaxVersionTested `10.0.26200.7653`. The FSE API (`gamingexperience.h`, delay-loaded `api-ms-win-gaming-experience-l1-1-0.dll`) is checked at runtime (`GamingExperience::ApiIsAvailable`).

### Source Directory Layout

```
src/
  Ally/             # ROG Ally + hotkeys: Ally.cpp (HID/hotkey listener), Handlers.cpp (button actions),
                    #   Services.cpp (ASUS Optimization / injector service control)
    ACSEfilter/     #   Injector.cpp → ACSEFilterInjector.exe; Hook.cpp, IATHook, HidReadFilter, Native → ACSEFilterHook.dll
  App/              # Main exe
                    #   Main.cpp       — WinMain
                    #   CmdLine.cpp    — command-line handlers (/task, /HidListener, /FSE, /Settings)
                    #   App.cpp        — helpers: first launch, Playnite restart, startup apps, settings DLL
                    #   Constants.hpp  — package identity, file/registry/protocol/service names, release URLs
                    #   ExitFSE.cpp    — leaving FSE when the home app exits
                    #   DockedMode.cpp — fork: Smart Docked Mode (display classification, leaving FSE when docked)
                    #   Launchers.cpp  — start/detect/focus launcher, startup apps, Playnite specifics
                    #   MainWindow.cpp, MainWindow_Animation.cpp, MainWindow_Timer.cpp — splash window, animation, timers
                    #   GamingExperience.cpp — FSE API wrapper (Enter/Exit/IsFullscreen, DeviceForm handheld switch)
                    #   JumpList.cpp   — Jump List (/FSE, /Settings)
                    #   VideoPlayer.cpp — splash video playback
  AppSettings/      # Settings DLL: SettingsDialog (+ .Update.cpp), layout, GamepadInput, DetectLaunchers.cpp
    SettingsPages/  #   LauncherPage, SplashPage, StartupPage (+ StartupEditDlg), ConfirmationsPage (FSE prompts,
                    #   hotkeys), AllyHidPage, UpdatePage, TroubleshootPage, SupportPage
  AppInstaller/     # Installer (AppInstaller*.cpp) and uninstaller (AppUninstaller.cpp), Admin, Zip (miniz payload
                    #   extraction), ScheduledTask (elevated task), Installer.rc
  Configuration/    # Config.cpp/.hpp — JSON config as static members; Config.Launchers.cpp — launcher types/defaults
  FluentDesign/     # Win32 GDI+ UI framework: Theme, Button, CheckBox, ComboBox, Dialog, Popup, ScrollView,
                    #   SettingsLine, Static, TextBox, Toggle, Align
  Logging/          # Logger + LogManager: levels Disabled(0) … Trace(6)
  Tools/            # Win32 helpers: Elevated (scheduled-task elevation), Localization, Registry, Process, Window,
                    #   Paths, Packages (PackageManager), Steam (overlay key sequence), PowerEfficiency (throttle
                    #   background processes), Icon, Event, Unicode, Notification, GdiPlus, Minidump, nlohmann/json
  Updater/          # Async update check + self-update (GitHub releases API, Codeberg fallback)
localization/       # <locale>.json UI strings
```

## Key Design Patterns

**Event system:** `src/Tools/Event.hpp` is a multicast delegate over `std::vector<std::function<void()>>`. Subscribe with `+=` and fire with `Notify()`. The `delegate(method)` macro captures `this` as `This`, and `delegateparam(method, param)` also binds an argument.

**Configuration:** `src/Configuration/Config.hpp` keeps all config as static members of `Config`. It is loaded from `AnyFSE.json` with nlohmann/json `json_pointer` paths (e.g. `/Extra/ExitFSEOnHomeExit`, `/AllyHid/Enable`, `/Hotkeys/Enable`, `/Locale`) through `Config::Load()` / `Config::Save()`. The file is in the package's LocalState: `%LOCALAPPDATA%\Packages\ArtemShpynov.AnyFSE_by4wjhxmygwn4\LocalState\`.

**Constants:** identifiers (paths, registry keys, protocol, exe, task and service names, package identity, release URLs) live in `src/App/Constants.hpp` (see AGENTS.md).

**Elevation:** never add UAC prompts in the Home-app path. Register a handler in `CmdLine::Elevated()` and call it with `Elevated::Call(Constants::...)`.

**Localization:** UI strings are flat key/value JSON files in `localization/` (`"language"` holds the display name).
- **Lookup:** use `Translate(L"key")` / `TranslateF(L"key", ...)` from `Tools/Localization.hpp`. `en_US.json` is always loaded as the base, and the selected locale is overlaid on it, so missing keys fall back to English.
- **Where the files come from:** the exe and Settings DLL read `%PROGRAMDATA%\AnyFSE\Localization` first, then `<exe dir>\Localization` (copied by the `CopyLocalization` target and the installer payload). The installer and uninstaller embed the files as `LOCALIZATION` resources listed in `src/AppInstaller/Installer.rc`.
- **Choosing a language:** Settings, installer and uninstaller each have a language button. Settings saves the choice to `/Locale`. The installer and uninstaller read `/Locale` as their default; otherwise the Windows user locale is used.
- **Adding strings:** add new keys to `en_US.json` and, where possible, to every locale.
- **Adding a language:** add the JSON file and a line in `Installer.rc`.

**Settings pages:** each page inherits `SettingsPage` and implements `AddPage()`, `LoadControls()` and `SaveControls()`. Build rows with `SettingsDialog::AddSettingsLine()` (Toggle/ComboBox/TextBox/Button) and use `SettingsLine::AddGroupItem()` for collapsible groups.

**FluentDesign:** a custom Win32 GDI+ UI framework in `src/FluentDesign/`. `Theme` manages dark/light, DPI, accent and named color slots. Controls derive from `FluentControl`. All UI (settings, installer, uninstaller) uses these controls.

**Settings DLL:** `AnyFSE.Settings.dll` exports a single `Main()` (`AppSettings.def`), loaded by `App::CallLibrary()`.

**Launcher detection:**
- `Config::GetConfiguredLauncher()` maps the start command to a `LauncherType`: Custom, Native, PlayniteFullscreen/Desktop, ArmouryCrate, SteamBigPicture, Steam, BigBox, OneGameLauncher, RetroBat, Kodi, Cortex or PocketDeck.
- `Config::GetLauncherDefaults()` fills in process, window class and title defaults.
- Discovery (`FindInstalledLaunchers` / `FindNotInstalledLaunchers`) lives in `src/AppSettings/DetectLaunchers.cpp`.

**CRT:** AnyFSE, Settings, Installer and Uninstaller link the static CRT (`/MT`, `/MTd`). The ACSE hook and injector use the project default.

## Custom Features (Fork)

### Smart Docked Mode

When Windows starts in FSE while the device is docked, AnyFSE leaves FSE instead of starting the launcher. Follow-up to
ashpynov/AnyFSE#77 (closed upstream; branch `smart-docked-mode` holds the 0.90.18 version).

- **UI:** opt-in toggle on the LauncherPage below "Leave full screen experience on home app exit": "Leave full screen experience
  when docked" (`settingsLeaveFseWhenDocked` / `...Description`, in all locale files). Shown/enabled like the other AnyFSE launcher options.
- **Config:** `/Extra/SmartDockedMode` (bool, default `false`) → `Config::SmartDockedMode`.
- **Call site:** `src/App/Main.cpp`, after the early-exit block: `if (App::IsFirstLaunch() && DockedMode::ExitWhenDocked()) return 0;`.
- **`DockedMode::ExitWhenDocked()`** (`src/App/DockedMode.cpp`):
  - Runs only if the option is on, the `DockedCheckAtomName` global atom is not set yet (once per session), `StartupToGamingHome` is
    set (Windows starts in FSE; `RestoreEnterFSEConfirmation()` has already undone the temporary value of a `/FSEReboot` entry) and FSE is active.
  - Sets the atom, then checks `IsExternalDisplayActive()`. If docked: `GamingExperience::ExitFSEMode()`, then waits while the Game Bar
    exit confirmation is shown (`gamebar.exe` at `WindowFromPoint(1,1)` or as foreground window) and up to 3 s after it closed;
    without a confirmation it gives up after 10 s, and after 60 s in any case.
  - Still in FSE (cancelled/timeout) → returns false and the launcher starts as usual. Left FSE → deletes the `PackageAtomName` atom so
    the next FSE entry of the session is a first launch again (startup apps run), and returns true.
- **Detection (`IsExternalDisplayActive()`):** every active path of `QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)` (retry on
  `ERROR_INSUFFICIENT_BUFFER`) is described with `DisplayConfigGetDeviceInfo` (source GDI name, monitor name and device path, base output
  technology, adapter device path) and, on an own MTA thread, `Windows.Devices.Display.DisplayMonitor::FromInterfaceIdAsync(monitorDevicePath)`
  (connection kind, physical connector, EDID size). The first matching rule classifies a display:
  1. target not available → ignored
  2. output or base technology `INTERNAL`, `DISPLAYPORT_EMBEDDED` or `UDI_EMBEDDED`, or connection kind `Internal` → built-in
  3. technology `OTHER` / `INDIRECT_VIRTUAL`, or connection kind `Virtual` → ignored
  4. adapter device path `\\?\ROOT#` or `\\?\SWD#` (virtual display drivers, `Constants::VirtualAdapterPathPrefixes`) → ignored
  5. `MIRACAST` or connection kind `Wireless` → ignored (not a dock)
  6. EDID diagonal below 10 inches → built-in (handheld panel whose driver reports an external connector)
  7. otherwise → external

  Docked = at least one external display. Query errors count as not docked; without DisplayMonitor the rules use the
  `QueryDisplayConfig` data only.
- **Logging:** all gates, every display (all fields above, classification and reason), the summary, the Game Bar confirmation and the
  result are logged at **Info**. The legacy `SM_CMONITORS` / `HORZSIZE` values are logged once for comparison.
- **Earlier designs, removed:** monitor count plus `GetDeviceCaps(HORZSIZE)` against a mm threshold (January and March 2026). `HORZSIZE`
  is computed from resolution/DPI, not read from EDID, so a handheld panel always looked external and FSE was left on every start.
  The 0.90.18 version trusted the reported output technology only.

### German Localization

`localization/de_DE.json` (German), plus its `LOCALIZATION` entry in `src/AppInstaller/Installer.rc`. After a rebase, compare its keys
with `en_US.json`: translate new upstream keys and drop removed ones.

## Fork Maintenance

- **Branches:** `main` mirrors `upstream/main` exactly and is never committed to. All fork work lives on `smart-display`, named after
  its main feature but holding every fork change: `upstream/main` plus a few fork commits (Smart Docked Mode in two commits, de_DE, CI
  and this CLAUDE.md). Old states: `main-backup` (March 2026), `main-backup-0.90.18` (the fork as of September 2026) and
  `smart-docked-mode` (the 0.90.18 feature as offered upstream).
- **Updating:** first sync `main` (GitHub "Sync fork" button, or `git push origin upstream/main:main`, a fast-forward). Then rebase
  `smart-display` instead of merging: `git switch smart-display && git fetch upstream && git rebase upstream/main`, resolve conflicts,
  check `de_DE.json` against `en_US.json`, and `git push --force-with-lease origin smart-display`.
- **Builds:** the workflow exists only on `smart-display`, so every push to it builds the offline installer. Manual `workflow_dispatch`
  is not available, because GitHub only offers it for workflows on the default branch (`main`).
- Keep fork changes small and in upstream style, so that they rebase cleanly and can be offered upstream. Fork-only code lives in its
  own files where possible (`DockedMode.cpp`).
- The in-app updater and the online installer point to **ashpynov's** releases (GitHub, then Codeberg). Fork builds carry upstream's version number, so the updater only offers a newer upstream release, and installing it **replaces the fork build** (and removes the fork features). Distribute fork builds as the CI's offline installer.

## Debugging Notes

- **VS Code launch configs** (`.vscode/launch.json`, cwd `build\Debug`):
  - "Debug AnyFSE Launcher" (`anyfse://launcher`)
  - "Debug AnyFSE Settings" (`/settings`)
  - "Debug AnyFSE HidListener" (`/HidListener`)
  - "Debug AnyFSE FSE Now" (`/fse-now`, which matches `/FSE` in Ask mode, not `/FSENow`)
  - "Debug AnyFSE.Installer"
  - The "Debug Uninstaller" config refers to a task (`Build AnyFSE.Uninstaller Debug`) and folder (`bin\Debug`) that do not exist.
- **Config for unpackaged runs:** a debug build run from `build\Debug` has no package identity, so `Paths` falls back to `%LOCALAPPDATA%\Packages\<PFN>\...` and uses the installed app's config.
- **Instance and launch markers:**
  - The main window class `AnyFSE` prevents duplicate instances.
  - The listener uses class `HIDListener`.
  - A global atom named after the package family marks "already launched this session" (`App::IsFirstLaunch()`).
  - The global atom `ArtemShpynov.AnyFSE_DockedCheck` marks that the Smart Docked Mode check already ran this session.
- **Logs:** set the level at `/Log/Level` (0 = Disabled … 6 = Trace; default Disabled). Smart Docked Mode needs at least Info (4). Logs go to `...\Packages\ArtemShpynov.AnyFSE_by4wjhxmygwn4\LocalCache\logs`, and crash dumps to `...\TempState\dumps`.
