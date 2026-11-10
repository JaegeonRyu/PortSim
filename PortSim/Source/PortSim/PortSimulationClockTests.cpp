#if WITH_DEV_AUTOMATION_TESTS
#include "PortSimulationClock.h"
#include "PortEnvironmentComponent.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortSimulationClockTest,"PortSim.Environment.SimulationClock",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FPortSimulationClockTest::RunTest(const FString& Parameters)
{
    using namespace PortSimulationClock;
    auto* Environment=NewObject<UPortEnvironmentComponent>();
    const auto Start=Environment->GetLocalDateTime();
    TestEqual(TEXT("Starts at the scenario date and time"),Start,DefaultStartLocalTime());
    TestEqual(TEXT("Default time is 09:00 KST"),Start.GetTimeOfDay().GetTotalSeconds(),StartSeconds);
    Environment->AdvanceEnvironment(15.f*3600.f);
    TestEqual(TEXT("Midnight advances the calendar"),Environment->GetLocalDateTime(),Start.GetDate()+FTimespan::FromDays(1));
    Environment->ResetEnvironmentClock();
    TestEqual(TEXT("Reset restores the scenario date and time"),Environment->GetLocalDateTime(),Start);

    constexpr double Afternoon=15.0*3600.0;
    TestTrue(TEXT("Mild 09:00 temperature"),FMath::IsNearlyEqual(TemperatureCelsius(StartSeconds,false,0,false,0),18.f));
    TestTrue(TEXT("Mild afternoon peak"),FMath::IsNearlyEqual(TemperatureCelsius(Afternoon,false,0,false,0),24.f));
    TestTrue(TEXT("Snow begins near freezing"),TemperatureCelsius(StartSeconds,true,0,false,0)<=0.f);
    TestTrue(TEXT("Snowfall intensifies cooling"),TemperatureCelsius(StartSeconds,true,50,false,0)<TemperatureCelsius(StartSeconds,true,0,false,0));
    TestTrue(TEXT("Heavy rain cools the day"),TemperatureCelsius(StartSeconds,false,0,true,50)<TemperatureCelsius(StartSeconds,false,0,false,0));
    TestTrue(TEXT("Weather reduces afternoon range"),TemperatureCelsius(Afternoon,false,0,true,50)-TemperatureCelsius(StartSeconds,false,0,true,50)<6.f);
    return true;
}
#endif
