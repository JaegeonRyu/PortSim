#if WITH_DEV_AUTOMATION_TESTS
#include "TrafficJunctionPolicy.h"
#include "TrafficSpatialIndex.h"
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
    return true;
}
#endif
