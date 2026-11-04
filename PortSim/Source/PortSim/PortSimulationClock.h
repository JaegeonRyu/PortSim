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

    inline float TemperatureCelsius(double Elapsed)
    {
        // Assumed daily air temperature: 18 C at 09:00, 24 C at 15:00, 12 C at 03:00.
        return static_cast<float>(18.0+6.0*FMath::Sin(2.0*UE_DOUBLE_PI*(TimeOfDaySeconds(Elapsed)-StartSeconds)/DaySeconds));
    }
}
