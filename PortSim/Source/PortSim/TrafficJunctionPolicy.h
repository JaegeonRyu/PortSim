#pragma once
#include "CoreMinimal.h"

namespace TrafficJunctionPolicy
{
    // A straight traffic gate needs only its current segment. Claim additional
    // segments only when a turn is too close to stop clear, then keep the whole
    // coupled turn and a 20 m exit tangent atomic. A fixed 60 m lookahead on
    // every straight segment made same-direction queues mutually exclusive.
    inline void AppendExitCorridor(TArray<FBox>& Segments,const TArray<FVector>& Route,
        int32 Waypoint,int32 RouteEnd,TFunctionRef<FVector(int32)> ExtentFor,
        FVector InitialDirection=FVector::ZeroVector)
    {
        if(!Route.IsValidIndex(Waypoint)) return;
        RouteEnd=FMath::Clamp(RouteEnd,Waypoint+1,Route.Num());
        constexpr double Clearance=2000.;
        auto TurnWithin=[&](int32 At)
        {
            double Distance=0;
            for(int32 I=At;I+1<RouteEnd;++I)
            {
                const FVector Before=I>0?(Route[I]-Route[I-1]).GetSafeNormal2D():InitialDirection;
                const FVector After=(Route[I+1]-Route[I]).GetSafeNormal2D();
                if(!Before.IsNearlyZero() && FVector::DotProduct(Before,After)<.995f) return true;
                Distance+=FVector::Dist2D(Route[I],Route[I+1]);
                if(Distance>=Clearance) return false;
            }
            return false;
        };
        if(!TurnWithin(Waypoint)) return;
        FVector From=Route[Waypoint];
        double SinceTurn=0;
        for(int32 Next=Waypoint+1;Next<RouteEnd;++Next)
        {
            const FVector Extent=ExtentFor(Next);
            FBox Exit(From-Extent,From+Extent);
            Exit+=Route[Next]-Extent; Exit+=Route[Next]+Extent;
            Segments.Add(Exit);
            const FVector Direction=(Route[Next]-From).GetSafeNormal2D();
            const double StepLength=FVector::Dist2D(From,Route[Next]);
            const FVector Previous=(From-(Next>1?Route[Next-2]:From)).GetSafeNormal2D();
            if(!Previous.IsNearlyZero() && FVector::DotProduct(Previous,Direction)<.995f) SinceTurn=0;
            SinceTurn+=StepLength;
            From=Route[Next];
            if(SinceTurn>=Clearance && !TurnWithin(Next)) break;
        }
    }

    // Return only a closed wait cycle, not the upstream queue leading to it.
    inline TArray<int32> WaitCycle(const TMap<int32,int32>& Blockers,int32 Start)
    {
        TArray<int32> Chain;
        int32 Current=Start;
        while(const int32* Next=Blockers.Find(Current))
        {
            const int32 Seen=Chain.Find(Current);
            if(Seen!=INDEX_NONE)
            {
                Chain.RemoveAt(0,Seen);
                return Chain;
            }
            Chain.Add(Current);
            Current=*Next;
        }
        return {};
    }

    inline bool OlderRequestFirst(const double* A,const double* B,int32 IDA,int32 IDB)
    {
        if(bool(A)!=bool(B)) return A!=nullptr;
        if(A && B && *A!=*B) return *A<*B;
        return IDA<IDB;
    }

    // A bounded head start in seconds, not an absolute right of way. Aging
    // always lets a sufficiently older return beat a fresh pickup request.
    inline double MissionBonus(bool Pickup,double SuspendedWaitSeconds,bool ClearingSTS)
    {
        if(ClearingSTS) return 40.;
        return Pickup?20.+FMath::Clamp(SuspendedWaitSeconds*.1,0.,20.):0.;
    }
    inline double PriorityTime(const double* Since,double Now,double Bonus)
    { return (Since?*Since:Now)-FMath::Clamp(Bonus,0.,40.); }
}
