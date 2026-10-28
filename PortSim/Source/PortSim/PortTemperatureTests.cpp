#if WITH_DEV_AUTOMATION_TESTS
#include "PortEnvironmentComponent.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortTemperatureTest,"PortSim.Environment.Temperature",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPortTemperatureTest::RunTest(const FString& Parameters)
{
    auto* E=NewObject<UPortEnvironmentComponent>();
    TestEqual(TEXT("Default fixed temperature"),E->GetTemperatureCelsius(),18.f);
    E->SetTemperatureCelsius(-99); TestEqual(TEXT("Lower bound"),E->GetTemperatureCelsius(),-25.f);
    E->SetTemperatureCelsius(99); TestEqual(TEXT("Upper bound"),E->GetTemperatureCelsius(),40.f);
    E->SetTemperatureCelsius(std::numeric_limits<float>::quiet_NaN());
    TestEqual(TEXT("Invalid input preserves setting"),E->GetTemperatureCelsius(),40.f);
    E->SetTemperatureCelsius(18); E->AdvanceEnvironment(7200);
    TestEqual(TEXT("Fixed temperature remains constant"),E->GetTemperatureCelsius(),18.f);
    E->SetDailyTemperatureCycleEnabled(true);
    E->AdvanceEnvironment(21600);
    TestTrue(TEXT("Daily maximum at 14h"),FMath::IsNearlyEqual(E->GetTemperatureCelsius(),22.f,.001f));
    E->AdvanceEnvironment(43200);
    TestTrue(TEXT("Daily minimum at 02h"),FMath::IsNearlyEqual(E->GetTemperatureCelsius(),14.f,.001f));
    const float Before=E->GetTemperatureCelsius(); E->AdvanceEnvironment(0);
    TestEqual(TEXT("Paused environment does not advance temperature"),E->GetTemperatureCelsius(),Before);
    E->SetDailyTemperatureCycleEnabled(false);
    TestEqual(TEXT("Disabling cycle restores fixed setting"),E->GetTemperatureCelsius(),18.f);
    auto* Other=NewObject<UPortEnvironmentComponent>();
    E->SetDailyTemperatureCycleEnabled(true); Other->SetDailyTemperatureCycleEnabled(true);
    // E has advanced 18 hours; compare another 6 hours with a fresh 24-hour cycle.
    E->AdvanceEnvironment(21600);
    for(int I=0;I<864;++I) Other->AdvanceEnvironment(100);
    TestTrue(TEXT("Equal simulated time is independent of playback steps"),FMath::IsNearlyEqual(E->GetTemperatureCelsius(),Other->GetTemperatureCelsius(),.001f));
    E->SetTemperatureCelsius(40); E->AdvanceEnvironment(21600);
    TestEqual(TEXT("Daily peak also respects upper bound"),E->GetTemperatureCelsius(),40.f);
    E->SetTemperatureCelsius(-25); E->AdvanceEnvironment(43200);
    TestEqual(TEXT("Daily minimum respects lower bound"),E->GetTemperatureCelsius(),-25.f);
    return true;
}
#endif
