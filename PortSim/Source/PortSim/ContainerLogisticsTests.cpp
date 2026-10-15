#if WITH_DEV_AUTOMATION_TESTS
#include "ContainerSpecification.h"
#include "AGVDispatchPolicy.h"
#include "SpreaderTelescope.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FContainerSpecificationTest,"PortSim.Logistics.ContainerSpecifications",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FContainerSpecificationTest::RunTest(const FString& Parameters)
{
    TMap<int32,int32> Counts;
    for(int32 ID=2000;ID<2600;++ID)
    {
        const auto S=PortContainerSpecification::ForSequence(ID);
        Counts.FindOrAdd(S.LengthFt)++;
        TestTrue(TEXT("Gross mass includes positive tare and cargo"),S.TareMassKg>0 && S.CargoMassKg>0 && S.GrossMassKg()<30480.f);
        TestTrue(TEXT("ISO length class has matching external length"),
            (S.LengthFt==20 && FMath::IsNearlyEqual(S.LengthCm,605.8f)) ||
            (S.LengthFt==40 && FMath::IsNearlyEqual(S.LengthCm,1219.2f)) ||
            (S.LengthFt==45 && FMath::IsNearlyEqual(S.LengthCm,1371.6f)));
    }
    TestEqual(TEXT("20 ft 28 percent share"),Counts.FindRef(20),168);
    TestEqual(TEXT("40 ft 70 percent share"),Counts.FindRef(40),420);
    TestEqual(TEXT("45 ft 2 percent share"),Counts.FindRef(45),12);
    for(int32 Total:{1,2,7,37,200,600,601})
    {
        FIntVector Actual=FIntVector::ZeroValue;
        for(int32 I=0;I<Total;++I)
        {
            const auto S=PortContainerSpecification::ForSequence(2000+I,I,Total);
            if(S.LengthFt==20) ++Actual.X; else if(S.LengthFt==40) ++Actual.Y; else ++Actual.Z;
        }
        TestEqual(TEXT("Arbitrary manifest preserves rounded quotas"),Actual,PortContainerSpecification::CountsForManifest(Total));
        TestEqual(TEXT("No container lost to percentage rounding"),Actual.X+Actual.Y+Actual.Z,Total);
        TestTrue(TEXT("20ft rounding stays within one box"),FMath::Abs(Actual.X-Total*.28)<1.);
        TestTrue(TEXT("40ft rounding stays within one box"),FMath::Abs(Actual.Y-Total*.70)<1.);
        TestTrue(TEXT("45ft rounding stays within one box"),FMath::Abs(Actual.Z-Total*.02)<1.);
    }
    float Length=PortSpreaderTelescope::Length20Cm;
    for(int32 Step=0;Step<299;++Step)
        Length=PortSpreaderTelescope::Advance(Length,PortSpreaderTelescope::Length45Cm,.1f);
    TestFalse(TEXT("20 to 45 ft stroke does not finish before the public 30 second reference"),
        PortSpreaderTelescope::IsReady(Length,PortSpreaderTelescope::Length45Cm));
    Length=PortSpreaderTelescope::Advance(Length,PortSpreaderTelescope::Length45Cm,.1f);
    TestTrue(TEXT("20 to 45 ft stroke reaches its target in approximately 30 seconds"),
        PortSpreaderTelescope::IsReady(Length,PortSpreaderTelescope::Length45Cm));

    const float C20=PortSpreaderTelescope::TwistlockHalfLengthCm(PortSpreaderTelescope::Length20Cm);
    const float C40=PortSpreaderTelescope::TwistlockHalfLengthCm(PortSpreaderTelescope::Length40Cm);
    const float C45=PortSpreaderTelescope::TwistlockHalfLengthCm(PortSpreaderTelescope::Length45Cm);
    TestTrue(TEXT("Twistlock end beams have distinct ordered positions for 20/40/45 ft"),C20<C40 && C40<C45);
    TestTrue(TEXT("20 ft longitudinal twistlock centre spacing is approximately 5.852 m"),FMath::IsNearlyEqual(C20*2.f,585.2f,.2f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAGVDispatchPolicyTest,"PortSim.Logistics.CostAwareAGVDispatch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAGVDispatchPolicyTest::RunTest(const FString& Parameters)
{
    const FVector Target(4500,0,0);
    const double NearUsed=AGVDispatchPolicy::Score(FVector(6500,0,0),Target,4,6,5);
    const double FarUnused=AGVDispatchPolicy::Score(FVector(6500,20000,0),Target,4,5,5);
    TestTrue(TEXT("A very distant unused AGV does not beat a nearby vehicle solely for equal work counts"),NearUsed<FarUnused);

    const double NearUsedClose=AGVDispatchPolicy::Score(FVector(6500,0,0),Target,4,6,5);
    const double NearUnused=AGVDispatchPolicy::Score(FVector(6500,3000,0),Target,4,5,5);
    TestTrue(TEXT("Fairness resolves close ETA choices toward the less-used AGV"),NearUnused<NearUsedClose);
    TestEqual(TEXT("Workload fairness cannot outweigh more than 48 seconds of travel"),
        AGVDispatchPolicy::Score(FVector(6500,0,0),Target,4,100,0)-
        AGVDispatchPolicy::Score(FVector(6500,0,0),Target,4,0,0),48.);
    TArray<AGVDispatchPolicy::FRequest> Requests={
        {10,0,0,true,false}, // First box cannot yet be stacked: keep it aboard.
        {11,1,0,false,true}, // An inaccessible lower ship box cannot be picked.
        {12,2,10,true,true},
        {13,3,20,true,true}};
    TestEqual(TEXT("Blocked head does not idle an executable STS/AGV job"),AGVDispatchPolicy::SelectExecutable(Requests),12);
    Requests[0].YardReady=true;
    TestEqual(TEXT("Old request regains priority once its support/RMG is ready"),AGVDispatchPolicy::SelectExecutable(Requests),10);
    for(auto& R:Requests) R.YardReady=false;
    TestEqual(TEXT("No pickup if all downstream admissions are blocked"),AGVDispatchPolicy::SelectExecutable(Requests),INDEX_NONE);
    TestFalse(TEXT("A distant return cannot pin an STS with a closer idle AGV"),AGVDispatchPolicy::UseDirectReturn(300.,45.));
    TestTrue(TEXT("Reuse a return that reaches pickup before a parked AGV"),AGVDispatchPolicy::UseDirectReturn(40.,90.));
    const auto DepotPosition=[](int32 Bay)
    {
        return FVector(TerminalLayout::SiteCmX(TerminalLayout::AGVStandbyX(Bay)),
            TerminalLayout::AGVStandbyY(Bay),0);
    };
    const double FirstRow=AGVDispatchPolicy::EstimatedApproachDistanceCm(DepotPosition(0),Target,4,0);
    const double SecondRow=AGVDispatchPolicy::EstimatedApproachDistanceCm(DepotPosition(8),Target,4,8);
    TestEqual(TEXT("Opposite depot rows sharing an exit have equal approach cost, despite different straight-line distances"),
        FirstRow,SecondRow);
    TestTrue(TEXT("The next depot aisle incurs more travel on the southbound spine"),
        AGVDispatchPolicy::EstimatedApproachDistanceCm(DepotPosition(16),Target,4,16)>FirstRow);
    TestTrue(TEXT("Depot ETA includes its mandatory exit detour instead of straight-line pickup distance"),
        FirstRow>FVector::Dist2D(DepotPosition(0),Target));
    return true;
}
#endif
