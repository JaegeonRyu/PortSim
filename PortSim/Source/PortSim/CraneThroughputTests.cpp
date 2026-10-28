#if WITH_DEV_AUTOMATION_TESTS
#include "STSOperatingProfile.h"
#include "CraneTravelBraking.h"
#include "YardFlowEstimate.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCraneTravelResponseTest,"PortSim.STS.TravelResponse",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCraneTravelResponseTest::RunTest(const FString& Parameters)
{
    FSTSOperatingProfile P;
    if(!TestTrue(TEXT("Load operating profile"),P.Load(FPaths::ProjectDir()/TEXT("../Document/STS/STS_ReferenceData.json"),
        FPaths::ProjectConfigDir()/TEXT("STS_Simulation.json")))) return false;
    for(double Length:{12.,25.,40.}) for(double Wind:{0.,5.})
    {
        auto C=P.Dynamics; C.Wind=FVector(Wind,0,0);
        FSTSSuspension Plant;
        double X=0,V=0,SampleX=0,SampleV=0,Angle=0,Rate=0,LastOutside=0,Peak=0;
        constexpr double H=1./120.,Mass=25300.;
        const double Offset=C.WindEquilibriumOffset(Length,Mass).X;
        const double Equilibrium=FMath::Asin(FMath::Clamp(Offset/Length,-1.,1.));
        for(int32 Step=0;Step<120*100;++Step)
        {
            // Sampled 20 Hz feedback, while the plant runs at 120 Hz.
            if(Step%6==0) { SampleX=X; SampleV=V; Angle=Plant.Angle.X; Rate=Plant.Rate.X; }
            const double Error=40-Offset-SampleX-SampleV*(Step%6)*H;
            double A=C.TravelAcceleration(Error,SampleV,Length,0,Rate,Angle,Equilibrium);
            A=CraneTravelBraking::Acceleration(Error*100,V*100,A*100,80,100,H)*.01;
            const FVector2D Limited=CraneTravelBraking::SwayAcceleration(FVector2D(Angle,0),FVector2D(Rate,0),
                FVector2D(C.AerodynamicForce(FVector(V,0,0)).X/Mass,0),FVector2D(A,0),FVector2D(.8,.8),
                Length,0,C.Damping,FMath::DegreesToRadians(12.),H);
            const double Next=FMath::Clamp(V+Limited.X*H,-4.,4.);
            A=(Next-V)/H; V=Next; X+=V*H;
            Plant.Step(C,H,Length,0,FVector(A,0,0),FVector(V,0,0),Mass,FVector(.2,0,0),P.HoistPowerW);
            Peak=FMath::Max(Peak,Plant.SwayDegrees());
            if(FMath::Abs(40-X-Plant.Offset.X)>.05 || FMath::Abs(V+Plant.OffsetVelocity.X)>.05 || Plant.SwayDegrees()>1)
                LastOutside=Step*H;
        }
        UE_LOG(LogTemp,Display,TEXT("TRAVEL_RESPONSE: rope=%.0f wind=%.0f settled=%.2f peak_sway=%.2f"),Length,Wind,LastOutside,Peak);
        TestTrue(TEXT("40 m travel settles within 60 seconds with sampled sensors"),LastOutside<60);
        TestTrue(TEXT("Rope forces remain valid and sway inside sensor envelope"),Plant.Fault.IsEmpty() && Peak<20);
    }
    auto Legacy=P.Dynamics; Legacy.TravelResponseRatio=0;
    TestEqual(TEXT("Explicit zero retains configured legacy feedback"),Legacy.TravelAcceleration(2,.1,25,0,.01,.02,0),
        Legacy.HorizontalAcceleration(2,.1,25,.01,.02,0));
    auto Disabled=P.Dynamics; Disabled.AntiSway=false;
    TestEqual(TEXT("Disabling anti-sway does not silently re-enable feedback"),Disabled.TravelAcceleration(2,.1,25,0,.01,.02,0),
        Disabled.HorizontalAcceleration(2,.1,25,.01,.02,0));
    const auto BusyStack=YardFlowEstimate::Evaluate(0,800,700,100,40,false,false);
    const auto FreeFloor=YardFlowEstimate::Evaluate(0,0,0,140,40,true,false);
    TestTrue(TEXT("Free compatible floor beats a blocked stack despite longer travel"),FreeFloor.Cost<BusyStack.Cost);
    const auto FreeStack=YardFlowEstimate::Evaluate(800,0,700,100,40,false,false);
    const auto SameFloor=YardFlowEstimate::Evaluate(800,0,0,100,40,true,false);
    TestTrue(TEXT("Free floor avoids a needless forecast support dependency"),SameFloor.Cost<FreeStack.Cost);
    const auto DistantFloor=YardFlowEstimate::Evaluate(800,0,0,280,40,true,false);
    TestTrue(TEXT("Compatible stack remains worthwhile when it saves enough travel"),FreeStack.Cost<DistantFloor.Cost);
    TestTrue(TEXT("Approach forecast includes the outbound empty vehicle"),FreeFloor.Released>FreeFloor.Placed);
    return true;
}
#endif
