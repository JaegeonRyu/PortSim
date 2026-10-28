#if WITH_DEV_AUTOMATION_TESTS
#include "PortEnvironmentComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherOperatingHoldTest,"PortSim.Environment.OperatingHold",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWeatherOperatingHoldTest::RunTest(const FString& Parameters)
{
    auto* E=NewObject<UPortEnvironmentComponent>();
    E->SetWindEnabled(false); E->SetSeaMotionEnabled(false);
    FPortWeatherHold H;
    H.Update(*E,1); TestFalse(TEXT("Normal conditions allow operation"),H.Active);
    E->SetRainEnabled(true); E->SetRainfallMmPerHour(60);
    H.Update(*E,1); TestTrue(TEXT("Heavy rain pauses work"),H.Active);
    E->SetRainfallMmPerHour(45); H.Update(*E,100);
    TestTrue(TEXT("Stop/resume hysteresis prevents premature resume"),H.Active && H.RecoveryElapsed==0);
    E->SetRainEnabled(false); H.Update(*E,9);
    TestTrue(TEXT("Recovery requires stable weather"),H.Active);
    E->SetFogEnabled(true); E->SetFogVisibilityMeters(20); H.Update(*E,1);
    TestTrue(TEXT("Another hazard resets recovery"),H.Active && H.RecoveryElapsed==0);
    E->SetFogEnabled(false); H.Update(*E,9); H.Update(*E,1);
    TestFalse(TEXT("Ten safe seconds automatically release hold"),H.Active);
    E->SetSurfaceConditions(0,0,1); H.Update(*E,1);
    E->SetIceEnabled(false); H.Update(*E,100);
    TestTrue(TEXT("Residual ice blocks resume after icing switch is off"),H.Active);
    E->AdvanceEnvironment(3600); H.Update(*E,10);
    TestFalse(TEXT("Surface weather continues and melting allows resume"),H.Active);
    E->SetSnowEnabled(true); E->SetSnowfallMmPerHour(90); H.Update(*E,1);
    TestTrue(TEXT("Heavy snow pauses work"),H.Active);
    E->SetSnowEnabled(false); H.Update(*E,10);
    TestFalse(TEXT("Clear weather releases hold"),H.Active);
    E->SetSeaMotionEnabled(true); E->SetSeaIntensity(1); H.Update(*E,1);
    TestTrue(TEXT("High combined wave and swell pauses work"),H.Active);
    E->SetSeaIntensity(0); H.Update(*E,10);
    TestFalse(TEXT("Calmer sea releases hold"),H.Active);
    E->SetWindEnabled(true); E->SetWindSpeedMetersPerSecond(7);
    E->OperatingLimits.StopWindMps=6; H.Update(*E,1);
    TestTrue(TEXT("Effective wind observes configurable limit"),H.Active);
    return true;
}
#endif
