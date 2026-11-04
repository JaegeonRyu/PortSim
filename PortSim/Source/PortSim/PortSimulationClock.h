#pragma once

#include "CoreMinimal.h"

namespace PortSimulationClock
{
    constexpr double StartSeconds=9.0*3600.0;
    constexpr double DaySeconds=24.0*3600.0;

    inline double TimeOfDaySeconds(double Elapsed)
    {
        const double Wrapped=FMath::Fmod(StartSeconds+FMath::Max(0.0,Elapsed),DaySeconds);
        return Wrapped<0?Wrapped+DaySeconds:Wrapped;
    }

    inline float TemperatureCelsius(double Elapsed,bool SnowEnabled,float SnowfallMmPerHour,
        bool RainEnabled,float RainfallMmPerHour)
    {
        // Scenario assumptions, not a forecast: mild dry day 18 C at 09:00,
        // snow near/below freezing, and heavy rain cooler with a smaller daily range.
        const double SnowCooling=SnowEnabled?18.0+6.0*FMath::Clamp(double(SnowfallMmPerHour)/50.0,0.0,1.0):0.0;
        const double RainCooling=RainEnabled?2.0+4.0*FMath::Clamp(double(RainfallMmPerHour)/50.0,0.0,1.0):0.0;
        const double DailyAmplitude=SnowEnabled?3.0:RainEnabled?4.0:6.0;
        const double Phase=2.0*UE_DOUBLE_PI*(TimeOfDaySeconds(Elapsed)-StartSeconds)/DaySeconds;
        return static_cast<float>(18.0-FMath::Max(SnowCooling,RainCooling)+DailyAmplitude*FMath::Sin(Phase));
    }
}
