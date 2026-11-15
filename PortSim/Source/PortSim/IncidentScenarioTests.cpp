#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortWorkingCrane.h"
#include "PortAGVActor.h"
#include "PortContainerActor.h"
#include "QuayCrane.h"
#include "Dom/JsonObject.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIncidentScenarioTest,"PortSim.Incidents.LiveCombinationAndRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FIncidentScenarioTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto* Owner=World->SpawnActor<AQuayCrane>();Owner->bAutoRunning=true;
    auto* Site=World->SpawnActor<APortSiteLogistics>();Site->SetOwner(Owner);Site->ReportBase=TEXT("IncidentNative");
    auto* V=World->SpawnActor<APortAGVActor>();V->InitializeVehicle(100);Site->Vehicles={V};Site->Jobs.SetNum(1);
    auto Stamp=[&](FJsonObject& C){C.SetStringField(TEXT("run_id"),TEXT("IncidentNative"));C.SetStringField(TEXT("control_session"),Site->DashboardControlSession);C.SetStringField(TEXT("created_at"),FDateTime::UtcNow().ToIso8601());};
    auto Step=[&](const TCHAR* Action,double Delay,double Duration)
    {auto S=MakeShared<FJsonObject>();S->SetStringField(TEXT("actor_id"),V->GetName());S->SetStringField(TEXT("action"),Action);S->SetNumberField(TEXT("delay"),Delay);S->SetNumberField(TEXT("duration"),Duration);S->SetNumberField(TEXT("timeout"),300);return S;};
    auto Rain=Step(TEXT("set_environment"),0,0);Rain->SetStringField(TEXT("actor_id"),TEXT("environment"));
    auto Weather=MakeShared<FJsonObject>();Weather->SetNumberField(TEXT("rain_mm_h"),25);Rain->SetObjectField(TEXT("weather"),Weather);
    FJsonObject Start;Stamp(Start);Start.SetStringField(TEXT("action"),TEXT("scenario_start"));Start.SetStringField(TEXT("id"),TEXT("native"));
    TArray<TSharedPtr<FJsonValue>> Steps={MakeShared<FJsonValueObject>(Rain),MakeShared<FJsonValueObject>(Step(TEXT("fail_reboot"),0,0)),MakeShared<FJsonValueObject>(Step(TEXT("fail"),0,5)),MakeShared<FJsonValueObject>(Step(TEXT("fail"),90,0))};
    Start.SetArrayField(TEXT("steps"),Steps);FString Error;
    TestFalse(TEXT("Duplicate fault rejected before any weather or machine mutation"),Site->ApplyDashboardCommand(Start,Error));
    TestFalse(TEXT("Rejected batch did not inject drive failure"),V->bDriveFault);
    Steps.Pop();Start.SetArrayField(TEXT("steps"),Steps);
    TestTrue(TEXT("Register simultaneous weather, sensor and drive failures"),Site->ApplyDashboardCommand(Start,Error));
    Site->AdvanceIncidentScenarios(.05f);
    TestTrue(TEXT("Weather changed in the actual shared environment"),Owner->GetEnvironment()->IsRainEnabled()&&Owner->GetEnvironment()->GetRainfallMmPerHour()==25);
    TestTrue(TEXT("Independent sensor and drive failures coexist"),V->bDriveFault&&V->bIncidentSensorFault&&!V->CanAcceptWork());
    Owner->bAutoPaused=true;const double Clock=Site->IncidentClock;Site->AdvanceIncidentScenarios(30);
    TestEqual(TEXT("Manual pause freezes event/reboot timers"),Site->IncidentClock,Clock);
    Owner->bAutoPaused=false;Owner->WeatherHold.Active=true;Site->AdvanceIncidentScenarios(6);
    TestTrue(TEXT("Weather hold still permits scheduled recovery"),Site->IncidentClock>Clock);
    TestTrue(TEXT("Drive recovery cannot repair a simultaneous sensor failure"),V->bIncidentSensorFault);
    TestFalse(TEXT("Drive was repaired by its own timer"),V->bDriveFault);
    Site->AdvanceIncidentScenarios(.05f);
    TestEqual(TEXT("A repair acknowledgement alone is not completion"),Site->IncidentRuns[0].Steps[2].State,FString(TEXT("awaiting_completion")));
    Site->AdvanceIncidentScenarios(60);TestTrue(TEXT("Sensor fault does not silently reboot without a recovery request"),V->bIncidentSensorFault&&V->IncidentSensorPhase==TEXT("failed"));
    FJsonObject Reboot;Reboot.SetStringField(TEXT("actor_id"),V->GetName());Reboot.SetStringField(TEXT("action"),TEXT("incident_clear"));Reboot.SetStringField(TEXT("fault_action"),TEXT("fail_reboot"));
    TestTrue(TEXT("Request reboot after the sensor error"),Site->ApplyIncidentCommand(Reboot,Error));
    TestTrue(TEXT("Reboot retains the failed-sensor interlock"),V->bIncidentSensorFault&&V->IncidentSensorPhase==TEXT("rebooting"));
    Site->AdvanceIncidentScenarios(30);TestTrue(TEXT("Reboot cannot finish early"),V->bIncidentSensorFault);
    Site->AdvanceIncidentScenarios(31);TestFalse(TEXT("Sensor reboot finishes after its actual dwell"),V->bIncidentSensorFault);
    FJsonObject Cancel;Stamp(Cancel);Cancel.SetStringField(TEXT("action"),TEXT("scenario_cancel"));Cancel.SetStringField(TEXT("scenario_id"),TEXT("native"));
    TestTrue(TEXT("Cancellation retains already applied scenario history"),Site->ApplyDashboardCommand(Cancel,Error));
    TestTrue(TEXT("Weather is not silently reset by cancellation"),Owner->GetEnvironment()->IsRainEnabled());
    Owner->GetEnvironment()->AdvanceEnvironment(20);TestTrue(TEXT("Rain evolves real surface accumulation"),Owner->GetEnvironment()->GetSurfaceConditions().WaterFilmMm>0);
    World->DestroyWorld(false);return true;
}
#endif
