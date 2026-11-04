#pragma once
#include "CoreMinimal.h"

namespace YardFlowEstimate
{
    // Planning estimates only: actual crane/vehicle controllers determine time.
    // The approach is exclusively leased from dispatch until the empty AGV exits.
    constexpr double PickupSeconds=144., PlacementSeconds=180.;
    struct FResult { double Start=0, Handover=0, Placed=0, Released=0, Cost=0; };
    inline FResult Evaluate(double STSReady,double ApproachReady,double SupportReady,
        double LoadedTravelSeconds,double EmptyExitSeconds,bool Ground,bool Completing20Cell)
    {
        FResult R;
        R.Start=FMath::Max3(STSReady,ApproachReady,SupportReady);
        R.Handover=R.Start+PickupSeconds;
        R.Placed=R.Handover+LoadedTravelSeconds+PlacementSeconds;
        R.Released=R.Placed+EmptyExitSeconds;
        // In this initially empty yard, unnecessary support dependencies can
        // serialize otherwise independent STSs. Charge one estimated pickup
        // cycle for relying on an unplaced lower booking. Real space/size,
        // outbound, hazard and weight constraints still decide feasibility.
        R.Cost=R.Placed+(Ground?0.:PickupSeconds)-(Completing20Cell?90.:0.);
        return R;
    }
}
