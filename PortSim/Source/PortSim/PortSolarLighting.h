#pragma once

#include "CoreMinimal.h"

struct FPortSolarPosition
{
    double ElevationDegrees = 0.0;
    double AzimuthDegrees = 0.0;
    double EarthSunDistanceAu = 1.0;

    FVector DirectionToSun() const;
    double RelativeSolarIlluminance() const;
    float EstimateHorizontalIlluminanceLux() const;
};

namespace PortSolarLighting
{
    // Representative Busan New Port coordinates. Map north follows the wind compass (+X).
    constexpr double LatitudeDegrees = 35.02;
    constexpr double LongitudeDegrees = 128.79;

    FPortSolarPosition Calculate(const FDateTime& LocalDateTime);
}
