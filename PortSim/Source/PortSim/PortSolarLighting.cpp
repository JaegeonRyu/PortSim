#include "PortSolarLighting.h"

FVector FPortSolarPosition::DirectionToSun() const
{
    // UE world +X is north and +Y is east. Azimuth is clockwise from north.
    const double Elevation = FMath::DegreesToRadians(ElevationDegrees);
    const double Azimuth = FMath::DegreesToRadians(AzimuthDegrees);
    return FVector(FMath::Cos(Elevation) * FMath::Cos(Azimuth),
        FMath::Cos(Elevation) * FMath::Sin(Azimuth), FMath::Sin(Elevation));
}

double FPortSolarPosition::RelativeSolarIlluminance() const
{
    // Inverse-square irradiance at the top of the atmosphere. The renderer handles
    // atmospheric extinction, Earth shadow and the surface incidence angle.
    // Fade out the atmospheric source beyond nautical twilight to avoid updating
    // the light in full darkness, where the model omits moonlight and airglow.
    const double Twilight = FMath::SmoothStep(-18.0, -12.0, ElevationDegrees);
    return Twilight / FMath::Square(EarthSunDistanceAu);
}

float FPortSolarPosition::EstimateHorizontalIlluminanceLux() const
{
    // Representative clear-sky horizontal illuminance from IALA G1038, Table 1.
    // Interpolate in log space around twilight, where light falls by orders of magnitude.
    struct FIlluminanceSample { double Elevation; double Lux; };
    constexpr FIlluminanceSample Samples[] = {
        {-18.0,0.000651}, {-12.0,0.00831}, {-6.0,3.4}, {0.0,732.0}, {5.0,4760.0},
        {15.0,18600.0}, {30.0,47000.0}, {50.0,85000.0}, {70.0,113000.0}, {90.0,124000.0},
    };
    if (ElevationDegrees<=Samples[0].Elevation) return 0.f;
    for (int32 Index=1; Index<UE_ARRAY_COUNT(Samples); ++Index)
    {
        const auto& Upper=Samples[Index];
        if (ElevationDegrees>Upper.Elevation) continue;
        const auto& Lower=Samples[Index-1];
        const double Alpha=(ElevationDegrees-Lower.Elevation)/(Upper.Elevation-Lower.Elevation);
        return FMath::Exp(FMath::Lerp(FMath::Loge(Lower.Lux),FMath::Loge(Upper.Lux),Alpha))
            * RelativeSolarIlluminance();
    }
    return Samples[UE_ARRAY_COUNT(Samples)-1].Lux * RelativeSolarIlluminance();
}

FPortSolarPosition PortSolarLighting::Calculate(const FDateTime& LocalDateTime)
{
    // Meeus solar coordinates and equation of time, as used by NOAA's Solar Calculator.
    // Geometric elevation feeds SkyAtmosphere, whose scattering uses a geometric horizon.
    const FDateTime Utc = LocalDateTime - FTimespan::FromHours(9);
    const double Century = (Utc.GetJulianDay() - 2451545.0) / 36525.0;
    const double MeanLongitude = FMath::DegreesToRadians(FMath::Fmod(
        280.46646 + Century * (36000.76983 + 0.0003032 * Century), 360.0));
    const double MeanAnomaly = FMath::DegreesToRadians(FMath::Fmod(
        357.52911 + Century * (35999.05029 - 0.0001537 * Century), 360.0));
    const double Eccentricity = 0.016708634 - Century * (0.000042037 + 0.0000001267 * Century);
    const double EquationOfCenter = FMath::DegreesToRadians(
        FMath::Sin(MeanAnomaly) * (1.914602 - Century * (0.004817 + 0.000014 * Century))
        + FMath::Sin(2.0 * MeanAnomaly) * (0.019993 - 0.000101 * Century)
        + FMath::Sin(3.0 * MeanAnomaly) * 0.000289);
    const double Omega = FMath::DegreesToRadians(125.04 - 1934.136 * Century);
    const double ApparentLongitude = MeanLongitude + EquationOfCenter
        - FMath::DegreesToRadians(0.00569 + 0.00478 * FMath::Sin(Omega));
    const double Obliquity = FMath::DegreesToRadians(23.0 + (26.0 + (21.448
        - Century * (46.815 + Century * (0.00059 - Century * 0.001813))) / 60.0) / 60.0
        + 0.00256 * FMath::Cos(Omega));
    const double Declination = FMath::Asin(FMath::Sin(Obliquity) * FMath::Sin(ApparentLongitude));

    const double Y = FMath::Square(FMath::Tan(Obliquity / 2.0));
    const double SinLongitude2 = FMath::Sin(2.0 * MeanLongitude);
    const double EquationOfTimeMinutes = 4.0 * FMath::RadiansToDegrees(Y * SinLongitude2
        - 2.0 * Eccentricity * FMath::Sin(MeanAnomaly)
        + 4.0 * Eccentricity * Y * FMath::Sin(MeanAnomaly) * FMath::Cos(2.0 * MeanLongitude)
        - 0.5 * Y * Y * FMath::Sin(4.0 * MeanLongitude)
        - 1.25 * Eccentricity * Eccentricity * FMath::Sin(2.0 * MeanAnomaly));
    const double SolarMinutes = Utc.GetTimeOfDay().GetTotalMinutes() + EquationOfTimeMinutes + 4.0 * LongitudeDegrees;
    const double HourAngle = FMath::DegreesToRadians(SolarMinutes / 4.0 - 180.0);
    const double Latitude = FMath::DegreesToRadians(LatitudeDegrees);
    const double North = FMath::Sin(Declination) * FMath::Cos(Latitude)
        - FMath::Cos(Declination) * FMath::Cos(HourAngle) * FMath::Sin(Latitude);
    const double East = -FMath::Cos(Declination) * FMath::Sin(HourAngle);
    const double Up = FMath::Sin(Declination) * FMath::Sin(Latitude)
        + FMath::Cos(Declination) * FMath::Cos(HourAngle) * FMath::Cos(Latitude);

    FPortSolarPosition Sun;
    Sun.ElevationDegrees = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up, -1.0, 1.0)));
    Sun.AzimuthDegrees = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(East, North)) + 360.0, 360.0);
    Sun.EarthSunDistanceAu = 1.000001018 * (1.0 - FMath::Square(Eccentricity))
        / (1.0 + Eccentricity * FMath::Cos(MeanAnomaly + EquationOfCenter));
    return Sun;
}
