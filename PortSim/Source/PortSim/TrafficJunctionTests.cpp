#if WITH_DEV_AUTOMATION_TESTS
#include "TrafficJunctionPolicy.h"
#include "TrafficSpatialIndex.h"
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTrafficJunctionTest,"PortSim.Traffic.JunctionAdmission",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FTrafficJunctionTest::RunTest(const FString& Parameters)
{
    const TArray<FVector> Route={FVector(0,0,0),FVector(900,0,0),FVector(900,900,0),FVector(900,2600,0)};
    TArray<FBox> Exit;
    TrafficJunctionPolicy::AppendExitCorridor(Exit,Route,0,Route.Num(),[](int32){return FVector(730,210,250);});
    TestEqual(TEXT("Dogleg length alone does not clear junction"),Exit.Num(),3);
    FTrafficSpatialIndex Occupancy;
    Occupancy.Set(4,{FBox(FVector(800,2400,-100),FVector(1000,2800,100))});
    TestTrue(TEXT("Occupied downstream exit blocks admission before entering junction"),Occupancy.Query(Exit).Contains(4));
    Occupancy.Remove(4);
    TestTrue(TEXT("Cleared exit permits admission"),Occupancy.Query(Exit).IsEmpty());
    TArray<FVector> LongRoad;
    for(int32 I=0;I<12;++I) LongRoad.Add(FVector(I*3000,0,0));
    Exit.Reset();
    TrafficJunctionPolicy::AppendExitCorridor(Exit,LongRoad,0,LongRoad.Num(),[](int32){return FVector(730,210,250);});
    TestTrue(TEXT("A straight gate does not claim the road occupied by a distant leader"),Exit.IsEmpty());
    const TArray<FVector> Turn={FVector(0,0,0),FVector(4000,0,0),FVector(4000,600,0),
        FVector(4000,1400,0),FVector(4000,3400,0),FVector(4000,6400,0)};
    Exit.Reset();
    TrafficJunctionPolicy::AppendExitCorridor(Exit,Turn,1,Turn.Num(),[](int32){return FVector(730,210,250);});
    TestEqual(TEXT("A turn retains the entire coupled corner and clear exit"),Exit.Num(),3);
    TestTrue(TEXT("Turn claim stops after a complete straight exit"),Exit.Last().Max.Y<6400.f);
    Exit.Reset();
    TrafficJunctionPolicy::AppendExitCorridor(Exit,Route,0,2,[](int32){return FVector(730,210,250);});
    TestTrue(TEXT("Staging stop never claims unowned crane handover"),Exit.IsEmpty());
    TMap<int32,int32> Blockers={{100,102},{102,106},{106,101},{101,100},{110,100}};
    const auto Cycle=TrafficJunctionPolicy::WaitCycle(Blockers,110);
    TestEqual(TEXT("Four-vehicle cycle excludes incoming queue"),Cycle.Num(),4);
    TestFalse(TEXT("Upstream waiter is not part of cycle"),Cycle.Contains(110));
    Blockers.Remove(101);
    TestTrue(TEXT("Open wait chain is not a deadlock"),TrafficJunctionPolicy::WaitCycle(Blockers,110).IsEmpty());
    const double Old=10,New=20;
    TestTrue(TEXT("Longer road wait beats vehicle ID"),TrafficJunctionPolicy::OlderRequestFirst(&Old,&New,200,100));
    TestFalse(TEXT("New request cannot bypass waiting vehicle"),TrafficJunctionPolicy::OlderRequestFirst(nullptr,&Old,1,200));
    TestTrue(TEXT("Equal waiting times have deterministic order"),TrafficJunctionPolicy::OlderRequestFirst(&Old,&Old,100,200));
    TestEqual(TEXT("Pickup head start is small and bounded"),TrafficJunctionPolicy::MissionBonus(true,0,false),20.);
    TestEqual(TEXT("Suspended STS urgency is capped"),TrafficJunctionPolicy::MissionBonus(true,10000,false),40.);
    TestEqual(TEXT("Return trips do not receive cargo waiting credit"),TrafficJunctionPolicy::MissionBonus(false,10000,false),0.);
    TestEqual(TEXT("Clearing a loaded STS bay receives exit credit"),TrafficJunctionPolicy::MissionBonus(false,0,true),40.);

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Road priority test world"),World)) return false;
    auto* Site=World->SpawnActor<APortSiteLogistics>();
    for(int32 I=0;I<3;++I)
    {
        auto* V=World->SpawnActor<APortAGVActor>(); V->VehicleID=100+I;
        Site->Vehicles.Add(V); Site->Jobs.AddDefaulted();
    }
    Site->bPickupRoadPriority=true; Site->TrafficClock=120;
    Site->Jobs[0].Stage=6; Site->Jobs[0].STS=0;
    Site->Jobs[1].Stage=5; Site->Jobs[1].STS=0;
    Site->Jobs[2].Stage=3; Site->Jobs[2].STS=0; Site->STSOwners={2};
    Site->RoadWaitSince={{100,115},{101,110},{102,115}};
    TestTrue(TEXT("Actual pickup job beats slightly older parking return"),Site->RoadRequestBefore(100,101));
    TestTrue(TEXT("Loaded handover exit drains before an equal-age pickup"),Site->RoadRequestBefore(102,100));
    Site->Jobs[1].ReturnSTS=1;
    TestEqual(TEXT("Direct RMG to STS pickup is treated as productive travel"),Site->RoadPriorityBonus(101),20.);
    Site->Jobs[1].ReturnSTS=INDEX_NONE;
    Site->RoadWaitSince[101]=70;
    TestTrue(TEXT("Aging overrides pickup preference to prevent return starvation"),Site->RoadRequestBefore(101,100));
    Site->RoadWaitSince.Remove(100);
    TestTrue(TEXT("New pickup cannot perpetually bypass a long waiting return"),Site->RoadRequestBefore(101,100));
    Site->Jobs[0].Stage=FSiteTransfer::ReturnToStandby;
    TestEqual(TEXT("Charging return does not inherit pickup credit"),Site->RoadPriorityBonus(100),0.);
    Site->Jobs[0].Stage=6; Site->RoadWaitSince[101]=110; Site->RoadWaitSince.Add(100,115);
    Site->bPickupRoadPriority=false;
    TestTrue(TEXT("A/B baseline restores actual FIFO admission"),Site->RoadRequestBefore(101,100));
    TestEqual(TEXT("A/B baseline does not publish pickup gap priority"),Site->RoadPriorityBonus(100),0.);
    Site->bPickupRoadPriority=true;
    for(int32 A=100;A<103;++A) for(int32 B=100;B<103;++B) for(int32 C=100;C<103;++C)
        if(Site->RoadRequestBefore(A,B) && Site->RoadRequestBefore(B,C))
            TestTrue(TEXT("Mission and wait ordering stays transitive"),Site->RoadRequestBefore(A,C));
    World->DestroyWorld(false);
    return true;
}
#endif
