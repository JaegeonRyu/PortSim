#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortSupportVehicle.h"
#include "PortContainerActor.h"
#include "TerminalLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoadedAGVRecoveryTest,"PortSim.Recovery.LoadedAGV",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLoadedAGVRecoveryTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    auto* Site=World->SpawnActor<APortSiteLogistics>(); Site->LaneCount=9;
    const FVector Origin(TerminalLayout::SiteCmX(12500),0,0);
    auto* Old=World->SpawnActor<APortAGVActor>(Origin,FRotator::ZeroRotator);
    auto* New=World->SpawnActor<APortAGVActor>(Origin+FVector(900,0,0),FRotator::ZeroRotator);
    Old->InitializeVehicle(100);New->InitializeVehicle(101);
    Site->Vehicles={Old,New};Site->Jobs.SetNum(2);
    auto* RS=World->SpawnActor<APortSupportVehicle>(Origin+FVector(450,0,0),FRotator::ZeroRotator);
    RS->Configure(EPortSupportType::ReachStacker,1);
    FLoadedAGVRecovery R;R.Failed=0;R.Replacement=1;R.Stacker=RS;R.Target=New->GetActorLocation();R.Phase=1;
    for(int32 I=0;I<2;++I)
    {
        auto* C=World->SpawnActor<APortContainerActor>();C->InitializeContainer(9000+I);
        C->GetBody()->SetSimulatePhysics(false);C->LocationOwner=ECargoOwner::AGV;
        C->SetActorLocation(Old->DeckPosition(I?306.7f:-306.7f));
        C->AttachToActor(Old,FAttachmentTransformRules::KeepWorldTransform);
        R.Cargo.Add(C);R.DeckTransforms.Add(C->GetActorTransform().GetRelativeTransform(Old->GetActorTransform()));
        Site->ShipContainers.Add(C);
    }
    auto& J=Site->Jobs[0];J.Actor=R.Cargo[0];J.Stage=3;J.Cargo=0;J.ExtraCargo=1;J.TripID=17;
    J.Route={Origin+FVector(0,8000,0)};J.YardEntryWaypoint=0;
    Site->Jobs[1].Stage=FSiteTransfer::ToRecovery;
    FString Error;
    TestFalse(TEXT("Healthy loaded AGV cannot be replaced"),Site->CanRecoverLoadedAGV(0,Error));
    Old->SetDriveFailure(true);
    TestTrue(TEXT("Stopped loaded road job is eligible"),Site->CanRecoverLoadedAGV(0,Error));
    TArray<FVector> Access;
    TestTrue(TEXT("Depot has a road route to the receiving bay"),Site->PlanLoadedRecoveryRoad(Site->FleetPark(1),R.Target,FVector(0,1,0),1,Access));
    Site->LoadedAGVRecoveries.Add(R);
    TestFalse(TEXT("Duplicate recovery is rejected"),Site->RequestLoadedAGVRecovery(0,Error));
    New->SetDriveFailure(true);Site->AdvanceLoadedAGVRecoveries(10);
    TestEqual(TEXT("Replacement failure retains first box"),R.Cargo[0]->GetAttachParentActor(),static_cast<AActor*>(Old));
    New->SetDriveFailure(false);
    bool SawLift=false;
    for(int32 Step=0;Step<24000 && Site->HasLoadedAGVRecovery();++Step)
    {
        Site->AdvanceLoadedAGVRecoveries(1.f/60);
        if(!Site->ValidateLoadedAGVRecoveries(Error)) {AddError(Error);break;}
        SawLift|=R.Cargo[0]->LocationOwner==ECargoOwner::ReachStacker;
    }
    TestTrue(TEXT("Cargo passes through stacker ownership"),SawLift);
    for(const auto& C:R.Cargo)
    {
        TestEqual(TEXT("Both boxes seated on replacement"),C->GetAttachParentActor(),static_cast<AActor*>(New));
        TestEqual(TEXT("Final AGV ownership"),C->LocationOwner,ECargoOwner::AGV);
    }
    TestFalse(TEXT("Recovery finishes with a connected road route"),Site->HasLoadedAGVRecovery());
    TestEqual(TEXT("Trip identity inherited"),Site->Jobs[1].TripID,17);
    TestEqual(TEXT("Failed vehicle job released"),Site->Jobs[0].Stage,0);
    TestTrue(TEXT("Transfer does not repair failed drive"),Old->bDriveFault);
    World->DestroyWorld(false);return true;
}
#endif
