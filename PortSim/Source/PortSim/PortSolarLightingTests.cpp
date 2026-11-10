#if WITH_DEV_AUTOMATION_TESTS
#include "PortSolarLighting.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortSolarLightingTest,"PortSim.Environment.SolarLighting",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FPortSolarLightingTest::RunTest(const FString& Parameters)
{
    // Independent NREL SPA geometric elevations and clockwise-from-north azimuths
    // at 35.02 N, 128.79 E. All inputs to the simulation use KST (UTC+09:00).
    struct FReferenceSample { FDateTime Time; double Elevation; double Azimuth; };
    const FReferenceSample Reference[] = {
        {FDateTime(2026,11,10,7),0.310679,111.224140},
        {FDateTime(2026,11,10,12),37.820599,177.363122},
        {FDateTime(2026,11,10,17,30),-2.173864,250.465213},
        {FDateTime(2026,11,11),-72.130087,353.165181},
        {FDateTime(2026,6,21,12),77.056060,151.711332},
        {FDateTime(2026,12,21,12),31.294914,173.899855},
    };
    for (const auto& Sample : Reference)
    {
        const auto Sun=PortSolarLighting::Calculate(Sample.Time);
        TestTrue(FString::Printf(TEXT("SPA elevation at %s"),*Sample.Time.ToString()),
            FMath::IsNearlyEqual(Sun.ElevationDegrees,Sample.Elevation,0.05));
        TestTrue(FString::Printf(TEXT("SPA azimuth at %s"),*Sample.Time.ToString()),
            FMath::IsNearlyEqual(Sun.AzimuthDegrees,Sample.Azimuth,0.05));
    }

    const auto Morning=PortSolarLighting::Calculate(Reference[0].Time);
    const auto Noon=PortSolarLighting::Calculate(Reference[1].Time);
    const auto Twilight=PortSolarLighting::Calculate(Reference[2].Time);
    const auto Night=PortSolarLighting::Calculate(Reference[3].Time);
    TestTrue(TEXT("Morning is east and above the map horizon"),Morning.DirectionToSun().Y>0 && Morning.DirectionToSun().Z>0);
    TestTrue(TEXT("Evening is west and below the map horizon"),Twilight.DirectionToSun().Y<0 && Twilight.DirectionToSun().Z<0);
    TestTrue(TEXT("Orbit agrees with SPA Earth-Sun distance"),FMath::IsNearlyEqual(Noon.EarthSunDistanceAu,0.990450,0.0003));
    TestTrue(TEXT("Twilight retains sunlight for atmospheric scattering"),Twilight.RelativeSolarIlluminance()>0.0);
    TestEqual(TEXT("No solar illuminance during astronomical night"),Night.RelativeSolarIlluminance(),0.0);
    TestTrue(TEXT("Fog gets much less daylight in twilight"),
        Twilight.EstimateHorizontalIlluminanceLux()>0.f &&
        Twilight.EstimateHorizontalIlluminanceLux()<0.01f*Noon.EstimateHorizontalIlluminanceLux());
    TestEqual(TEXT("Fog gets no sunlight at midnight"),Night.EstimateHorizontalIlluminanceLux(),0.f);

    auto* Atmosphere=NewObject<USkyAtmosphereComponent>();
    const float ReferenceLux=Atmosphere->GetAtmosphericLightToMatchIlluminanceOnGround(FVector::UpVector,120000.f);
    auto DirectLux=[&](const FPortSolarPosition& Sun)
    {
        const float IncidentLux=ReferenceLux*Sun.RelativeSolarIlluminance();
        return IncidentLux/Atmosphere->GetAtmosphericLightToMatchIlluminanceOnGround(Sun.DirectionToSun(),1.f);
    };
    TestTrue(TEXT("Atmospheric extinction strongly dims the horizon"),DirectLux(Morning)<0.2f*DirectLux(Noon));
    TestTrue(TEXT("Midday direct illuminance is in the tens of kilolux"),DirectLux(Noon)>60000.f && DirectLux(Noon)<120000.f);
    TestTrue(TEXT("Earth shadow blocks direct light during twilight"),DirectLux(Twilight)<1.f);
    return true;
}
#endif
