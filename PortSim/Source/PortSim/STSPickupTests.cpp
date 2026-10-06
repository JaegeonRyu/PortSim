#if WITH_DEV_AUTOMATION_TESTS
#include "STSOperatingProfile.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSTSPickupTest,"PortSim.STS.SensorPickup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSTSPickupTest::RunTest(const FString& Parameters)
{
    FSTSPickupConfig C;FSTSObservation O;O.bValid=O.bTargetVisible=true;
    O.SpreaderPosition=FVector(0,0,154.5);
    for(bool& Contact:O.CornerSeated)Contact=true;
    FSTSPickupController P;double Now=0;
    auto Tick=[&](bool Fresh=true){Now+=.1;if(Fresh)O.Timestamp=Now;P.Update(C,O,Now,.1,.5,FVector::ZeroVector,65000);};
    O.CornerSeated[2]=false;
    for(int I=0;I<20;++I)Tick();
    TestTrue(TEXT("Three contacts cannot request any lock"),P.Phase==ESTSPickupPhase::Align&&!P.RequestLocks[0]);
    TestTrue(TEXT("Bounded horizontal deck-following speed is accepted separately from vertical impact speed"),
        C.PickupSpeedWithin(FVector(C.RelativeSpeed+1,0,0)));
    TestFalse(TEXT("Excess vertical approach speed remains blocked"),
        C.PickupSpeedWithin(FVector(0,0,C.RelativeSpeed+1)));
    O.CornerSeated[2]=true;
    Tick();
    TestTrue(TEXT("Four seating contacts start lock actuation during the verification dwell"),
        P.Phase==ESTSPickupPhase::Seat&&P.RequestLocks[0]&&P.RequestLocks[1]&&P.RequestLocks[2]&&P.RequestLocks[3]);
    for(int I=0;I<11;++I)Tick();
    TestTrue(TEXT("Lock command is not lock feedback"),P.Phase==ESTSPickupPhase::Lock&&P.RequestLocks[0]);
    for(bool& Lock:O.Locked)Lock=true;
    Tick();TestTrue(TEXT("Four lock feedbacks allow attachment"),P.Phase==ESTSPickupPhase::Attach);
    P.Attached(O.SpreaderPosition);O.SpreaderPosition.Z+=C.TrialHeight;
    for(float& Force:O.CornerLoadsN)Force=12000*9.80665/4;
    O.bCargoSupported=true;
    for(int I=0;I<20;++I)Tick();
    TestFalse(TEXT("Supported cargo cannot pass trial"),P.EstimateValid);
    O.bCargoSupported=false;O.CornerLoadsN[0]=0;
    for(int I=0;I<20;++I)Tick();
    TestFalse(TEXT("Unloaded corner blocks full hoist"),P.EstimateValid);
    for(float& Force:O.CornerLoadsN)Force=12000*9.80665/4;
    for(int I=0;I<14;++I)Tick();
    TestTrue(TEXT("Stable suspended load permits full hoist"),P.EstimateValid);
    TestTrue(TEXT("Mass derived from measurements"),FMath::Abs(P.EstimatedMass-12000)<1);

    // Wind creates a normal horizontal equilibrium offset during the trial
    // hold. It must not be judged by the much tighter twist-lock seating limit.
    P=FSTSPickupController();
    O.SpreaderPosition=FVector::ZeroVector; O.SpreaderVelocity=FVector::ZeroVector; O.bCargoSupported=false;
    P.Attached(O.SpreaderPosition); O.SpreaderPosition=FVector(C.TrialHorizontalTolerance-1,0,C.TrialHeight);
    for(int I=0;I<14;++I)Tick();
    TestTrue(TEXT("Bounded horizontal wind offset permits trial hold"),P.EstimateValid);

    P=FSTSPickupController();
    O.SpreaderPosition=FVector::ZeroVector; P.Attached(O.SpreaderPosition);
    O.SpreaderPosition=FVector(C.TrialHorizontalTolerance+1,0,C.TrialHeight);
    for(int I=0;I<14;++I)Tick();
    TestFalse(TEXT("Excessive horizontal drift blocks trial hold"),P.EstimateValid);

    P=FSTSPickupController();P.Attached(FVector(0,0,154.5));O.SpreaderPosition=FVector(0,0,154.5+C.TrialHeight);Tick();
    const double Before=P.StableTime;
    for(int I=0;I<3;++I)Tick(false);
    TestEqual(TEXT("Repeated samples cannot accumulate proof time"),P.StableTime,Before);
    for(int I=0;I<4;++I)Tick(false);
    TestTrue(TEXT("Stale sample faults rather than succeeds"),!P.Fault.IsEmpty()&&!P.EstimateValid);
    // A 120 Hz controller consuming 20 Hz observations must count sensor time
    // once, without stretching the configured trial hold by six times.
    P=FSTSPickupController();P.Attached(FVector(0,0,154.5));
    Now=0; O.Timestamp=0;
    for(int I=1;I<=150;++I)
    {
        Now=I/120.;
        if(I%6==0) O.Timestamp=Now;
        P.Update(C,O,Now,1./120.,.5,FVector::ZeroVector,65000);
    }
    TestTrue(TEXT("20 Hz sensor hold completes on time with 120 Hz control"),P.EstimateValid&&P.Fault.IsEmpty());

    // Losing the downward camera during large sway must command a return to
    // the reserved cargo location, otherwise the spreader freezes outside the
    // camera cone and can never reacquire the target.
    P=FSTSPickupController(); O=FSTSObservation(); O.bValid=true; O.Timestamp=0;
    const FVector NominalCargo(100,200,300);
    P.Update(C,O,0,.1,.5,NominalCargo,65000);
    TestTrue(TEXT("Invisible target returns to nominal camera acquisition pose"),
        P.Target.Equals(NominalCargo+FVector(0,0,154.5),.001));
    return true;
}
#endif
