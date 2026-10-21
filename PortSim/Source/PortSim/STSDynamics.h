#pragma once
#include "CoreMinimal.h"
class FJsonObject;

namespace CraneSimulationSteps
{
    inline int32 Count(double Seconds,double Hertz)
    {
        // A float 0.05 s becomes 3.0000000447 cycles at 60 Hz. Do not turn
        // that representation error into a fourth controller iteration.
        return FMath::Max(1,FMath::CeilToInt(Seconds*Hertz-1.e-6));
    }
}

struct FSTSSensorMount
{
    FString Key,Frame,Unit;
    FVector Position=FVector::ZeroVector;
    FRotator Rotation=FRotator::ZeroRotator;
    double Minimum=0,Maximum=0,Fov=0;
    bool Scan=false;
    TArray<FVector> AdditionalPositions;
    double MeasurementOrigin=0,Resolution=0,MaxTraversingSpeed=0;
    TSharedPtr<FJsonObject> Reference;
    double ReadPosition(double PositionM) const
    { const double Raw=PositionM-MeasurementOrigin; return Resolution>0?FMath::RoundToDouble(Raw/Resolution)*Resolution:Raw; }
};

/** Explicit SI assumptions for a taut-rope, reduced-order suspension. */
struct FSTSDynamicsParameters
{
    bool AntiSway=true,AntiSkew=true;
    double Damping=0,PositionGain=0,VelocityGain=0,SwayGain=0,SkewKp=0,SkewKd=0,SkewMaxTorque=0;
    double DrumRadius=0,GearRatio=0,Efficiency=0,Parts=0,MotorCount=0,MotorMaxTorque=0,MotorMaxRPM=0,MotorInertia=0;
    double RopeEA=0,RopeLimit=0,SkewLimit=0;
    FVector Wind=FVector::ZeroVector;
    // Local X/Y/Z projected areas (YZ/XZ/XY faces). Zero retains the legacy
    // scalar WindArea fallback for standalone dynamics tests/config loading.
    FVector WindProjectedAreasM2=FVector::ZeroVector;
    double WindArea=0,WindDrag=0,WindMoment=0;
    FVector AerodynamicForce(FVector BodyVelocityMps) const;
    FVector WindEquilibriumOffset(double LengthM,double MassKg) const;
    double HorizontalAcceleration(double ErrorM,double VelocityMps,double LengthM,double AngularRate,
        double AngularDisplacement=0,double EquilibriumAngle=0,double PositionGainScale=1) const
    {
        const double SwayFeedback=SwayGain*LengthM*AngularRate+
            FMath::Square(SwayGain)*LengthM*(AngularDisplacement-EquilibriumAngle);
        return PositionGain*PositionGainScale*ErrorM-VelocityGain*VelocityMps+(AntiSway?SwayFeedback:0);
    }
};

// Force integration copies only numeric parameters. Sensor layout strings and
// reference JSON stay in the operating profile instead of being copied at 120 Hz.
struct FSTSDynamicsConfig : FSTSDynamicsParameters
{
    TArray<FSTSSensorMount> Mounts;
    bool Load(TSharedPtr<FJsonObject> Root,FString& Error);
};

/** Two sway coordinates + yaw. Roll/pitch constrained; no elastic/slack-rope FEM. */
struct FSTSSuspension
{
    FVector2D Angle=FVector2D::ZeroVector,Rate=FVector2D::ZeroVector;
    double Yaw=0,YawRate=0,ControlTorque=0,MotorTorque=0,MotorRPM=0,MotorPower=0;
    double Tension[4]={0,0,0,0},Extension[4]={0,0,0,0};
    FVector Offset=FVector::ZeroVector,OffsetVelocity=FVector::ZeroVector;
    bool Saturated=false;
    FString Fault;
    void Step(const FSTSDynamicsParameters& C,double Dt,double Length,double LengthRate,
        FVector Acceleration,FVector DriveVelocity,double Mass,FVector CoG,double PowerW);
    double SwayDegrees() const { return FMath::RadiansToDegrees(Angle.Size()); }
};
