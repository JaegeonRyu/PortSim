#if WITH_DEV_AUTOMATION_TESTS
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "PortSiteLogistics.h"
#include "TerminalLayout.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMovingPickupRecoveryTest,"PortSim.Recovery.UnliftedPickup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMovingPickupRecoveryTest::RunTest(const FString& Parameters)
{
    FSpreaderAttitudeConfig Trim;Trim.Enabled=true;
    FSpreaderAttitude Plant;
    for(int I=0;I<360;++I)
    {
        const auto Before=Plant.Angle,Rate=Plant.Rate;
        Plant.Step(Trim,1./120.,Plant.Angle,Plant.Rate,FVector2D(1,-.5),FVector2D::ZeroVector,8000,1.13,5.99);
        TestTrue(TEXT("Trim has bounded torque and rate"),Plant.Torque.GetAbsMax()<=Trim.MaxTorque && Plant.Rate.GetAbsMax()<=Trim.MaxRate);
        TestTrue(TEXT("Trim cannot rotate or accelerate instantaneously"),(Plant.Angle-Before).GetAbsMax()<=Trim.MaxRate/120.+1.e-6 &&
            (Plant.Rate-Rate).GetAbsMax()<=Trim.MaxAcceleration/120.+1.e-6);
    }
    TestTrue(TEXT("Bounded plant converges to measured tilt"),Plant.Angle.Equals(FVector2D(1,-.5),.03));
    FSTSOperatingProfile Profile;
    if(!TestTrue(TEXT("Load STS operating profile"),Profile.Load(FPaths::ProjectDir()/TEXT("../Document/STS/STS_ReferenceData.json"),
        FPaths::ProjectConfigDir()/TEXT("STS_Simulation.json"))))return false;
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Pickup recovery world"),World))return false;
    auto* Site=World->SpawnActor<APortSiteLogistics>(); Site->bReady=true;Site->LaneCount=0;Site->YardCraneCount=0;
    Site->NextDashboardWall=Site->NextControlPoll=TNumericLimits<double>::Max();
    FActorSpawnParameters Spawn;Spawn.Owner=Site;
    APortWorkingCrane* Cranes[2]; APortContainerActor* Boxes[2];
    for(int I=0;I<2;++I)
    {
        const FVector Home(500000,I*20000,0),Source=Home+FVector(-1600,0,1500);
        auto* C=World->SpawnActor<APortWorkingCrane>(Home,FRotator::ZeroRotator,Spawn);
        C->SetSTSProfile(Profile);C->Configure(I+1,true,Source,Home+FVector(3000,0,149.5),false);
        auto* B=World->SpawnActor<APortContainerActor>(Source,FRotator::ZeroRotator,Spawn);
        B->InitializeContainer(9000+I);B->ConfigureSpecification(FPortContainerSpecification());
        B->GetBody()->SetSimulatePhysics(false);B->LocationOwner=ECargoOwner::Ship;
        B->SetSecuredVesselMotion(Source,FQuat::Identity,FVector::ZeroVector);
        TestTrue(TEXT("Bind physical ship cargo"),C->AssignCargo(B,Source,Home+FVector(3000,0,149.5),false,false));
        C->SetDestinationReady(false); Site->Equipment.Add(C);Site->ShipContainers.Add(B);
        Cranes[I]=C;Boxes[I]=B;
    }
    auto* C=Cranes[0];auto* B=Boxes[0];
    C->Stage=2;C->Head=C->Local(B->GetActorLocation())+FVector(0,0,154.5);C->UpdateParts();C->SampleSTS(true);
    const auto Pose=B->GetActorTransform();
    C->Pickup.Failure=ESTSPickupFailure::Safety;
    TestFalse(TEXT("Lock/collision/load failures cannot enter alignment recovery"),C->StartPickupRecovery());
    C->bCarrying=true;
    TestFalse(TEXT("A suspended load cannot be abandoned by pickup recovery"),C->CanRecoverPickup());C->bCarrying=false;
    B->GetBody()->SetSimulatePhysics(true);
    TestFalse(TEXT("An unsupported dynamic cargo cannot be reacquired automatically"),C->CanRecoverPickup());B->GetBody()->SetSimulatePhysics(false);
    C->CornerLocked[0]=true;C->LockProgress[0]=Profile.Pickup.LockTime;C->SampleSTS(true);
    for(int Cycle=0;Cycle<=Profile.Pickup.RecoveryMaxCycles;++Cycle)
    {
        C->Pickup.Phase=ESTSPickupPhase::Failed;C->Pickup.Failure=ESTSPickupFailure::AlignmentExhausted;
        C->Pickup.Fault=TEXT("Pickup alignment attempts exhausted");
        const double UnlockHeight=C->HeadPosition().Z;
        if(!TestTrue(TEXT("Only verified unlifted alignment exhaustion can recover"),C->StartPickupRecovery()))break;
        Site->Advance(.05,false);
        TestTrue(TEXT("No hoist retreat while unlocking"),C->PickupRecoveryStep==1 && FMath::Abs(C->HeadPosition().Z-UnlockHeight)<3);
        for(int Step=0;Step<1200 && C->PickupRecoveryStep && C->Fault.IsEmpty();++Step)Site->Advance(.05,false);
        TestTrue(TEXT("Cargo pose/owner/mass are preserved during physical retreat"),
            Pose.Equals(B->GetActorTransform(),.001) && B->LocationOwner==ECargoOwner::Ship && !B->GetAttachParentActor());
        TestTrue(TEXT("Unrelated terminal operations remain enabled"),Site->Fault.IsEmpty());
        if(Cycle<Profile.Pickup.RecoveryMaxCycles)
            TestTrue(TEXT("Bounded retreat returns to measured acquisition"),C->Fault.IsEmpty() && !C->PickupRecoveryStep && C->Pickup.Phase==ESTSPickupPhase::Align);
    }
    TestTrue(TEXT("Exhausted retries isolate only the safely retreated crane"),C->IsPickupUnavailable() && !C->RetainsLoad() && Site->Fault.IsEmpty());
    const double Clock=Site->SimulationTime;Site->Advance(2,false);
    TestTrue(TEXT("Other equipment and site clock continue after local isolation"),Site->SimulationTime>Clock && Cranes[1]->Stage>0 && Cranes[1]->Fault.IsEmpty());
    TestTrue(TEXT("Web distinguishes pickup isolation from sensor failure"),C->DashboardState()->GetBoolField(TEXT("pickup_failure")) &&
        C->DashboardState()->GetStringField(TEXT("fault_scope"))==TEXT("equipment"));
    C->RepairEquipment();TestTrue(TEXT("Unloaded isolated crane supports explicit repair"),C->Fault.IsEmpty());
    AddExpectedError(TEXT("WORKING_CRANE_1: Injected overload protection"),EAutomationExpectedErrorFlags::Contains,1);
    AddExpectedError(TEXT("SITE_LOGISTICS_FAIL: Injected overload protection"),EAutomationExpectedErrorFlags::Contains,1);
    C->Stop(TEXT("Injected overload protection"));Site->Advance(.05,false);
    TestTrue(TEXT("Critical safety faults still stop the whole terminal"),!Site->Fault.IsEmpty());
    World->DestroyWorld(false);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLoggedSeaPickupTest,"PortSim.STS.LoggedSeaPickup",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FLoggedSeaPickupTest::RunTest(const FString& Parameters)
{
    FSTSOperatingProfile Profile;
    if(!TestTrue(TEXT("Load reference for logged sea state"),Profile.Load(FPaths::ProjectDir()/TEXT("../Document/STS/STS_ReferenceData.json"),
        FPaths::ProjectConfigDir()/TEXT("STS_Simulation.json"))))return false;
    // Same sea/wind seed as the failing interactive run. Replay several phases
    // at the logged STS-5 cargo region, without relaxing any seating conditions.
    for(double Start:{2561.,2669.,2849.})
    {
        const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
            .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
        auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
        if(!TestNotNull(TEXT("Sea pickup world"),World))return false;
        FPortSeaState Sea;Sea.Initialize(120892294);Sea.Advance(Start);
        FPortWindState Wind;Wind.Initialize(120892294);Wind.Advance(Start);
        const FVector Base=Start==2561.?FVector(-4550,-17100,840):FVector(-3660,-15100,1120);
        const FVector Pivot(TerminalLayout::VesselOriginXM*100,TerminalLayout::VesselCenterY(1)*100,250);
        const FVector Destination(TerminalLayout::SiteCmX(6000),TerminalLayout::STSCenterY(4)*100,349.5);
        auto Motion=Sea.SampleVesselMotion(1);const FVector Source=Motion.TransformPosition(Base,Pivot);
        auto* Crane=World->SpawnActor<APortWorkingCrane>(FVector(1200,TerminalLayout::STSCenterY(4)*100,0),FRotator::ZeroRotator);
        Crane->SetSTSProfile(Profile);Crane->Configure(51,true,Source,Destination,false);
        // Start at the previous job's quayside end: test the long next-job
        // approach as well as the final fine seating loop.
        Crane->Head=Crane->Local(Destination);Crane->Head.Z=Crane->SafeZ;
        Crane->JobStartHead=Crane->StageHoldTarget=Crane->Head;Crane->UpdateParts();
        auto* Box=World->SpawnActor<APortContainerActor>(Source,FRotator::ZeroRotator);
        Box->InitializeContainer(2251);Box->ConfigureSpecification(FPortContainerSpecification());Box->SetPhysicalParameters(20300,FVector::ZeroVector);
        Box->GetBody()->SetSimulatePhysics(false);Box->LocationOwner=ECargoOwner::Ship;
        auto Move=[&](double Dt)
        {
            const auto M=Sea.SampleVesselMotion(1);
            Box->SetSecuredVesselMotion(M.TransformPosition(Base,Pivot),M.TransformRotation(FQuat::Identity),
                M.VelocityAtPosition(Base,Pivot),M.AccelerationAtPosition(Base,Pivot),M.JerkAtPosition(Base,Pivot),Dt);
            Crane->UpdateMovingShipSource(Box,Box->GetActorLocation());
        };
        Move(0);TestTrue(TEXT("Assign moving C2251-sized cargo"),Crane->AssignCargo(Box,Box->GetActorLocation(),
            Destination,false,false));Crane->SetDestinationReady(false);
        double Elapsed=0;
        for(int Step=0;Step<6000 && Crane->Stage<3 && Crane->Fault.IsEmpty();++Step)
        {
            Sea.Advance(.05);Wind.Advance(.05);Crane->SetWindVelocity(Wind.EffectiveVelocity());
            if(!Crane->bCarrying)Move(.05);
            Crane->Advance(.05,false);Elapsed+=.05;
            FString EnvelopeError;
            if(!Crane->ValidateOperation(EnvelopeError))
            {AddError(EnvelopeError);break;}
        }
        UE_LOG(LogTemp,Display,TEXT("LOGGED_SEA_PICKUP: seed=120892294 phase=%.0f verified=%d elapsed_s=%.2f fault=%s"),Start,Crane->Stage>=3,Elapsed,*Crane->Fault);
        TestTrue(TEXT("Full seating, four locks and trial lift pass in logged sea phases"),
            Crane->Fault.IsEmpty() && Crane->Stage>=3 && Crane->bCarrying && Crane->Observation.AllLocked());
        World->DestroyWorld(false);
    }
    return true;
}
#endif
