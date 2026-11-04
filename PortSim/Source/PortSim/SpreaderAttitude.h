#pragma once
#include "CoreMinimal.h"

// Equivalent trim drive assumptions, not a manufacturer hydraulic/reeving model.
// Axes are Unreal pitch/roll, degrees, seconds and SI torque/inertia.
struct FSpreaderAttitudeConfig
{
    bool Enabled=false;
    double MaxAngle=2, MaxRate=2, MaxAcceleration=6, Response=4, MaxTorque=30000, CoGDepth=1.5;
};

struct FSpreaderAttitude
{
    FVector2D Angle=FVector2D::ZeroVector,Rate=FVector2D::ZeroVector,Torque=FVector2D::ZeroVector;
    bool Saturated=false;
    FQuat Rotation(double YawRadians) const
    { return FRotator(Angle.X,FMath::RadiansToDegrees(YawRadians),Angle.Y).Quaternion(); }
    void Step(const FSpreaderAttitudeConfig& C,double Dt,FVector2D Measured,FVector2D MeasuredRate,
        FVector2D Target,FVector2D TargetRate,double Mass,double HalfWidthM,double HalfLengthM)
    {
        if(!C.Enabled || Dt<=0 || Mass<=0) return;
        Saturated=false;
        for(int32 I=0;I<2;++I)
        {
            const double Radius=I==0?HalfWidthM:HalfLengthM;
            const double Inertia=Mass*(4*Radius*Radius+.6*.6)/12.;
            const double Goal=FMath::Clamp(Target[I],-C.MaxAngle,C.MaxAngle);
            const double GoalRate=FMath::Clamp(TargetRate[I],-C.MaxRate,C.MaxRate);
            const double Restoring=Mass*9.80665*C.CoGDepth;
            const double Request=Restoring*FMath::Sin(FMath::DegreesToRadians(Goal))+
                Inertia*FMath::DegreesToRadians(C.Response*C.Response*(Goal-Measured[I])+
                    2*C.Response*(GoalRate-MeasuredRate[I]));
            Torque[I]=FMath::Clamp(Request,-C.MaxTorque,C.MaxTorque);
            const double Acc=FMath::RadiansToDegrees((Torque[I]-Restoring*FMath::Sin(FMath::DegreesToRadians(Angle[I])))/Inertia);
            const double Limited=FMath::Clamp(Acc,-C.MaxAcceleration,C.MaxAcceleration);
            Saturated |= FMath::Abs(Request)>C.MaxTorque || FMath::Abs(Acc)>C.MaxAcceleration;
            Rate[I]=FMath::Clamp(Rate[I]+Limited*Dt,-C.MaxRate,C.MaxRate);
            Angle[I]+=Rate[I]*Dt;
            if(FMath::Abs(Angle[I])>C.MaxAngle)
            { Angle[I]=FMath::Clamp(Angle[I],-C.MaxAngle,C.MaxAngle);Rate[I]=0;Saturated=true; }
        }
    }
};
