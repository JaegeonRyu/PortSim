#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortContainerActor.h"
#include "PortWorkingCrane.h"
#include "TerminalLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInlandRoadTrafficTest,"PortSim.Traffic.InlandRoadQueues",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FInlandRoadTrafficTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Inland traffic verification world"),World)) return false;
    auto* Site=World->SpawnActor<APortSiteLogistics>();
    auto* A=World->SpawnActor<APortAGVActor>();
    auto* B=World->SpawnActor<APortAGVActor>();
    A->InitializeVehicle(100); B->InitializeVehicle(101);
    Site->LaneCount=9; Site->bDepotParking=Site->bReuseFreedApron=Site->bCargoAlignedHandover=true;
    Site->Vehicles={A,B}; Site->TrafficVehicles={A,B}; Site->Jobs.SetNum(2);
    // V106 was stopped by V128, 77 m ahead on the same northbound road,
    // despite a clear first traffic gate and a clear front LiDAR.
    const float NorthX=TerminalLayout::SiteCmX(TerminalLayout::AGVInlandInboundX);
    A->ResetVehicle(FVector(NorthX,7535.02f,0)); B->ResetVehicle(FVector(NorthX,15246.98f,0));
    auto& JA=Site->Jobs[0]; auto& JB=Site->Jobs[1];
    JA.Stage=JB.Stage=5;
    JA.Route={FVector(NorthX,9710.37,0),FVector(NorthX,11792.82,0),FVector(NorthX,15475.27,0),FVector(NorthX,20000,0)};
    JB.Route={FVector(NorthX,18249.41,0),FVector(NorthX,21153.53,0),FVector(NorthX,24057.65,0)};
    Site->BeginTrafficFrame(); Site->MoveVehicle(A,JA.Route[0],.05f);
    TestTrue(TEXT("Recorded same-direction follower moves while the first gate is clear"),A->Speed>0);
    TestFalse(TEXT("A distant same-direction leader does not cause FMS hold"),A->Sensors.bFMSHold);
    TestTrue(TEXT("Explicit front LiDAR remains clear"),A->Sensors.bLiDARClear);

    A->ResetVehicle(FVector(NorthX,0,0)); B->ResetVehicle(FVector(NorthX+5000,20000,0));
    JA.Route={FVector(NorthX,3000,0),FVector(NorthX,6000,0),FVector(NorthX,9000,0),FVector(NorthX,12000,0)};
    JA.Waypoint=0;
    Site->RoadReservations.Reset(); Site->RoadReservationEndWaypoint.Reset(); Site->RoadTargets.Reset();
    Site->bTrafficIndexReady=false;
    for(int Step=0;Step<600 && JA.Waypoint==0;++Step)
    { Site->TrafficClock+=.05; Site->BeginTrafficFrame(); Site->Drive(0,.05f); }
    TestTrue(TEXT("Empty straight road advances across a virtual traffic gate"),JA.Waypoint>0);
    TestTrue(TEXT("Rolling lease keeps driving through a clear gate instead of stopping"),A->Speed>300.f);

    // Recorded near-RMG merge starvation: new through traffic must queue
    // outside an older request's area, while vehicles inside can drain out.
    const float MergeSpine=TerminalLayout::SiteCmX(15300.f);
    A->ResetVehicle(FVector(MergeSpine,21200,0));
    B->ResetVehicle(FVector(16335.39f,22820,0)); B->SetActorRotation(FRotator(0,90,0)); B->SetFMSHold(true);
    JA.Route={FVector(MergeSpine,24500,0),FVector(MergeSpine,27700,0)};
    JA.Waypoint=0;
    Site->RoadReservations.Reset(); Site->RoadReservationEndWaypoint.Reset(); Site->RoadTargets.Reset();
    Site->TrafficClock=100; Site->RoadWaitSince.Add(101,0.);
    Site->PendingRoadRequests.Add(101,{FBox(FVector(MergeSpine-380,22300,-250),FVector(17100,28500,250))});
    Site->bTrafficIndexReady=false; Site->BeginTrafficFrame(); Site->MoveVehicle(A,JA.Route[0],.05f);
    TestTrue(TEXT("New through traffic leaves a gap for an older merge request"),A->Sensors.bFMSHold);
    TestEqual(TEXT("FMS identifies the actual waiting merge vehicle"),Site->RoadBlockers.FindRef(100),101);
    TestFalse(TEXT("A through-road waiter does not claim unrelated future junctions"),Site->PendingRoadRequests.Contains(100));
    A->ResetVehicle(FVector(MergeSpine,24500,0));
    JA.Route={FVector(MergeSpine,27700,0),FVector(MergeSpine,30700,0)};
    Site->BeginTrafficFrame(); Site->MoveVehicle(A,JA.Route[0],.05f);
    TestTrue(TEXT("A vehicle already inside the merge area can drain out"),A->Speed>0 && !A->Sensors.bFMSHold);
    Site->PendingRoadRequests.Reset(); Site->PendingRoadIndex.Reset(); Site->RoadWaitSince.Reset();

    Site->Jobs.Init(FSiteTransfer(),2); Site->Yard.SetNum(2); Site->Manifest.SetNum(2);
    Site->SlotAssigned.Init(false,2); Site->BlocksBusy.Init(false,1);
    Site->RMGBusy.Init(true,1); Site->YardApproachOwners.Init(0,1);
    // Admission now also checks that the destination crane is operational.
    Site->Equipment.Add(World->SpawnActor<APortWorkingCrane>());
    const FVector Dock(TerminalLayout::RMGHandoverX(1)*100.f,-8680,0);
    for(auto& Slot:Site->Yard)
    { Slot.Half=1; Slot.Crane=Slot.Block=0; Slot.Handover=Dock; Slot.Reserved=true; }
    Site->Yard[1].Occupied=false;
    auto* Cargo=World->SpawnActor<APortContainerActor>(); Cargo->GetBody()->SetSimulatePhysics(false);
    Site->Manifest[1].Actor=Cargo; Site->Manifest[1].PlannedSlot=1;
    Site->Jobs[0].Stage=5; Site->Jobs[0].Slot=Site->Jobs[0].RMG=0; Site->Jobs[0].bRMGReserved=true;
    Site->Jobs[1].Stage=2; Site->Jobs[1].Cargo=1;
    // V156/V117 met on this same aisle after its owner was released just 30 m
    // from the pad. The outgoing vehicle is still 100 m from the main road.
    A->ResetVehicle(FVector(48435.07f,-8680,0)); A->SetActorRotation(FRotator(0,-90,0));
    Site->ReleaseYardApproachIfClear(0);
    TestEqual(TEXT("Far RMG keeps its aisle while the outgoing vehicle is still inside"),Site->YardApproachOwners[0],0);
    TestFalse(TEXT("Next loaded AGV cannot enter against the outgoing vehicle"),Site->ReserveYardApproach(1));
    A->ResetVehicle(FVector(NorthX,Dock.Y+2000,0));
    Site->ReleaseYardApproachIfClear(0);
    TestEqual(TEXT("A fully cleared main-road merge releases the aisle"),Site->YardApproachOwners[0],INDEX_NONE);
    TestTrue(TEXT("Next loaded AGV is admitted after the exit clears"),Site->ReserveYardApproach(1));

    Site->Jobs.Init(FSiteTransfer(),2); Site->YardApproachOwners.Init(INDEX_NONE,1);
    Site->Manifest[0].Actor=Cargo; Site->Manifest[0].STS=4;
    Site->Manifest[0].Transform=FTransform(FVector(-4900,-15100,400));
    Site->Jobs[0].Stage=5; Site->Jobs[0].Cargo=Site->Jobs[0].Slot=0; Site->Jobs[0].STS=4;
    A->ResetVehicle(Dock); B->ResetVehicle(FVector(TerminalLayout::SiteCmX(4800),60000,0));
    Site->RoadReservations.Reset(); Site->RoadReservationEndWaypoint.Reset(); Site->RoadTargets.Reset();
    Site->bTrafficIndexReady=false; Site->BeginTrafficFrame();
    TestTrue(TEXT("Uncongested far RMG has a legal depot return"),Site->PlanRoadRoute(0,false));
    TestEqual(TEXT("Empty north corridor is preferred by actual route distance"),Site->Jobs[0].ReturnCorridor,2);
    const FBox Busy(FVector(NorthX-380,-6000,-250),FVector(NorthX+380,33000,250));
    for(int ID=900;ID<905;++ID) Site->RoadReservations.Add(ID,{Busy});
    Site->bTrafficIndexReady=false; Site->BeginTrafficFrame(); A->ResetVehicle(Dock);
    TestTrue(TEXT("Congested return can select another existing cross-terminal road"),Site->PlanRoadRoute(0,false));
    TestTrue(TEXT("Live initial planning diverts from the occupied north spine"),Site->Jobs[0].ReturnCorridor!=2);
    TestTrue(TEXT("Return still ends at its own depot bay"),Site->Jobs[0].Route.Last().Equals(Site->FleetPark(0),.1));
    World->DestroyWorld(false);
    return true;
}
#endif
