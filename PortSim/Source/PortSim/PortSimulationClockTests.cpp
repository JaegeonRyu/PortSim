#if WITH_DEV_AUTOMATION_TESTS
#include "PortSimulationClock.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPortSimulationClockTest,"PortSim.Environment.SimulationClock",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FPortSimulationClockTest::RunTest(const FString& Parameters)
{
    using namespace PortSimulationClock;
    TestEqual(TEXT("Starts at 09:00"),TimeOfDaySeconds(0),9.0*3600.0);
    TestEqual(TEXT("Wraps at midnight"),TimeOfDaySeconds(15.0*3600.0),0.0);
    TestTrue(TEXT("Mild 09:00 temperature"),FMath::IsNearlyEqual(TemperatureCelsius(0,false,0,false,0),18.f));
    TestTrue(TEXT("Mild afternoon peak"),FMath::IsNearlyEqual(TemperatureCelsius(6*3600,false,0,false,0),24.f));
    TestTrue(TEXT("Snow begins near freezing"),TemperatureCelsius(0,true,0,false,0)<=0.f);
    TestTrue(TEXT("Snowfall intensifies cooling"),TemperatureCelsius(0,true,50,false,0)<TemperatureCelsius(0,true,0,false,0));
    TestTrue(TEXT("Heavy rain cools the day"),TemperatureCelsius(0,false,0,true,50)<TemperatureCelsius(0,false,0,false,0));
    TestTrue(TEXT("Weather reduces afternoon range"),TemperatureCelsius(6*3600,false,0,true,50)-TemperatureCelsius(0,false,0,true,50)<6.f);
    return true;
}
#endif
