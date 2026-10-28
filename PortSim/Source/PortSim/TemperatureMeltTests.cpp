#if WITH_DEV_AUTOMATION_TESTS
#include "PortEnvironmentComponent.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTemperatureMeltTest,"PortSim.Environment.TemperatureMelt",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FTemperatureMeltTest::RunTest(const FString& Parameters)
{
    FPortSurfaceDynamics D; D.EvaporationMmPerHour=0; D.DrainHalfLifeSeconds=864000;
    FPortSurfaceState Initial; Initial.Snow=1; Initial.Ice=1;
    FPortSurfaceAccumulation Base; Base.Initialize(Initial,D);
    for(const double T:{-25.,-5.,0.})
    {
        auto A=Base; A.Advance(D,0,0,0,600,T);
        TestEqual(TEXT("No snow melt at/below freezing"),A.SnowMm,Base.SnowMm);
        TestEqual(TEXT("Disabling icing below freezing preserves ice"),A.Ice,Base.Ice);
        TestEqual(TEXT("No meltwater below freezing"),A.WaterMm,0.);
    }
    auto Cool=Base,Warm=Base;
    Cool.Advance(D,0,0,0,60,5); Warm.Advance(D,0,0,0,60,10);
    TestTrue(TEXT("Higher positive temperature increases snow and ice melt"),Warm.SnowMm<Cool.SnowMm && Warm.Ice<Cool.Ice);
    TestTrue(TEXT("Snow and ice contribute meltwater"),Cool.WaterMm>0 && Warm.WaterMm>Cool.WaterMm);
    TestTrue(TEXT("Reference temperature retains configured snow melt rate"),FMath::IsNearlyEqual(Base.SnowMm-Cool.SnowMm,D.SnowMeltMmPerHour/60.,1.e-6));
    TestTrue(TEXT("Reference temperature retains configured ice melt rate"),FMath::IsNearlyEqual(Base.Ice-Cool.Ice,60./D.IceMeltSeconds,1.e-6));
    auto Forced=Base; Forced.Advance(D,0,0,1,60,5);
    TestTrue(TEXT("Warm air melts ice even with icing selected"),Forced.Ice<Base.Ice);
    auto Big=Base,Small=Base; Big.Advance(D,0,5,0,3600,10);
    for(int I=0;I<3600;++I) Small.Advance(D,0,5,0,1,10);
    TestTrue(TEXT("Reservoir depletion and continuing snowfall preserve water balance across steps"),
        FMath::IsNearlyEqual(Big.WaterMm,Small.WaterMm,1.e-6) && FMath::IsNearlyEqual(Big.SnowMm,Small.SnowMm,1.e-6) && FMath::IsNearlyEqual(Big.Ice,Small.Ice,1.e-6));
    TestEqual(TEXT("Warm air can melt incoming snow without stored snow"),Big.SnowMm,0.);
    auto Invalid=Base; Invalid.Advance(D,0,0,0,60,std::numeric_limits<double>::quiet_NaN());
    TestEqual(TEXT("Invalid temperature does not alter reservoir"),Invalid.SnowMm,Base.SnowMm);
    auto* E=NewObject<UPortEnvironmentComponent>(); E->SetTemperatureCelsius(-5); E->SetSurfaceConditions(0,1,1);
    E->AdvanceEnvironment(60); TestEqual(TEXT("Component passes cold air into surface model"),E->GetSurfaceConditions().Ice,1.f);
    E->SetTemperatureCelsius(10); E->AdvanceEnvironment(60);
    TestTrue(TEXT("Warming resumes melt without resetting stored snow or ice"),E->GetSurfaceConditions().Ice<1 && E->GetSurfaceConditions().SnowDepthMm<10);
    auto* Cycle=NewObject<UPortEnvironmentComponent>(); Cycle->SetTemperatureCelsius(0); Cycle->SetDailyTemperatureCycleEnabled(true);
    Cycle->TemperatureStartHour=1; Cycle->SetSurfaceConditions(0,1,1); Cycle->AdvanceEnvironment(3600);
    TestEqual(TEXT("Daily cycle cold phase preserves snow"),Cycle->GetSurfaceConditions().SnowDepthMm,10.f);
    Cycle->AdvanceEnvironment(43200);
    TestTrue(TEXT("Daily cycle positive phase melts snow and ice"),Cycle->GetSurfaceConditions().SnowDepthMm<10 && Cycle->GetSurfaceConditions().Ice<1);
    return true;
}
#endif
