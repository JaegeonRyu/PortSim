#pragma once
#include "STSDynamics.h"

namespace HoistDriveLimits
{
    // Reserve the torque needed for configured acceleration/deceleration at
    // the allowed speed. P/(m*g) alone can consume all electrical power in
    // steady lowering and leave zero torque headroom to brake before landing.
    inline double SpeedMps(const FSTSDynamicsParameters& C,double MassKg,double AccelerationMps2,double PowerW)
    {
        if(MassKg<=0 || AccelerationMps2<=0 || PowerW<=0 || C.Parts<=0 || C.GearRatio<=0 ||
            C.DrumRadius<=0 || C.Efficiency<=0 || C.MotorCount<=0) return 0;
        const double Shaft=C.Parts*C.GearRatio/C.DrumRadius;
        const double Holding=MassKg*9.80665/(Shaft*C.Efficiency*C.MotorCount);
        const double PerAcceleration=MassKg/(Shaft*C.Efficiency*C.MotorCount)+C.MotorInertia*Shaft;
        const double Reserve=FMath::Min(AccelerationMps2,FMath::Max(0.,(C.MotorMaxTorque-Holding)/PerAcceleration));
        if(Reserve<=0) return 0;
        const double Torque=Holding+Reserve*PerAcceleration;
        return FMath::Min(C.MotorMaxRPM*2*PI/(60*Shaft),PowerW/(C.MotorCount*Torque*Shaft));
    }
}
