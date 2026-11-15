#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortWorkingCrane.h"
#include "AGVRoadNetwork.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FIncidentExtensionTest,"PortSim.Incidents.NavigationCommunicationAndRoadClosure",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FIncidentExtensionTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto* Site=World->SpawnActor<APortSiteLogistics>();auto* V=World->SpawnActor<APortAGVActor>();V->InitializeVehicle(100);
    Site->Vehicles={V};Site->Jobs.SetNum(1);const FVector Goal(0,3000,0);Site->Jobs[0].Stage=3;Site->Jobs[0].Route={Goal};
    auto Command=[&](const TCHAR* Action,bool Clear=false)
    {FJsonObject C;C.SetStringField(TEXT("actor_id"),V->GetName());C.SetStringField(Clear?TEXT("fault_action"):TEXT("action"),Action);FString Error;return Site->ApplyExtendedIncident(C,Error,Clear);};
    TestTrue(TEXT("Inject absolute localization loss during an existing road job"),Command(TEXT("localization_loss")));
    for(int I=0;I<200;++I){Site->AdvanceIncidentScenarios(.1f);V->SetFMSCommand(Goal,0,1,false);V->MoveToPosition(Goal,.1f);}
    TestTrue(TEXT("Wheel odometry continues while absolute fixes are unavailable"),V->Sensors.WheelOdometryCm>100&&!V->Sensors.bTransponderLocked);
    TestTrue(TEXT("Uncertainty bounds eventually stop the real planar drive"),V->IncidentPositionUncertaintyCm>=25&&V->Speed<1);
    TestTrue(TEXT("Degraded pose is validated against its declared uncertainty, not a false absolute fix"),V->LocalizationObservationValid());
    TestFalse(TEXT("New dispatch is forbidden before relocalization"),V->CanAcceptWork());
    TestTrue(TEXT("Remove localization outage without inventing an absolute fix"),Command(TEXT("localization_loss"),true));
    TestTrue(TEXT("Repair still requires an approved node fix"),V->bIncidentNeedsPositionFix&&!V->Sensors.bTransponderLocked);
    for(int I=0;I<700&&V->bIncidentNeedsPositionFix;++I){V->SetFMSCommand(Goal,0,1,true);V->MoveToPosition(Goal,.1f);}
    TestTrue(TEXT("Vehicle reacquires its transponder at the real destination"),V->Sensors.bTransponderLocked&&!V->bIncidentNeedsPositionFix&&V->GetActorLocation().Equals(Goal,6));
    const float FixedError=V->Sensors.PositionErrorCm;V->Sensors.PositionErrorCm=50;
    TestFalse(TEXT("Nominal operation still rejects a bad absolute fix"),V->LocalizationObservationValid());V->Sensors.PositionErrorCm=FixedError;
    TestTrue(TEXT("Inject FMS latency"),Command(TEXT("fms_delay")));
    const FVector Next=Goal+FVector(0,2000,0);V->SetFMSCommand(Next,4,5,false);
    TestTrue(TEXT("Unreceived new command is pending"),V->bIncidentFMSPending&&V->Sensors.FMSNodeIndex!=4);
    Site->AdvanceIncidentScenarios(1);V->SetFMSCommand(Next,4,5,false);TestTrue(TEXT("One second is too early"),V->bIncidentFMSPending);
    Site->AdvanceIncidentScenarios(1.1f);V->SetFMSCommand(Next,4,5,false);
    TestTrue(TEXT("Delayed command eventually arrives without node substitution"),!V->bIncidentFMSPending&&V->Sensors.FMSNodeIndex==4);
    TestTrue(TEXT("Inject independent FMS disconnect"),Command(TEXT("fms_disconnect")));
    TestTrue(TEXT("Clear latency only"),Command(TEXT("fms_delay"),true));V->SetFMSCommand(Next,5,6,false);
    TestTrue(TEXT("Disconnect cannot be overwritten by repeated command generation"),!V->Sensors.bFMSConnected&&V->Sensors.FMSNodeIndex==4);
    TestTrue(TEXT("Restore communication"),Command(TEXT("fms_disconnect"),true));V->SetFMSCommand(Next,5,6,false);
    TestTrue(TEXT("Fresh FMS command confirms reconnect"),V->Sensors.bFMSConnected&&V->Sensors.FMSNodeIndex==5);
    FPortIncidentRoadClosure Block;Block.Bounds=FBox(FVector(-300,4500,-100),FVector(300,5500,200));Site->IncidentRoadClosures.Add(V->GetName(),Block);
    FAGVRoadNetwork N;const FVector A(0,0,0),B(0,10000,0),C(5000,0,0),D(5000,10000,0);
    N.Add(A,B,true);N.Add(A,C,true);N.Add(C,D,true);N.Add(D,B,true);TArray<FVector> Route;
    auto Cost=[&](FVector From,FVector To){return Site->IncidentRoadBlocked(From,To,true)?TNumericLimits<double>::Max():0.;};
    TestTrue(TEXT("Closed direct road uses the real alternate road"),N.Find(A,B,Route,Cost));
    TestTrue(TEXT("Route visits alternate spine"),Route.ContainsByPredicate([](FVector P){return P.X>4000;}));
    FVector From=A;for(FVector To:Route){TestFalse(TEXT("Every chosen segment clears the closure envelope"),Site->IncidentRoadBlocked(From,To));From=To;}
    Site->IncidentRoadClosures[V->GetName()].Active=false;TestTrue(TEXT("Road removal reconnects the direct road"),N.Find(A,B,Route,Cost));
    TestFalse(TEXT("No unnecessary detour after road restoration"),Route.ContainsByPredicate([](FVector P){return P.X>4000;}));

    V->ResetVehicle(FVector::ZeroVector);
    V->Speed=450.f;
    TestTrue(TEXT("Lose absolute position at driving speed"),Command(TEXT("localization_loss")));
    TestTrue(TEXT("Disconnect FMS while the moving AGV is estimating position"),Command(TEXT("fms_disconnect")));
    bool ValidDuringBraking=true;
    for(int I=0;I<20;++I)
    {
        Site->AdvanceIncidentScenarios(.1f);
        V->SetFMSCommand(Goal,0,1,false);
        V->MoveToPosition(Goal,.1f);
        ValidDuringBraking&=V->LocalizationObservationValid();
    }
    TestTrue(TEXT("Wheel slip during braking is included in localization uncertainty"),ValidDuringBraking);
    TestTrue(TEXT("Degraded position estimation still permits commanded braking"),V->Speed<450.f&&!V->Sensors.bTransponderLocked);
    World->DestroyWorld(false);return true;
}
#endif
