#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortContainerActor.h"
#include "AGVTrafficGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadedTrafficRecoveryTest,"PortSim.Traffic.LoadedLiDARDeadlockRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLoadedTrafficRecoveryTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Recovery fixture world"),World)) return false;
    auto* Site=World->SpawnActor<APortSiteLogistics>();
    auto* A=World->SpawnActor<APortAGVActor>();
    auto* B=World->SpawnActor<APortAGVActor>();
    // Recorded V119/V135 poses; contiguous fixture IDs retain fleet indexing.
    const FVector Start(13887.977495646425,-13338.88825213254,0);
    const FVector Other(14725.254543678727,-12481.854262157914,0);
    A->InitializeVehicle(100); B->InitializeVehicle(101);
    A->ResetVehicle(Start); B->ResetVehicle(Other);
    A->SetActorRotation(FRotator(0,-.00099524318937938,0));
    B->SetActorRotation(FRotator(0,-75.3419283883103,0));
    A->PayloadKg=B->PayloadKg=12000;
    A->Sensors.bControlledStop=true; A->Sensors.bFMSHold=false;
    B->Sensors.bFMSHold=true; B->Sensors.bReversing=true;
    Site->LaneCount=9; Site->TrafficClock=100;
    Site->Vehicles={A,B}; Site->TrafficVehicles={A,B}; Site->Jobs.SetNum(2);
    auto& JA=Site->Jobs[0]; auto& JB=Site->Jobs[1];
    JA.Stage=JB.Stage=3; JA.StationaryTime=JB.StationaryTime=20;
    JA.Route={FVector(Start.X,-9904.295758883005,0),FVector(Start.X,-6569.7,0)};
    // Recovery temporarily substitutes two points but retains this original
    // staging boundary. Every lookahead must clamp against the active route.
    for(int N=0;N<49;++N) JA.Route.Add(JA.Route.Last()+FVector(0,300,0));
    JB.Route={FVector(14538.80045628248,-12179.999727755785,0),FVector(16338.800416049344,-12179.999727755785,0)};
    JA.YardEntryWaypoint=JA.Route.Num()-1; JB.YardEntryWaypoint=1;
    auto* Cargo=World->SpawnActor<APortContainerActor>();
    Cargo->GetBody()->SetSimulatePhysics(false);
    Cargo->SetActorLocation(A->CargoPosition());
    Cargo->AttachToActor(A,FAttachmentTransformRules::KeepWorldTransform);
    Cargo->LocationOwner=ECargoOwner::AGV; JA.Actor=Cargo;
    Site->RoadBlockers={{100,101},{101,100}};
    Site->BeginTrafficFrame();
    Site->RecoverTrafficCycle();
    TestTrue(TEXT("Mixed LiDAR/FMS cycle admits loaded straight-road yield"),Site->TrafficRecoveryTargets.Contains(100));
    const FVector Goal=Site->TrafficRecoveryTargets.FindRef(100);
    TestTrue(TEXT("Recorded clearance-buffer contact permits separating solid-body sweep"),
        AGVTrafficGeometry::ClearReverseSweep(Start,Start-FVector(0,2000,0),Other,B->GetActorRightVector()));
    TestFalse(TEXT("A vehicle behind the reversing AGV blocks the entire sweep"),
        AGVTrafficGeometry::ClearReverseSweep(Start,Start-FVector(0,2000,0),Start-FVector(0,1700,0),FVector(0,1,0)));
    bool NoLateralSlip=true;
    for(int32 I=0;I<1000 && Site->TrafficRecoveryTargets.Contains(100);++I)
    {
        Site->BeginTrafficFrame(); Site->Drive(0,.05f);
        NoLateralSlip&=A->Sensors.LateralSlipCm<.01f;
    }
    TestTrue(TEXT("Recovery never slides laterally"),NoLateralSlip);
    TestFalse(TEXT("Loaded reverse completes with normal motion controller"),Site->TrafficRecoveryTargets.Contains(100));
    TestTrue(TEXT("Chassis physically reaches reverse endpoint"),A->GetActorLocation().Equals(Goal,5.f));
    TestTrue(TEXT("Load ownership and attachment survive reversal"),
        Cargo->LocationOwner==ECargoOwner::AGV && Cargo->GetAttachParentActor()==A && A->PayloadKg==12000);
    TestTrue(TEXT("Original FMS route survives recovery"),JA.Route[0].Equals(FVector(Start.X,-9904.295758883005,0),.01f));
    const FVector After=A->GetActorLocation();
    Site->Drive(0,.05f);
    TestTrue(TEXT("Yield persists until priority vehicle clears, preventing oscillation"),
        Site->TrafficRecoveryYieldTo.Contains(100) && A->GetActorLocation().Equals(After,.001f));
    Site->MoveVehicle(B,JB.Route[0],.05f);
    TestTrue(TEXT("Previously blocked priority vehicle can move"),B->Speed>0);
    // Fixture placement of the priority vehicle after clearing the crossing.
    B->SetActorLocation(JB.Route.Last()); B->SetActorRotation(FRotator(0,90,0)); B->Speed=0;
    Site->BeginTrafficFrame(); Site->Drive(0,.05f);
    TestFalse(TEXT("Cleared crossing releases the yielding vehicle"),Site->TrafficRecoveryYieldTo.Contains(100));
    World->DestroyWorld(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FApronIntersectionRecoveryTest,"PortSim.Traffic.ApronCrossingRecovery",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FApronIntersectionRecoveryTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(true).RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Apron crossing fixture world"),World)) return false;
    auto* Site=World->SpawnActor<APortSiteLogistics>();
    auto* Empty=World->SpawnActor<APortAGVActor>();
    auto* Loaded=World->SpawnActor<APortAGVActor>();
    // Exact stopped V137/V122 poses from the interrupted 52-delivery run.
    const FVector Start(6783.362862308931,-16449.999116729194,0);
    const FVector Other(5445.205731553406,-15196.571385228918,0);
    Empty->InitializeVehicle(100); Loaded->InitializeVehicle(101);
    Empty->ResetVehicle(Start); Loaded->ResetVehicle(Other);
    Empty->SetActorRotation(FRotator(0,90,0));
    Loaded->SetActorRotation(FRotator(0,.1809085304983571,0));
    Empty->Sensors.bFMSHold=Loaded->Sensors.bFMSHold=true;
    Loaded->Sensors.bReversing=true; Loaded->PayloadKg=12000;
    Site->LaneCount=9; Site->TrafficClock=100; Site->bReuseFreedApron=true;
    Site->Vehicles={Empty,Loaded}; Site->TrafficVehicles={Empty,Loaded}; Site->Jobs.SetNum(2);
    auto& A=Site->Jobs[0]; auto& B=Site->Jobs[1];
    A.Stage=6; B.Stage=3; A.StationaryTime=B.StationaryTime=20;
    A.Route={FVector(5118.555549653865,-16449.999632313848,0),FVector(4538,-18000,0)};
    B.Route={FVector(Other.X,-19000,0),FVector(Other.X,-22000,0)};
    B.YardEntryWaypoint=1;
    Site->RoadBlockers={{100,101},{101,100}};
    Site->BeginTrafficFrame(); Site->RecoverTrafficCycle();
    TestTrue(TEXT("Empty STS approach may yield along its actual horizontal road"),Site->TrafficRecoveryTargets.Contains(100));
    const FVector Goal=Site->TrafficRecoveryTargets.FindRef(100);
    TestTrue(TEXT("Yield reverses east, never slides north or south"),Goal.X>Start.X && FMath::Abs(Goal.Y-Start.Y)<.01f);
    bool NoSlip=true;
    for(int I=0;I<1000 && Site->TrafficRecoveryTargets.Contains(100);++I)
    {
        Site->BeginTrafficFrame(); Site->Drive(0,.05f);
        NoSlip&=Empty->Sensors.LateralSlipCm<.01f;
    }
    TestTrue(TEXT("Horizontal recovery retains car steering and longitudinal motion"),NoSlip);
    TestFalse(TEXT("Horizontal recovery completes without the vertical docking-axis lock"),Site->TrafficRecoveryTargets.Contains(100));
    TestTrue(TEXT("Empty chassis reaches the actual horizontal reverse endpoint"),Empty->GetActorLocation().Equals(Goal,5.f));
    Site->BeginTrafficFrame(); Site->MoveVehicle(Loaded,B.Route[0],.05f);
    TestTrue(TEXT("Yield releases the loaded vehicle's crossing approach"),Loaded->Speed>0);
    World->DestroyWorld(false);
    return true;
}
#endif
