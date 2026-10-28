#if WITH_DEV_AUTOMATION_TESTS
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "AGVReference.h"
#include "AGVTrafficGeometry.h"
#include "TerminalLayout.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAGVLeasedEntryDockTest,"PortSim.Traffic.LeasedYardEntryDocking",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAGVLeasedEntryDockTest::RunTest(const FString& Parameters)
{
    FSiteTransfer Job;Job.Stage=3;Job.Route={FVector(0,0,0),FVector(1800,0,0)};
    Job.YardEntryWaypoint=Job.Waypoint=0;
    for(bool Reserved:{false,true})
    {
        Job.bRMGReserved=Reserved;
        TestEqual(TEXT("Only pending RMG admission needs an extra entry stop"),Job.IsDockingWaypoint(),!Reserved);
        Job.Waypoint=1;TestTrue(TEXT("Final handover always requires precise docking"),Job.IsDockingWaypoint());Job.Waypoint=0;
    }
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Entry docking world"),W))return false;
    auto* V=W->SpawnActor<APortAGVActor>();V->InitializeVehicle(100);
    // Road docking starts on the approach axis; an arbitrary perpendicular
    // approach needs a proper rounded road, not a last-metre steering trick.
    for(double Offset:{-200.,200.}) for(bool Reverse:{false,true}) for(double Angle:{-90.,90.})
    {
        V->ResetVehicle(FVector(-700,Offset,0));V->SetActorRotation(FRotator(0,Angle,0));
        V->SetPayload(12300);V->Sensors.bReversing=Reverse;
        V->Dynamics.Reset(Reverse?-2.5:2.5,V->DynamicsSettings);V->Speed=250;
        bool Arrived=false;
        for(int Step=0;Step<20*120 && !Arrived;++Step)
        {
            V->SetFMSCommand(FVector::ZeroVector,34,36,true,100,FVector(1,0,0));
            Arrived=V->MoveToPosition(FVector::ZeroVector,.05,true);
        }
        TestTrue(FString::Printf(TEXT("Loaded entry offset=%.0f yaw=%.0f reverse=%d"),Offset,Angle,Reverse),Arrived);
        TestTrue(TEXT("Entry completion retains position and zero-speed precision"),
            V->GetActorLocation().Equals(FVector::ZeroVector,AGVReference::DockingAccuracyCm) && V->Speed<.5f);
    }
    auto* Site=W->SpawnActor<APortSiteLogistics>();Site->LaneCount=9;Site->bReuseFreedApron=Site->bDepotParking=true;
    Site->bCargoAlignedHandover=false;Site->Vehicles={V};Site->TrafficVehicles={V};Site->Jobs.SetNum(1);
    Site->Manifest.SetNum(1);Site->Yard.SetNum(1);Site->STSOwners.Init(INDEX_NONE,9);
    auto& J=Site->Jobs[0];J.Stage=3;J.STS=4;J.Cargo=J.Slot=0;J.bRMGReserved=true;
    Site->Manifest[0].STS=4;
    auto& Slot=Site->Yard[0];Slot.Block=7;Slot.Half=0;Slot.Occupied=false;
    Slot.Handover=FVector(TerminalLayout::RMGHandoverX(0)*100,TerminalLayout::RMGHandoverY(7)*100,0);
    for(bool Congestion:{false,true})
    {
        J=FSiteTransfer();J.Stage=3;J.STS=4;J.Cargo=J.Slot=0;J.bRMGReserved=true;
        V->ResetVehicle(Site->QuayPark(4));V->SetPayload(12300);
        if(Congestion)
        {
            const FBox Busy(FVector(9000,-20700,-250),FVector(14400,-20660,250));
            Site->RoadReservations.Add(999,{Busy});Site->ReservationIndex.Set(999,{Busy});
        }
        TestTrue(TEXT("Actual STS5 to block8 road route exists"),Site->PlanRoadRoute(0,false));
        TestTrue(TEXT("Congestion cannot create an unrounded turn at the near-yard entry"),
            AGVTrafficGeometry::RoundedRouteHasNoSharpTurns(V->GetActorLocation(),J.Route));
        Site->RoadReservations.Remove(999);Site->ReservationIndex.Remove(999);
        bool Finished=false;
        for(int Step=0;Step<20*600 && !Finished;++Step){Site->TrafficClock+=.05;Finished=Site->Drive(0,.05);}
        TestTrue(FString::Printf(TEXT("Full loaded route docks without orbiting: congestion=%d wp=%d position=%s docking=%d"),
            Congestion,J.Waypoint,*V->GetActorLocation().ToCompactString(),V->Sensors.bDockingNode),Finished);
    }
    W->DestroyWorld(false);return true;
}
#endif
