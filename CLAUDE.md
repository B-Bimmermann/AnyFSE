# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

AnyFSE is a Windows C++17 APPX-packaged application that registers as the Home application for Windows Gaming Full Screen Experience (FSE) mode, launching a user-configured game launcher (Playnite, Steam Big Picture, LaunchBox, etc.) instead of the Xbox app. It runs as a single process with no background service — Windows calls it directly via the APPX protocol.

**Upstream:** Originally by Artem Shpynov. Upstream moved from GitHub to [Codeberg](https://codeberg.org/ashpynov/AnyFSE). This fork is at [B-Bimmermann/AnyFSE](https://github.com/B-Bimmermann/AnyFSE).

## Build System

**Toolchain:** Visual Studio 2022 / MSBuild with MSVC v143 (platform toolset), targeting x64 only. Requires VsDevCmd.bat environment.

**Output paths:** `build\{Configuration}\` for binaries, `build\objs\{ProjectName}\{Configuration}\` for intermediates.

### Build Commands

```bash
# Debug build (AnyFSE.exe + AnyFSE.Settings.dll)
msbuild AnyFSE.sln -property:Configuration=Debug -property:Platform=x64

# Release build + APPX package
msbuild AnyFSE.sln /p:Configuration=Release /p:Platform=x64

# Installer (requires Package build first)
msbuild AnyFSE.Installer.vcxproj -property:Configuration=Release -property:Platform=x64
```

VS Code: `Ctrl+Shift+B` offers preconfigured tasks ("Build AnyFSE Debug", "Package", etc.).

### Version Management

Version is defined in `AnyFSE.Version.props` (`AssemblyVersion`, currently 0.90.6). `VersionRevision` auto-increments on each package build. All projects import this file for shared version info injected via preprocessor defines.

## Architecture

### Single-Process APPX Model

Since v0.90.x, AnyFSE is a **single-process** Full Trust APPX application. There is no background service, no ETW monitoring, no IPC — Windows directly invokes `AnyFSE.exe` as the registered FSE Home application via the `AppxManifest.xml` gaming capability.

### Solution Projects

| Output | Project | Description |
|---|---|---|
| `AnyFSE.exe` | `AnyFSE.vcxproj` | Main app: launcher, splash screen, settings host |
| `AnyFSE.Settings.dll` | `AnyFSE.Settings.vcxproj` | Settings UI, loaded dynamically by `AnyFSE.exe` |
| `AnyFSE-{ver}.appx` | `AnyFSE.Package.vcxproj` | APPX packaging (MakeAppx + signtool) |
| `AnyFSE.Installer.exe` | `AnyFSE.Installer.vcxproj` | Standalone installer with embedded APPX |

**Dependency chain:** Settings DLL → Main EXE → Package → Installer

### Application Flow

`App::WinMain()` in `src/App/App.cpp` determines the execution mode:

```
Config::Load()
  → Duplicate instance check (FindWindow)
  → /FSE arg? → EnterFSEMode()
  → AsSettings()? → ShowSettings() (loads Settings DLL)
  → Smart Docked Mode check → ExitFSEMode() if docked
  → Launcher already active? → FocusLauncher()
  → StartLauncher() → Show splash → Message loop → Exit
```

`AsSettings()` returns true when: no args, no config, `/Settings` flag, Launcher=None/Xbox, or AnyFSE is not the registered GamingHomeApp in the registry.

### APPX Packaging

`AppxManifest.xml` registers AnyFSE as:
- **Identity:** `ArtemShpynov.AnyFSE` (Full Trust)
- **Capability:** `Microsoft.appCategory.gamingHome` (Gaming Home application)
- **Protocol:** `anyfse://` (custom protocol handler)
- **Min Windows version:** 10.0.26200 (Windows 11 25H2)

The Package project runs MakeAppx.exe → MakePri.exe → signtool.exe to produce a signed `.appx`.

### Source Directory Layout

```
src/
  App/              # Main app: entry point, launcher mgmt, splash window, video player
                    #   App.cpp        — WinMain, mode selection, Smart Docked Mode
                    #   Launchers.cpp  — start/detect/focus launcher processes
                    #   MainWindow.cpp — splash screen window + animation
                    #   GamingExperience.cpp — FSE API wrapper (Enter/Exit/IsFullscreen)
                    #   JumpList.cpp   — Windows Jump List integration (WinRT)
                    #   VideoPlayer.cpp — splash video playback
  AppSettings/      # Settings DLL: dialog, pages, layout, auto-detection
    SettingsPages/  #   LauncherPage   — launcher selection + Smart Docked Mode UI
                    #   SplashPage     — splash screen customization
                    #   StartupPage    — startup applications
                    #   UpdatePage     — version update checker
                    #   TroubleshootPage, SupportPage
  AppInstaller/     # Standalone installer UI + admin elevation
  Configuration/    # Config.cpp/.hpp — JSON config (nlohmann/json), static members
                    #   Config.Launchers.cpp — launcher auto-discovery
  FluentDesign/     # Custom Win32 GDI+ UI framework
                    #   Theme (dark/light, DPI, accent colors, 80+ color slots)
                    #   Controls: Button, Toggle, ComboBox, TextBox, ScrollView, etc.
                    #   SettingsLine — paired name+description with control widget
                    #   Align — anchor-based layout positioning
  Logging/          # Logger + LogManager: file output, 6 levels (Disabled→Trace)
  Tools/            # Win32 utilities: Process, Window, Registry, Icon, Paths, Event,
                    #   Unicode, Notification, Packages, GdiPlus, Minidump
  Updater/          # Async update checker (Codeberg releases)
```

## Key Design Patterns

**Event system:** `src/Tools/Event.hpp` — multicast delegate using `std::vector<std::function<void()>>`. Subscribe with `+=`, fire with `Notify()`. Use `delegate(method)` macro which captures `this` as `This`.

**Configuration:** `src/Configuration/Config.hpp` — all config is static members on the `Config` class. Loaded from `AnyFSE.json` via nlohmann/json with `json::json_pointer` paths (e.g., `/Extra/SmartDockedMode`). `Config::Load()` / `Config::Save()`.

**Settings pages:** Each page inherits `SettingsPage` and implements `AddPage()`, `LoadControls()`, `SaveControls()`. Use `SettingsDialog::AddSettingsLine()` to create UI rows with Toggle/ComboBox/TextBox controls. Group items with `AddGroupItem()` for collapsible sections.

**FluentDesign:** Custom Win32 GDI+ UI framework in `src/FluentDesign/`. `Theme` manages colors, fonts, DPI. Controls use `FluentControl` base class. All UI must use these controls for consistency.

**Settings DLL:** `AnyFSE.Settings.dll` exports a single `Main()` function loaded via `App::CallLibrary()`. This keeps the settings UI out of the main executable.

**Launcher detection:** `Config::GetLauncherDefaults()` maps exe paths to known launcher types (Playnite, Steam, BigBox, Xbox, ArmoryCrate, RetroBat, OneGameLauncher). `Config::FindLaunchers()` auto-discovers installed launchers via registry.

**Static CRT:** Both Debug (`/MTd`) and Release (`/MT`) use static CRT linking to avoid Defender false positives.

## Custom Features (Fork)

### Smart Docked Mode

Exits FSE automatically when the device appears to be docked. Detection heuristics:
1. Multiple monitors attached (external display via dock/hub)
2. Primary screen physically wider than configurable threshold (EDID `HORZSIZE` in mm)

**Config:** `/Extra/SmartDockedMode` (bool), `/Extra/SmartDockedThresholdMm` (int, default 250)
**UI:** Toggle + ComboBox on LauncherPage with presets for common handhelds (7"–13")
**Code:** Check in `App::WinMain()` before launcher start; calls `GamingExperience::ExitFSEMode()` and returns.

## Debugging Notes

- VS Code launch configs: "Debug AnyFSE Launcher" (no args) and "Debug AnyFSE Settings" (`/settings` arg).
- The app creates a window with class `AnyFSE` and checks `FindWindow()` to prevent duplicate instances.
- Log level configured in `AnyFSE.json` at `/Log/Level` (0=Disabled, 6=Trace). Logs go to `%APPDATA%\AnyFSE\logs\`.
- APPX signing requires the certificate in `Cert:\CurrentUser\My` (thumbprint auto-discovered at package time).
