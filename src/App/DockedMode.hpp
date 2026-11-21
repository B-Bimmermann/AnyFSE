#pragma once

#include <windows.h>

namespace AnyFSE::App::DockedMode
{
    // Smart Docked Mode (/Extra/SmartDockedMode): on the first Home app launch after Windows started in full screen
    // experience, leave it when an external display is connected. Returns true when full screen experience was left.
    bool ExitWhenDocked();

    // Writes every active display path and its classification to the log and returns true if one counts as external.
    bool IsExternalDisplayActive();
}
