#pragma once

#include "CoreMinimal.h"
#include "TerminalLayout.h"

namespace AGVDispatchPolicy
{
    constexpr double CruiseSpeedCmPerSecond=600.;
    constexpr double FairnessSecondsPerExtraJob=12.;

    // Oldest physically accessible request wins among executable work. A
    // blocked request never prevents a different ready yard/RMG from working.
    inline bool RequestBefore(double EligibleA,int32 OrderA,double EligibleB,int32 OrderB)
    {
        return EligibleA!=EligibleB?EligibleA<EligibleB:OrderA<OrderB;
    }

    inline bool UseDirectReturn(double ReturnSeconds,double IdlePickupSeconds)
    {
        return ReturnSeconds<=IdlePickupSeconds;
    }

    struct FRequest
    {
        int32 Cargo=INDEX_NONE, Order=INDEX_NONE;
        double EligibleAt=0;
        bool SourceReady=false, YardReady=false;
    };
    inline int32 SelectExecutable(const TArray<FRequest>& Requests)
    {
        const FRequest* Best=nullptr;
        for(const auto& R:Requests)
            if(R.SourceReady && R.YardReady && (!Best ||
                RequestBefore(R.EligibleAt,R.Order,Best->EligibleAt,Best->Order))) Best=&R;
        return Best?Best->Cargo:INDEX_NONE;
    }

    inline double EstimatedApproachDistanceCm(FVector From,FVector Target,int32 STS)
    {
        const double ParkAisleX=TerminalLayout::SiteCmX(TerminalLayout::AGVParkAisleX(0));
        const double BerthApproachX=TerminalLayout::SiteCmX(TerminalLayout::AGVBerthApproachX);
        const double CrossY=TerminalLayout::AGVInboundCrossY(STS);
        return FMath::Abs(From.X-ParkAisleX)+FMath::Abs(From.Y-CrossY)+
            FMath::Abs(ParkAisleX-BerthApproachX)+FMath::Abs(Target.Y-CrossY)+
            FMath::Abs(Target.X-BerthApproachX);
    }

    inline double Score(FVector From,FVector Target,int32 STS,int32 CompletedJobs,int32 MinimumCompletedJobs)
    {
        const double PickupSeconds=EstimatedApproachDistanceCm(From,Target,STS)/CruiseSpeedCmPerSecond;
        const int32 ExtraJobs=FMath::Clamp(CompletedJobs-MinimumCompletedJobs,0,4);
        return PickupSeconds+ExtraJobs*FairnessSecondsPerExtraJob;
    }
}
