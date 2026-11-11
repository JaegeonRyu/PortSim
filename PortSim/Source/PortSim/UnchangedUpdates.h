#pragma once
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"

// Exact-value reuse only: physics steps, sensor samples and timers still run.
namespace PortSimUpdates
{
    inline bool SkipUnchanged()
    {
        static TAutoConsoleVariable<int32> Enabled(TEXT("PortSim.SkipUnchangedUpdates"),1,
            TEXT("Reuse identical presentation inputs; set 0 for performance A/B checks."));
        return Enabled.GetValueOnGameThread()!=0;
    }
}
