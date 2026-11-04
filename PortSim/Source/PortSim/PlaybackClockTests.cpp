#if WITH_DEV_AUTOMATION_TESTS
#include "PortSimTimeStep.h"
#include "PortSimGameEngine.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaybackFixedWorldStepTest,"PortSim.Playback.FixedWorldStep",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPlaybackFixedWorldStepTest::RunTest(const FString& Parameters)
{
    const double Current=FApp::GetCurrentTime(),Last=FApp::GetLastTime(),Delta=FApp::GetDeltaTime();
    ON_SCOPE_EXIT
    {
        FApp::SetCurrentTime(Last);
        FApp::UpdateLastTime();
        FApp::SetCurrentTime(Current);
        FApp::SetDeltaTime(Delta);
    };
    auto* Clock=NewObject<UPortSimTimeStep>();
    Clock->Initialize(nullptr);
    // Include transitions in both directions and an overloaded frame. Neither
    // requested speed nor delayed presentation may change the physics interval.
    for(float Rate:{1.f,2.f,4.f,8.f,16.f,8.f,1.f})
    {
        Clock->SetPlaybackRate(Rate);
        for(int32 Frame=0;Frame<3;++Frame)
        {
            if(Frame==1) FPlatformProcess::SleepNoStats(.02f);
            const double Before=FApp::GetCurrentTime();
            TestFalse(TEXT("Custom clock supplies the application time"),Clock->UpdateTimeStep(nullptr));
            TestEqual(TEXT("World delta is independent of speed and stalls"),FApp::GetDeltaTime(),UPortSimTimeStep::StepSeconds);
            TestTrue(TEXT("Application time advances by one fixed world step"),
                FMath::Abs(FApp::GetCurrentTime()-Before-UPortSimTimeStep::StepSeconds)<1.e-6);
        }
    }
    Clock->Shutdown(nullptr);
    TestTrue(TEXT("Engine time resumes after shutdown"),Clock->UpdateTimeStep(nullptr));
    FPlaybackInputGate Gate;
    Gate.Submit(10.);
    TestEqual(TEXT("External input forces 1x"),Gate.EffectiveRate(8.f,10.),1.f);
    Gate.Submit(11.);
    TestEqual(TEXT("Repeated input extends quiet period"),Gate.EffectiveRate(8.f,12.5),1.f);
    TestEqual(TEXT("Requested rate returns after wall-time quiet period"),Gate.EffectiveRate(8.f,13.),8.f);
    TestEqual(TEXT("Every input changes generation"),Gate.Revision,uint64(2));
    Clock->Initialize(nullptr); Clock->SetPlaybackRate(8.f); Clock->NotifyExternalInput();
    TestEqual(TEXT("Live clock applies input hold immediately"),Clock->GetEffectiveRate(),1.f);
    Clock->UpdateTimeStep(nullptr);
    TestEqual(TEXT("Input hold retains fixed physical step"),FApp::GetDeltaTime(),UPortSimTimeStep::StepSeconds);
    Clock->Shutdown(nullptr);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlaybackPresentationCadenceTest,"PortSim.Playback.PresentationCadence",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPlaybackPresentationCadenceTest::RunTest(const FString& Parameters)
{
    auto* Engine=NewObject<UPortSimGameEngine>();
    auto* Clock=NewObject<UPortSimTimeStep>(Engine);
    Engine->SetCustomTimeStep(Clock);
    ON_SCOPE_EXIT { Engine->SetCustomTimeStep(nullptr); };
    for(float Rate:{2.f,4.f,8.f,16.f})
    {
        Clock->SetPlaybackRate(Rate);
        Engine->StepsSincePresentation=0;
        for(int32 Step=1;Step<=int32(Rate);++Step)
        {
            // Every draw deadline has already passed, as on an overloaded GPU.
            Engine->NextPresentationWall=0;
            Engine->RedrawViewports(false);
            TestEqual(TEXT("Late render cannot consume every live physics step"),
                Engine->StepsSincePresentation,Step<int32(Rate)?Step:0);
        }
    }
    Clock->SetBufferedPlayback(true);
    Engine->NextPresentationWall=0;
    Engine->RedrawViewports(false);
    TestEqual(TEXT("Buffered model batches need only one engine step per picture"),Engine->StepsSincePresentation,0);
    return true;
}
#endif
