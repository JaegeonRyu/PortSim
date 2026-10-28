#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorkerImpactTest,"PortSim.Recovery.WorkerIntrusionImpacts",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWorkerImpactTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Worker impact world"),World))return false;
    auto* Site=World->SpawnActor<APortSiteLogistics>();Site->YardCraneCount=1;
    for(int32 I=0;I<5;++I)
    {auto* V=World->SpawnActor<APortAGVActor>();V->InitializeVehicle(100+I);Site->Vehicles.Add(V);Site->Jobs.AddDefaulted();}
    auto* RMG=World->SpawnActor<APortWorkingCrane>();Site->Equipment.Add(RMG);
    Site->Vehicles[0]->Emergency.Active=true;
    Site->Vehicles[1]->Sensors.bFMSHold=true;Site->RoadBlockers.Add(101,100);
    Site->Vehicles[2]->Sensors.bControlledStop=true;Site->Vehicles[2]->Sensors.LiDARBlockingVehicleID=101;
    Site->Vehicles[3]->Sensors.bFMSHold=true;Site->RoadBlockers.Add(103,999);
    Site->RoadBlockers.Add(104,100); // stale request without an actual hold
    Site->Jobs[1].bRMGReserved=true;Site->Jobs[1].RMG=0;
    auto* STS=World->SpawnActor<APortWorkingCrane>();
    FSTSOperatingProfile Profile;
    if(!TestTrue(TEXT("STS reference loads for the real handover interlock"),Profile.Load(
        FPaths::ProjectDir()/TEXT("../Document/STS/STS_ReferenceData.json"),FPaths::ProjectConfigDir()/TEXT("STS_Simulation.json"))))
    {World->DestroyWorld(false);return false;}
    STS->SetSTSProfile(Profile);
    const FVector Source(-2800,0,569.5),Destination(2800,0,349.5);
    STS->Configure(2,true,Source,Destination,false);
    Site->Equipment.Add(STS);Site->STSOwners.Init(2,1);Site->Jobs[2].STS=0;
    auto* Cargo=World->SpawnActor<APortContainerActor>();
    TestTrue(TEXT("STS receives an actual crane job"),STS->AssignCargo(Cargo,Source,Destination,false,false));
    STS->SetDestinationReady(false);STS->Stage=5;
    TestTrue(TEXT("Crane is genuinely interlocked waiting for its AGV"),STS->IsWaitingForAGV());
    Site->UpdateWorkerImpacts();
    TestEqual(TEXT("Root, actual queue links, waiting STS and idle reserved RMG are affected"),Site->WorkerImpacts.Num(),5);
    auto Has=[&](AActor* Actor,const TCHAR* Reason)
    {return Site->WorkerImpacts.ContainsByPredicate([&](const auto& E){return E->GetStringField(TEXT("to_actor"))==Actor->GetName() && E->GetStringField(TEXT("reason"))==Reason;});};
    TestTrue(TEXT("FMS blocker and indirect LiDAR queue are tracked"),Has(Site->Vehicles[1],TEXT("fms_queue")) && Has(Site->Vehicles[2],TEXT("lidar_queue")));
    TestTrue(TEXT("Idle reserved RMG is labelled as a reservation, not motion"),Has(RMG,TEXT("rmg_reservation_wait")));
    TestTrue(TEXT("Actual STS handover wait is linked through the indirect AGV queue"),Has(STS,TEXT("sts_handover_wait")));
    Site->SimulationTime=4;Site->UpdateWorkerImpacts();
    TestEqual(TEXT("Unchanged dependencies do not spam history"),Site->RecoveryEvents.Num(),5);
    for(const auto& E:Site->WorkerImpacts)TestEqual(TEXT("Duration uses simulation time"),E->GetNumberField(TEXT("duration_seconds")),4.);
    Site->Vehicles[1]->Sensors.bFMSHold=false;Site->UpdateWorkerImpacts();
    TestEqual(TEXT("Broken queue chain clears all downstream current effects"),Site->WorkerImpacts.Num(),1);
    Site->Vehicles[0]->Emergency.Active=false;Site->UpdateWorkerImpacts();
    TestEqual(TEXT("Removing worker clears every current highlight"),Site->WorkerImpacts.Num(),0);
    TestEqual(TEXT("Start and clearance history is retained"),Site->RecoveryEvents.Num(),10);
    World->DestroyWorld(false);return !HasAnyErrors();
}
#endif
