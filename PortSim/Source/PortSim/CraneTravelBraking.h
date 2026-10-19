#pragma once
#include "CoreMinimal.h"

namespace CraneTravelBraking
{
    // Radial angular stopping distance for the measured two-axis pendulum.
    // Corrective drive acceleration opposes outward angular motion; sensor
    // ranges and the actual per-axis motor acceleration stay unchanged.
    inline FVector2D SwayAcceleration(FVector2D Angle,FVector2D Rate,FVector2D WindAcceleration,
        FVector2D Requested,FVector2D Available,double Length,double LengthRate,double Damping,
        double MaximumAngle,double StepSeconds)
    {
        const double Magnitude=Angle.Size();
        const FVector2D Direction=Magnitude>1.e-6?Angle/Magnitude:Rate.GetSafeNormal();
        const double Outward=FVector2D::DotProduct(Rate,Direction);
        if(Outward<=0 || Direction.IsNearlyZero() || Length<=0 || MaximumAngle<=0) return Requested;
        double DriveLimit=TNumericLimits<double>::Max();
        for(int32 I=0;I<2;++I) if(FMath::Abs(Direction[I])>1.e-6)
            DriveLimit=FMath::Min(DriveLimit,Available[I]/FMath::Abs(Direction[I]));
        double Restoring=0,DriveProjection=0;
        for(int32 I=0;I<2;++I)
        {
            Restoring+=9.80665*FMath::Sin(Angle[I])*Direction[I];
            DriveProjection+=DriveLimit*Direction[I]*Direction[I]*FMath::Cos(Angle[I]);
        }
        const double Tangential=FMath::Max(0.,Rate.SizeSquared()-Outward*Outward)/FMath::Max(1.e-6,Magnitude);
        const double Brake=FMath::Max(0.,(Restoring+DriveProjection-
            FVector2D::DotProduct(WindAcceleration,Direction))/Length+
            (Damping+2*LengthRate/Length)*Outward-Tangential);
        const double Remaining=FMath::Max(0.,.9*MaximumAngle-Magnitude-Outward*StepSeconds);
        if(Outward>FMath::Sqrt(2*Brake*Remaining)) return Direction*DriveLimit;
        return Requested;
    }

    // Leave a bounded stroke for anti-sway correction around the encoder
    // target, but brake before velocity can carry the drive outside it.
    inline double Acceleration(double ErrorCm,double RelativeVelocityCmS,double RequestedCmS2,
        double AvailableCmS2,double CorrectionRadiusCm,double StepSeconds)
    {
        const double Limit=FMath::Max(0.,AvailableCmS2);
        if(Limit<=0) return 0;
        const double NextStroke=FMath::Abs(RelativeVelocityCmS)*StepSeconds+.5*Limit*StepSeconds*StepSeconds;
        const double PositiveLimit=FMath::Sqrt(2.*Limit*FMath::Max(0.,ErrorCm+CorrectionRadiusCm-NextStroke));
        const double NegativeLimit=-FMath::Sqrt(2.*Limit*FMath::Max(0.,CorrectionRadiusCm-ErrorCm-NextStroke));
        if(RelativeVelocityCmS>PositiveLimit) return -Limit;
        if(RelativeVelocityCmS<NegativeLimit) return Limit;
        if(StepSeconds<=0) return 0;
        const double RequestedVelocity=RelativeVelocityCmS+FMath::Clamp(RequestedCmS2,-Limit,Limit)*StepSeconds;
        const double SafeVelocity=FMath::Clamp(RequestedVelocity,NegativeLimit,PositiveLimit);
        return FMath::Clamp((SafeVelocity-RelativeVelocityCmS)/StepSeconds,-Limit,Limit);
    }
}
