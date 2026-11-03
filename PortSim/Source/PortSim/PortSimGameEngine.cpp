#include "PortSimGameEngine.h"
#include "PortSimTimeStep.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<float> CVarPresentationHz(TEXT("PortSim.PresentationHz"),60.f,
    TEXT("Standalone viewport refresh rate in wall-clock Hz (1..120). Physics remains fixed at 60 Hz simulation time."));

void UPortSimGameEngine::RedrawViewports(bool bShouldPresent)
{
    if(const auto* Clock=Cast<UPortSimTimeStep>(GetCustomTimeStep()))
    {
        const double Now=FPlatformTime::Seconds();
        ++StepsSincePresentation;
        const float Requested=CVarPresentationHz.GetValueOnGameThread();
        const double Hz=FMath::IsFinite(Requested)?FMath::Clamp(double(Requested),1.,120.):60.;
        // A slow draw must not cause another draw on every fixed physics tick.
        // In live Chaos mode, advance the requested number of world steps before
        // presenting again. Buffered replay already batches those model steps.
        const int32 RequiredSteps=Clock->IsBufferedPlayback()?1:
            FMath::Max(1,FMath::CeilToInt(Clock->GetEffectiveRate()/(Hz*UPortSimTimeStep::StepSeconds)));
        if(Now<NextPresentationWall || StepsSincePresentation<RequiredSteps) return;
        StepsSincePresentation=0;
        NextPresentationWall=FMath::Max(NextPresentationWall+1./Hz,Now);
    }
    else { NextPresentationWall=0; StepsSincePresentation=0; }
    // Draw all scene elements from the same completed step. In particular,
    // never throttle the rope separately from the spreader or attached load.
    Super::RedrawViewports(bShouldPresent);
}
