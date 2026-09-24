# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AnyFSE is a Windows C++17 application that registers as the Home application for the Windows Gaming Full Screen Experience (FSE, "Xbox mode"). It launches a user-configured game launcher (Playnite, Steam Big Picture, LaunchBox BigBox, Armoury Crate SE, RetroBat, Kodi, etc.) instead of the Xbox app. Windows starts `AnyFSE.exe` through a sparse APPX identity package; the binaries themselves are installed to `C:\Program Files\AnyFSE`.

**Upstream:** Artem Shpynov, [github.com/ashpynov/AnyFSE](https://github.com/ashpynov/AnyFSE) (primary: releases, issues). [Codeberg](https://codeberg.org/ashpynov/AnyFSE) is a mirror; the Support page links both, and the updater and online installer try GitHub first and fall back to Codeberg. Git remote `upstream` points to the GitHub repo.
**This fork:** [B-Bimmermann/AnyFSE](https://github.com/B-Bimmermann/AnyFSE). See [Fork Maintenance](#fork-maintenance).

**Read `AGENTS.md` first.** It holds the upstream rules for agents: how to build (below) and the C++ style:
- Lines may be up to 160 characters. Do not wrap code that is readable at that width.
- Put path, registry, protocol, executable and service name literals in `src/App/Constants.hpp`, not at call sites.
- Access the registry only through `Tools::Registry` (alias `Registry::`).
- Use the module namespace aliases. Prefer `namespace c = AnyFSE::App::Constants;` over `using` declarations.

## Build System

**Toolchain:** Visual Studio 2022 / MSBuild, MSVC v143, Windows SDK 10.0.26100.0, x64 only. Build from a VS developer environment (`VsDevCmd.bat`). `build.md` has the full setup, including certificates and publisher renaming (it was AI-generated upstream; check it against the code).

**Output paths:** binaries, APPX, CAB and installers go to `build\{Configuration}\`. Intermediates go to `build\objs\...`: per-project folders, package contents in `build\objs\Package\{Configuration}\x64`, and installer staging in `build\{Configuration}\staging`, which is deleted after the build.

### Build Commands

`.vscode/tasks.json` is the source of truth. Read it before building, run the task's command through its `windows.options.shell` (`cmd.exe /C VsDevCmd.bat &&`), run `dependsOn` tasks first, and report a missing or broken task instead of substituting another command. Main tasks (there are also `Clean Debug`, `Package Debug`, `Build AnyFSE.Installer Debug` and a coverage task):

| Task label | Runs | Output |
|---|---|---|
| `Build AnyFSE Debug` / `Build AnyFSE Release` | `msbuild AnyFSE.sln -target:AnyFSE;AnyFSE_Settings;AnyFSE_ACSEFilterHook;AnyFSE_ACSEFilterInjector` | exe + DLLs, no package |
| `Package` | `msbuild AnyFSE.sln /p:Configuration=Release -target:AnyFSE_Package` | signed `AnyFSE-<ver>.identity.appx` |
| `Build AnyFSE.Installer Release` (after `Package`) | `msbuild AnyFSE.Installer.vcxproj` | online `AnyFSE.Installer.exe` + `AnyFSE.<ver>.cab` |
| `Build AnyFSE.Installer Offline` (after `Package`) | same with `-property:Offline=Offline` | `AnyFSE.Installer.Offline.<ver>-<rev>.exe` (CAB embedded) |
| `Build AnyFSE.Installer Offline and check with VirusTotal` | `scripts\Test-VirusTotal.ps1` (needs `VIRUSTOTAL_API_KEY`) | scan result |
| `Build AnyFSE.Uninstaller Release` | `msbuild AnyFSE.Uninstaller.vcxproj` | `unins000.exe` |

One-shot offline release build (from `build.md`):

```bat
msbuild AnyFSE.sln /m /p:Configuration=Release /p:Platform=x64 /p:Offline=Offline /p:WindowsTargetPlatformVersion=10.0.26100.0
```

### Code Signing

- **Binaries:** Release builds of `AnyFSE`, `AnyFSE.Settings`, `ACSEFilterHook` and `ACSEFilterInjector` are signed after the build. `unins000.exe` and the installer are signed in every configuration (`BuildSigned` default target). Signing uses the certificate in `Cert:\CurrentUser\My` whose subject is exactly `CN=$(BinarySigningCN)` and that is code-signing, has a private key and is currently valid. The one that expires last wins. Override with `/p:BinarySigningCN=...`.
- **APPX:** `AnyFSE.Package.vcxproj` signs with the first certificate whose subject contains `$(PublisherCN)`. It must match `Publisher="CN=..."` in `AppxManifest.xml`. The thumbprint is looked up at build time and is not stored in the project.
- **`AnyFSE.Temp.cer`** (repo root) is the public part of the package-signing certificate. The installer copies it into `LocalMachine\TrustedPeople` only while it registers the package, then removes it. `Artem.Shpynov.cer` (public binary-signing certificate) is not used by the build.
- Signing targets run `powershell` (Windows PowerShell 5.1) from MSBuild `Exec`.
- `AnyFSE.Package.vcxproj` does not import the C++ props. Outside a VS developer shell, pass `/p:WindowsSdkDir=...`.

### Version Management

`AnyFSE.Version.props` sets `AssemblyVersion` (currently **0.90.18**), `VersionRevision`, `AssemblyCompany`, `AssemblyCopyright`, `BinarySigningCN` (`Artem Shpynov`) and `PublisherCN` (`DDCC7751-898D-4BC9-B80C-4AA73E5D5762`). Every project imports it, and the values reach the code as `VER_*` preprocessor defines. The `Package` target increments `VersionRevision` in this file on each package build. In the fork, do not commit that change, so that rebases stay clean.

### CI / GitHub Actions (fork)

`.github/workflows/build.yml`: the **build** job runs on every push and on PRs to `main`; the **package** job only on `main` and on manual `workflow_dispatch`. All `run` steps default to `shell: cmd`, and `WindowsSdkDir` is set as a workflow environment variable (locally the VS developer shell sets it). The signing targets start Windows PowerShell 5.1, which cannot load the `Cert:` drive when it inherits PowerShell 7's `PSModulePath` from a `pwsh` parent. Certificate steps use `shell: powershell` explicitly.

- **build** job (matrix Debug/Release): for Release it creates a throwaway one-day `CN=AnyFSE-CI` code-signing certificate, so the job needs no secrets and also works for PRs. It builds the solution targets `AnyFSE;AnyFSE_Settings;AnyFSE_ACSEFilterHook;AnyFSE_ACSEFilterInjector` with `/p:BinarySigningCN=AnyFSE-CI` and `/p:WindowsTargetPlatformVersion=10.0.26100.0`, and uploads the four binaries as `AnyFSE-{Configuration}`.
- **package** job (after build; only on `main` or manual dispatch):
  - Imports `SIGNING_CERTIFICATE` into `Cert:\CurrentUser\My`. It is one certificate, `CN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`, used for both binaries and APPX.
  - Exports its public part over `AnyFSE.Temp.cer`.
  - Builds the full solution in Release with `/p:Offline=Offline` and `/p:BinarySigningCN=<PublisherCN>`.
  - Uploads the `AnyFSE-Installer` artifact: offline installer, identity APPX, CAB, `.pdb.zip` and `AnyFSE.Temp.cer`.
  - Only the **offline** installer is useful for the fork. The online installer downloads `AnyFSE.<ver>.cab` from ashpynov's GitHub/Codeberg releases, so it would install upstream binaries.

**Secrets:**
- `SIGNING_CERTIFICATE`: Base64-encoded PFX with private key, subject `CN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`, Code Signing EKU.
- `SIGNING_CERTIFICATE_PASSWORD`: the PFX password.

### Setting Up the Signing Certificate (fork)

The fork keeps upstream's Publisher CN, so the package identity, the family name `ArtemShpynov.AnyFSE_by4wjhxmygwn4` and `Constants.hpp` stay unchanged. Only the key differs.

```powershell
# 1. Code-signing certificate; the CN must equal PublisherCN / the AppxManifest Publisher
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

# 3. Public certificate into the repo root (the fork commits it; CI re-exports it from the secret)
Export-Certificate -Cert $cert -FilePath AnyFSE.Temp.cer

# 4. Never commit the PFX
Remove-Item AnyFSE-signing.pfx
```

Local fork builds with this certificate: add `/p:BinarySigningCN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`. To trust test packages without the installer, import `AnyFSE.Temp.cer` into `Cert:\LocalMachine\TrustedPeople` (as admin).

## Architecture

### Install and Process Model

AnyFSE is **not** a single self-contained process. It has no always-on service of its own: the ACSE injector below is optional. The parts are:

- **Identity package:** `AnyFSE-<ver>.identity.appx` is a sparse package (`uap10:AllowExternalContent`, built with `MakeAppx pack /nv`). It contains only the manifest, Assets, `CustomCapability.SCCD` and `resources.pri`, and is registered with `ExternalLocationUri = %ProgramFiles%\AnyFSE` (`Tools::Packages::InstallPackage`).
- **Installer** (`AnyFSE.Installer.exe`, requires admin; `/autoupdate` for the updater flow):
  1. Unpacks the payload CAB to `%TEMP%\AnyFSE_install`. The offline build uses the CAB embedded as a resource; the online build downloads it.
  2. Runs the old `unins000.exe /s /u` (update mode).
  3. Temporarily enables Developer Mode if it is off.
  4. Trusts `AnyFSE.Temp.cer`.
  5. Copies the files to `C:\Program Files\AnyFSE` and registers the package.
  6. Removes the certificate.
  7. Writes `HKLM\...\Uninstall\AnyFSE` and registers the elevated scheduled task.
  8. Restarts the ACSE injector service if it was running.
  9. Starts the HID listener.
- **Uninstaller** (`unins000.exe`; `/s` silent, `/u` update):
  1. Deletes the task, disables the injector service and kills `AnyFSE.exe`.
  2. Deletes the files.
  3. Outside update mode, it also restores the device form and removes the package.
  4. Removes the certificates and the uninstall key.
- **Home app / settings:** `AnyFSE.exe`, started by Windows as the FSE Home app, from the Jump List (`/FSE`, `/Settings`) or via `anyfse://`. Settings UI lives in `AnyFSE.Settings.dll`.
- **Background HID/hotkey listener:** `AnyFSE.exe /HidListener`, started via `anyfse://HidListener` and the HKCU `Run` value `AnyFSE Hotkeys`. It runs only when FSE hotkeys (`/Hotkeys/Enable`) or ROG Ally button handling (`/AllyHid/Enable` on a supported device) are on. Window class `HIDListener`.
- **Elevated helper:** the scheduled task `\AnyFSE` (highest run level, interactive token) runs `AnyFSE.exe /task`. `Tools::Elevated::Call(name)` signals `Local\AnyFSE.Task.Command.<name>` and runs the task in the caller's session. The elevated instance runs the matching handler registered in `WinMain`: `StartLauncher`, `StartupApps`, `EnableGamingHandheld` or `RestoreGamingPC`. This avoids runtime UAC prompts for "run as admin" launchers and startup apps and for the `DeviceForm` handheld switch.
- **ACSE Filter Injector** (optional, ROG Ally): a Windows service `ACSEFilterInjector` (`AnyFSE.ACSEFilterInjector.exe --service`; `--create-service` / `--remove-service` from the Ally page). It injects `AnyFSE.ACSEFilterHook.dll` into `AsusOptimization.exe`, where an IAT hook on `ReadFile` filters ASUS-specific HID key reports so AnyFSE can handle the Armoury Crate / Command Center buttons.

### Solution Projects

| Output | Project (solution name) | Description |
|---|---|---|
| `AnyFSE.exe` | `AnyFSE.vcxproj` (`AnyFSE`) | Home app: launcher start, splash, HID listener, elevated task handler |
| `AnyFSE.Settings.dll` | `AnyFSE.Settings.vcxproj` (`AnyFSE.Settings`) | Settings UI, loaded dynamically by `AnyFSE.exe` |
| `unins000.exe` | `AnyFSE.Uninstaller.vcxproj` (`Uninstaller`) | Uninstaller |
| `AnyFSE.ACSEFilterHook.dll` | `AnyFSE.ACSEFilterHook.vcxproj` | Hook DLL injected into ASUS Optimization |
| `AnyFSE.ACSEFilterInjector.exe` | `AnyFSE.ACSEFilterInjector.vcxproj` | Injector Windows service |
| `AnyFSE-{ver}.identity.appx` | `AnyFSE.Package.vcxproj` (`AnyFSE.Package`) | Sparse identity package (MakePri, MakeAppx, signtool) |
| `AnyFSE.Installer[.Offline.*].exe`, `AnyFSE.{ver}.cab` | `AnyFSE.Installer.vcxproj` | Installer + payload CAB |

**Dependencies (`AnyFSE.sln`):** `AnyFSE.Package` depends on AnyFSE, Settings, Uninstaller, ACSEFilterHook and ACSEFilterInjector. `AnyFSE.Installer` depends on `AnyFSE.Package`. The installer payload is an explicit `ApplicationFiles` list in `AnyFSE.Installer.vcxproj`: the 5 runtime binaries, `AnyFSE.Temp.cer`, the identity APPX, package resources and `localization\*.json`. Update that list when adding or renaming a runtime binary.

### Application Flow

`App::WinMain()` in `src/App/App.cpp`:

```
Config::Load() → Localization::Initialize(Config::Locale) → LogManager
  "/task" (AsElevated)?     → Elevated::CallHandler() for the requested handler → exit
  GamingExperience::RestoreEnterFSEConfirmation()
  /HidListener?             → Ally::HIDListener() loop (hotkeys / Ally buttons) → exit
  Ally::CheckListener()     → listener enabled but not running? start anyfse://HidListener
  FindWindow("AnyFSE")      → another instance is active → exit
  JumpList::RegisterJumpList(); FSE API unavailable → error dialog → exit
  bFirstLaunch = global atom PackageAtomName not yet set (first Home-app launch this session)
  /FSE, /FSENow, /FSEReboot → EnterFSEMode(Ask | Now | Reboot) unless already in FSE → exit
  AsSettings()?             → ShowSettings() (loads AnyFSE.Settings.dll) → exit
  [fork] bFirstLaunch && ExitFSE::ExitWhenDocked() → exit (Smart Docked Mode)
  Launchers::IsLauncherActiveOrMinimized()? → FocusLauncher(); ExitFSE::WaitHomeAppExit() → exit
  ExitFSE::WaitExitFSEMode()? → waits for an instance that is leaving FSE; exit if FSE ended
  Playnite restart detection (non-first launch in FSE)
  First launch in FSE: startup apps (AsAdmin ones via Elevated::ElevatedStartupApps(), then the rest)
  Launchers::LauncherOnBoot(); Config::AsAdmin ? Elevated::Call(StartLauncher) : Launchers::StartLauncher()
  Splash MainWindow → RunLoop
  ExitFSE::WaitHomeAppExit()  (only with "Leave full screen experience on home app exit")
```

`AsSettings()` returns true when any of these holds:
- There are no arguments.
- There is no config file.
- `/Settings` was passed.
- The launcher is `None` or `Native` (another installed gaming home app such as Xbox).
- `HKCU\...\GamingConfiguration\GamingHomeApp` is not AnyFSE's `AppUserModelId`.
- The launcher executable is missing.

### APPX Packaging

`AppxManifest.xml`:
- **Identity:** `ArtemShpynov.AnyFSE`, Publisher `CN=DDCC7751-898D-4BC9-B80C-4AA73E5D5762`, x64. The family name is `ArtemShpynov.AnyFSE_by4wjhxmygwn4` (`Constants::PackageFamilyName`, `AppUserModelId` = `<PFN>!App`).
- **Application:** `Windows.FullTrustApplication`, `uap18:RuntimeBehavior="win32App"`.
- **Extensions:** the `windows.gamingApp` app extension makes it selectable as the FSE Home app; there is also the `anyfse://` protocol.
- **Capabilities:** `runFullTrust`, `unvirtualizedResources`, and the custom capability `Microsoft.appCategory.gamingHome_8wekyb3d8bbwe`, authorized by `CustomCapability.SCCD`.
- **Properties:** `AllowExternalContent`, `RegistryWriteVirtualization=disabled`.
- **Target:** `Windows.Desktop` with MinVersion `10.0.26100.8039` and MaxVersionTested `10.0.26200.7653`. The FSE API (`gamingexperience.h`, delay-loaded `api-ms-win-gaming-experience-l1-1-0.dll`) is checked at runtime (`GamingExperience::ApiIsAvailable`).

### Source Directory Layout

```
src/
  Ally/             # ROG Ally + hotkeys: Ally.cpp (HID/hotkey listener), Handlers.cpp (button actions),
                    #   Services.cpp (ASUS Optimization / injector service control)
    ACSEfilter/     #   Injector.cpp → ACSEFilterInjector.exe; Hook.cpp, IATHook, HidReadFilter, Native → ACSEFilterHook.dll
  App/              # Main exe
                    #   App.cpp        — WinMain, mode selection
                    #   Constants.hpp  — package identity, file/registry/protocol/service names, release URLs
                    #   ExitFSE.cpp    — leaving FSE: wait for home app exit; fork: ExitWhenDocked()
                    #   Launchers.cpp  — start/detect/focus launcher, startup apps, Playnite specifics
                    #   MainWindow.cpp, MainWindow_Animation.cpp — splash window + animation
                    #   GamingExperience.cpp — FSE API wrapper (Enter/Exit/IsFullscreen, DeviceForm handheld switch)
                    #   JumpList.cpp   — Jump List (/FSE, /Settings)
                    #   VideoPlayer.cpp — splash video playback
  AppSettings/      # Settings DLL: SettingsDialog (+ .Update.cpp), layout, GamepadInput, DetectLaunchers.cpp
    SettingsPages/  #   LauncherPage, SplashPage, StartupPage (+ StartupEditDlg), ConfirmationsPage (FSE prompts,
                    #   hotkeys), AllyHidPage, UpdatePage, TroubleshootPage, SupportPage
  AppInstaller/     # Installer (AppInstaller*.cpp) and uninstaller (AppUninstaller.cpp), Admin, Cabinet (FDI CAB
                    #   extraction), Certificate (TrustedPeople/Root), ScheduledTask (elevated task), Installer.rc
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

**Elevation:** never add UAC prompts in the Home-app path. Register a handler in `App::WinMain`'s `AsElevated` block and call it with `Elevated::Call(Constants::...)`.

**Localization:** UI strings are flat key/value JSON files in `localization/` (`"language"` holds the display name).
- **Lookup:** use `Translate(L"key")` / `TranslateF(L"key", ...)` from `Tools/Localization.hpp`. `en_US.json` is always loaded as the base, and the selected locale is overlaid on it, so missing keys fall back to English.
- **Where the files come from:** the exe and Settings DLL read `%PROGRAMDATA%\AnyFSE\Localization` first, then `<exe dir>\Localization` (copied by the `CopyLocalization` target and the installer payload). The installer and uninstaller embed the files as `LOCALIZATION` resources listed in `src/AppInstaller/Installer.rc`.
- **Choosing a language:** Settings, installer and uninstaller each have a language button. Settings saves the choice to `/Locale`. The installer and uninstaller read `/Locale` as their default; otherwise the Windows user locale is used.
- **Adding strings:** add new keys to `en_US.json` and, where possible, to every locale.
- **Adding a language:** add the JSON file and a line in `Installer.rc`. Upstream `tr_TR.json` currently has no `Installer.rc` entry.

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

When Windows starts in FSE while the device is docked, AnyFSE leaves FSE instead of starting the launcher. Offered upstream as
branch `smart-docked-mode` (follow-up to ashpynov/AnyFSE#77).

- **UI:** opt-in toggle on the LauncherPage below "Leave full screen experience on home app exit": "Leave full screen experience
  when docked" (`settingsLeaveFseWhenDocked` / `...Description`, in all locale files). Shown/enabled like the other AnyFSE launcher options.
- **Config:** `/Extra/SmartDockedMode` (bool, default `false`) → `Config::SmartDockedMode`.
- **Call site:** `App::WinMain`, right after the `AsSettings()` check: `if (bFirstLaunch && ExitFSE::ExitWhenDocked()) return 0;`.
- **`ExitFSE::ExitWhenDocked()`** (`src/App/ExitFSE.cpp`):
  - Runs only if the option is on, the `DockedCheckAtomName` global atom is not set yet (once per session), `StartupToGamingHome` is
    set (Windows starts in FSE; `RestoreEnterFSEConfirmation()` has already undone the temporary value of a `/FSEReboot` entry) and FSE is active.
  - Sets the atom, then checks `IsExternalDisplayActive()`. If docked: `GamingExperience::ExitFSEMode()`, then waits while the Game Bar
    exit confirmation is shown (`gamebar.exe` at `WindowFromPoint(1,1)` as in `WaitHomeAppExit`, or as foreground window) and up to
    3 s after it closed; without a confirmation it gives up after 10 s, and after 60 s in any case.
  - Still in FSE (cancelled/timeout) → returns false and the launcher starts as usual. Left FSE → deletes the `PackageAtomName` atom so
    the next FSE entry of the session is a first launch again (startup apps run), and returns true.
- **Detection (`IsExternalDisplayActive()`):** `QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS)` with retry on `ERROR_INSUFFICIENT_BUFFER`.
  Docked = at least one active path with `targetAvailable` and an `outputTechnology` other than `INTERNAL`, `DISPLAYPORT_EMBEDDED`,
  `UDI_EMBEDDED`, `OTHER` or `INDIRECT_VIRTUAL`. Each path is logged at Debug level. Query errors count as not docked.
  Known limitation: most virtual display drivers (e.g. for Sunshine) report HDMI and count as external.
- **Old design, removed:** monitor count plus `GetDeviceCaps(HORZSIZE)` against `/Extra/SmartDockedThresholdMm`. On WDDM drivers
  `HORZSIZE` is computed from resolution/DPI, not read from EDID, and `SM_CMONITORS` is 1 in duplicate and external-only mode.

### German Localization

`localization/de_DE.json` (German), plus its `LOCALIZATION` entry in `src/AppInstaller/Installer.rc`.

## Fork Maintenance

- The fork is kept as `upstream/main` plus a few fork commits: Smart Docked Mode, de_DE, CI and this CLAUDE.md.
- To update it, rebase instead of merging: `git fetch upstream && git rebase upstream/main`, then `git push --force-with-lease origin main`.
- Keep fork changes small and in upstream style, so that they rebase cleanly and can be offered upstream.
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
  - A global atom named after the package family marks "already launched this session" (`bFirstLaunch`).
- **Logs:** set the level at `/Log/Level` (0 = Disabled … 6 = Trace). Logs go to `...\Packages\ArtemShpynov.AnyFSE_by4wjhxmygwn4\LocalCache\logs`, and crash dumps to `...\TempState\dumps`.
