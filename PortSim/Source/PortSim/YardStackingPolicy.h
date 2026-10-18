#pragma once
#include "CoreMinimal.h"
#include "CargoInformation.h"

namespace YardStackingPolicy
{
    // Dedicated simulation zones, NOT an IMDG segregation-table implementation.
    inline bool ZoneAccepts(int32 Block,const FPortCargoInformation& C)
    {
        if(C.HazardGroup==TEXT("flammable")) return Block==21;
        if(C.HazardGroup==TEXT("corrosive")) return Block==22;
        if(C.HandlingType==TEXT("reefer")) return Block>=18 && Block<=20;
        return Block>=0 && Block<18;
    }
    inline bool MayStack(const FPortCargoInformation& Lower,int32 LowerFt,float LowerKg,
        const FPortCargoInformation& Upper,int32 UpperFt,float UpperKg,float AlreadyAboveKg=0)
    {
        return Lower.Valid() && Upper.Valid() && LowerFt==UpperFt && Lower.HandlingType==Upper.HandlingType &&
            Lower.HazardGroup==Upper.HazardGroup && Upper.OutboundOrder<=Lower.OutboundOrder &&
            FMath::IsFinite(LowerKg) && FMath::IsFinite(UpperKg) && LowerKg>=UpperKg && UpperKg>0 &&
            AlreadyAboveKg>=0 && AlreadyAboveKg+UpperKg<=Lower.PermittedSuperimposedKg;
    }
    inline bool PairBookingsCompatible(const FPortCargoInformation& A,const FPortCargoInformation& B)
    {
        return A.Valid() && B.Valid() && A.HandlingType==B.HandlingType && A.HazardGroup==TEXT("none") &&
            B.HazardGroup==TEXT("none") && A.OutboundOrder==B.OutboundOrder &&
            (A.HandlingType!=TEXT("reefer") || FMath::IsNearlyEqual(A.TemperatureC,B.TemperatureC));
    }
    inline double PairedTripSavingSeconds(double OneWaySeconds,double ReturnSeconds,double SecondPickupWaitSeconds,
        double ExtraHandlingSeconds)
    {
        return OneWaySeconds+ReturnSeconds-SecondPickupWaitSeconds-ExtraHandlingSeconds;
    }
    constexpr double MinimumPairSavingSeconds=30.;
    inline bool DockOutsideThroughPaths(float DockY,const TArray<FVector2D>& OtherLoadedPaths,float ClearanceCm=1800.f)
    {
        for(const auto& Path:OtherLoadedPaths)
            if(DockY>=FMath::Min(Path.X,Path.Y)-ClearanceCm &&
                DockY<=FMath::Max(Path.X,Path.Y)+ClearanceCm) return false;
        return true;
    }
    constexpr float PairPadOffsetCm=306.7f; // 2*605.8 cm + 7.6 cm gap = 40 ft footprint.
    inline bool PairFits(int32 FtA,int32 FtB,float LengthA,float LengthB,float KgA,float KgB,float CapacityKg)
    {
        return FtA==20 && FtB==20 && FMath::IsFinite(KgA) && FMath::IsFinite(KgB) && KgA>0 && KgB>0 &&
            FMath::IsFinite(LengthA) && FMath::IsFinite(LengthB) && LengthA>0 && LengthB>0 &&
            KgA+KgB<=CapacityKg && (LengthA+LengthB)*.5f+7.59f<=2*PairPadOffsetCm &&
            2*PairPadOffsetCm+FMath::Max(LengthA,LengthB)<=1380.f;
    }
}
