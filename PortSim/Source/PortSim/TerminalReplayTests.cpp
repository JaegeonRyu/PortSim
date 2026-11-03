#if WITH_DEV_AUTOMATION_TESTS
#include "TerminalCheckpoint.h"
#include "TerminalReplay.h"
#include "QuayCrane.h"
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "PortSimTimeStep.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "Physics/Experimental/PhysScene_Chaos.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerminalReplayCheckpointTest,"PortSim.Playback.TerminalCheckpoint",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FTerminalReplayCheckpointTest::RunTest(const FString& Parameters)
{
    const FString Command=FCommandLine::Get();
    FCommandLine::Append(TEXT(" -PortSimShips=1 -PortSimCargo1=9 -PortSimEnvironmentSeed=166290927"));
    ON_SCOPE_EXIT { FCommandLine::Set(*Command); };
    auto* BatchVariable=IConsoleManager::Get().FindConsoleVariable(TEXT("PortSim.BatchLivePhysics"));
    const int32 PreviousBatch=BatchVariable->GetInt();
    BatchVariable->Set(0,ECVF_SetByCode); // Exercise the unsupported/disabled fallback first.
    ON_SCOPE_EXIT { BatchVariable->Set(PreviousBatch,ECVF_SetByCode); };
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Owner=World->SpawnActor<AQuayCrane>();
    Owner->DispatchBeginPlay();
    Owner->StartAutomatic(false);
    auto* Site=Owner->SiteLogistics.Get();
    if(!TestNotNull(TEXT("Real terminal initialized"),Site)) return false;
    auto Initial=FTerminalCheckpoint::Capture(*Owner);
    if(!TestTrue(TEXT("Initial terminal has no Chaos bodies"),Initial.IsValid())) return false;
    const double InitialTime=Site->SimulationTime;
    auto* Vehicle=Site->Vehicles[0].Get();
    const FVector InitialPosition=Vehicle->GetActorLocation();
    Vehicle->Sensors.WheelOdometryCm=123;
    Site->RoadReservations.Add(123,{FBox(FVector::ZeroVector,FVector(100))});
    Site->Manifest[0].State=1;
    Site->Manifest[0].Actor->LocationOwner=ECargoOwner::AGV;
    Vehicle->SetActorLocation(InitialPosition+FVector(1000,2000,0));
    Owner->Environment->AdvanceEnvironment(.1f);
    TestTrue(TEXT("Checkpoint restores immediately"),Initial->Restore());
    TestTrue(TEXT("Position restored"),Vehicle->GetActorLocation().Equals(InitialPosition,1.e-6));
    TestEqual(TEXT("Sensor clock restored"),Vehicle->Sensors.WheelOdometryCm,0.f);
    TestFalse(TEXT("Reservation changes discarded"),Site->RoadReservations.Contains(123));
    TestEqual(TEXT("Cargo ownership restored"),Site->Manifest[0].Actor->LocationOwner,ECargoOwner::Ship);
    for(int32 I=0;I<8;++I) Owner->AdvanceTerminalReplayStep(1.f/60.f);
    const double ReferenceTime=Site->SimulationTime;
    const FVector ReferencePosition=Vehicle->GetActorLocation();
    const auto ReferenceSensors=Vehicle->Sensors;
    const auto ReferenceWind=Owner->Environment->GetEffectiveWindVelocity();
    Initial->Restore();
    for(int32 I=0;I<8;++I) Owner->AdvanceTerminalReplayStep(1.f/60.f);
    TestEqual(TEXT("Recalculation preserves simulation time"),Site->SimulationTime,ReferenceTime);
    TestTrue(TEXT("Recalculation preserves AGV position"),Vehicle->GetActorLocation().Equals(ReferencePosition,1.e-6));
    TestEqual(TEXT("Recalculation preserves sensor odometry"),Vehicle->Sensors.WheelOdometryCm,ReferenceSensors.WheelOdometryCm);
    TestTrue(TEXT("Random weather state restored"),Owner->Environment->GetEffectiveWindVelocity().Equals(ReferenceWind,1.e-9));
    Initial->Restore();
    Owner->SimulationSpeed=8; Owner->ApplyPlaybackRate();
    // Bypass the setup input hold only in this test, via a fresh clock.
    Owner->PlaybackClock->Shutdown(nullptr); Owner->PlaybackClock->Initialize(nullptr);
    // InputGate is reset by initialization; no actual wall-clock sleeps needed.
    TestTrue(TEXT("Real terminal prepares future"),Owner->TerminalReplay.Tick(*Owner,0.));
    TestEqual(TEXT("Prediction does not expose future simulation time"),Site->SimulationTime,InitialTime);
    TestTrue(TEXT("Preparation leaves displayed geometry unchanged"),Vehicle->GetActorLocation().Equals(InitialPosition,1.e-6));
    TestTrue(TEXT("Future buffer contains prepared physics"),Owner->TerminalReplay.BufferedSeconds()>0.);
    TestTrue(TEXT("Real terminal enters buffered replay"),Owner->TerminalReplay.Tick(*Owner,1./60.));
    TestEqual(TEXT("8x display matches eight reference physics steps"),Site->SimulationTime,ReferenceTime);
    TestTrue(TEXT("Replay geometry equals continuous model"),Vehicle->GetActorLocation().Equals(ReferencePosition,1.e-6));
    // A delayed render frame must not discard time or trigger a refill pause.
    const double BeforeDelay=Site->SimulationTime;
    TestTrue(TEXT("Delayed frame stays in replay"),Owner->TerminalReplay.Tick(*Owner,.1));
    TestTrue(TEXT("100ms at 8x advances 800ms"),FMath::Abs(Site->SimulationTime-BeforeDelay-.8)<1.e-5);
    for(const int32 Rate : {2,4,8,16})
    {
        Owner->TerminalReplay.Invalidate();
        Owner->SimulationSpeed=Rate; Owner->ApplyPlaybackRate();
        Owner->PlaybackClock->Shutdown(nullptr); Owner->PlaybackClock->Initialize(nullptr);
        const double Start=Owner->TerminalReplay.DisplayedSeconds();
        double Wall=0.;
        for(int32 Frame=0;Frame<20;++Frame)
        {
            const double Dt=Frame%2==0?.04:.06;
            Wall+=Dt;
            TestTrue(TEXT("Variable cadence remains buffered"),Owner->TerminalReplay.Tick(*Owner,Dt));
            const double Error=Wall*Rate-(Owner->TerminalReplay.DisplayedSeconds()-Start);
            TestTrue(FString::Printf(TEXT("Playback tracks elapsed time rate=%d frame=%d error=%.9f"),Rate,Frame,Error),Error>=-1.e-5 && Error<Rate/60.+1.e-5);
        }
        TestTrue(FString::Printf(TEXT("No elapsed time lost rate=%d error=%.9f"),Rate,Wall*Rate-(Owner->TerminalReplay.DisplayedSeconds()-Start)),
            FMath::Abs(Owner->TerminalReplay.DisplayedSeconds()-Start+Owner->TerminalReplay.Credit-Wall*Rate)<1.e-9);
    }
    // A multi-second stall is capped per presentation without losing the debt
    // or retaining one full terminal snapshot for every skipped picture.
    Owner->TerminalReplay.Invalidate();
    Owner->SimulationSpeed=16; Owner->ApplyPlaybackRate();
    Owner->PlaybackClock->Shutdown(nullptr); Owner->PlaybackClock->Initialize(nullptr);
    const double BeforeStall=Owner->TerminalReplay.DisplayedSeconds();
    TestTrue(TEXT("Large stall remains buffered"),Owner->TerminalReplay.Tick(*Owner,2.));
    TestEqual(TEXT("Catch-up bounded to 960 fixed steps"),Owner->TerminalReplay.DisplayedSeconds()-BeforeStall,16.);
    TestTrue(TEXT("Skipped pictures do not accumulate checkpoints"),Owner->TerminalReplay.Future.Num()<=2);
    TestTrue(TEXT("Remaining debt is processed next presentation"),Owner->TerminalReplay.Tick(*Owner,0.));
    TestEqual(TEXT("Two-second 16x stall preserves all 32 seconds"),Owner->TerminalReplay.DisplayedSeconds()-BeforeStall,32.);
    const double BeforeBoundary=Owner->TerminalReplay.DisplayedSeconds();
    auto* BoundaryBody=Site->Manifest[0].Actor->GetBody();
    BoundaryBody->SetSimulatePhysics(true); BoundaryBody->PutAllRigidBodiesToSleep();
    TestFalse(TEXT("Chaos boundary uses a real world step"),Owner->TerminalReplay.Tick(*Owner,.1));
    TestTrue(TEXT("Chaos boundary retains unprocessed elapsed time"),
        FMath::Abs(Owner->TerminalReplay.Credit-(1.6-1./60.))<1.e-9);
    BoundaryBody->SetSimulatePhysics(false);
    // Represent the model part of the one live world step; Chaos itself is not
    // advanced by this accounting regression and is never checkpointed.
    Owner->AdvanceTerminalReplayStep(1.f/60.f);
    TestTrue(TEXT("Replay resumes after the boundary"),Owner->TerminalReplay.Tick(*Owner,0.));
    TestTrue(TEXT("Boundary round-trip loses no playback time"),
        FMath::Abs(Owner->TerminalReplay.DisplayedSeconds()-BeforeBoundary+Owner->TerminalReplay.Credit+1./60.-1.6)<1.e-9);
    Owner->SimulationSpeed=8; Owner->ApplyPlaybackRate();
    const double Displayed=Site->SimulationTime;
    Owner->Environment->SetWindSpeedMetersPerSecond(7.f);
    TestEqual(TEXT("External input discards all future"),Owner->TerminalReplay.BufferedSeconds(),0.);
    TestEqual(TEXT("Input retains displayed time"),Site->SimulationTime,Displayed);
    TestEqual(TEXT("Requested speed preserved"),Owner->SimulationSpeed,8.f);
    TestEqual(TEXT("Actual clock switches to 1x"),Owner->GetEffectivePlaybackRate(),1.f);
    Owner->bAutoPaused=true;
    TestFalse(TEXT("Pause returns control to paused live path"),Owner->TerminalReplay.Tick(*Owner,1./60.));
    TestEqual(TEXT("Pause has no retained prediction"),Owner->TerminalReplay.BufferedSeconds(),0.);
    Owner->bAutoPaused=false;
    Initial->Restore();
    auto* Body=Site->Manifest[0].Actor->GetBody();
    Body->SetSimulatePhysics(true); Body->PutAllRigidBodiesToSleep();
    TestFalse(TEXT("Sleeping Chaos body blocks prediction"),FTerminalCheckpoint::CanPredict(*Owner));
    Body->SetSimulatePhysics(false);
    BatchVariable->Set(1,ECVF_SetByCode);
    const double PreviousDrag=Site->STSProfile.Dynamics.WindDrag;
    Site->STSProfile.Dynamics.WindDrag=0;
    ON_SCOPE_EXIT { Site->STSProfile.Dynamics.WindDrag=PreviousDrag; };
    auto* Drop=World->SpawnActor<APortContainerActor>();
    Site->ShipContainers.Add(Drop);
    Drop->LocationOwner=ECargoOwner::Yard;
    auto* DropBody=Drop->GetBody();
    DropBody->SetLinearDamping(0.f); // Analytic free-fall reference, independent of cargo drag tuning.
    auto ResetDrop=[&](double Height=100000.)
    {
        DropBody->SetSimulatePhysics(false);
        Drop->SetActorLocation(FVector(1000000,1000000,Height));
        DropBody->SetSimulatePhysics(true);
        DropBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
        DropBody->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
    };
    ResetDrop();
    Owner->TerminalReplay.Invalidate();
    // Complete a requested number of fixed steps across bounded presentations,
    // including machines where the wall-time budget ends a batch early.
    auto AdvanceSteps=[&](int32 Count,int32 Batch)
    {
        while(Count>0)
        {
            const uint64 Before=Owner->TerminalReplay.DisplayedSteps;
            Owner->TerminalReplay.Credit=FMath::Min(Count,Batch)/60.;
            if(!Owner->TerminalReplay.AdvanceLivePhysics(*Owner)) return false;
            const int32 Completed=int32(Owner->TerminalReplay.DisplayedSteps-Before);
            if(Completed<=0 || Completed>FMath::Min(Count,Batch)) return false;
            Count-=Completed;
        }
        return true;
    };
    TestTrue(TEXT("Supported free cargo advances without checkpoint rewind"),AdvanceSteps(60,16));
    const double FullZ=Drop->GetActorLocation().Z,FullVz=DropBody->GetPhysicsLinearVelocity().Z;
    TestTrue(FString::Printf(TEXT("Batch advances one second of real gravity z=%.6f vz=%.6f gravity=%.6f"),FullZ,FullVz,World->GetGravityZ()),FullZ<99520. && FullZ>99480. && FullVz<-970. && FullVz>-990.);
    auto* Scene=World->GetPhysicsScene();
    Scene->StartFrame(); Scene->WaitPhysScenes(); Scene->EndFrame();
    TestTrue(TEXT("Enclosing zero-time physics frame does not integrate twice"),FMath::Abs(Drop->GetActorLocation().Z-FullZ)<1.e-6);
    ResetDrop();
    TestTrue(TEXT("Split fixed-step physics batch succeeds"),AdvanceSteps(60,4));
    TestTrue(TEXT("Batch partition preserves real physics position"),FMath::Abs(Drop->GetActorLocation().Z-FullZ)<.01);
    TestTrue(TEXT("Batch partition preserves real physics velocity"),FMath::Abs(DropBody->GetPhysicsLinearVelocity().Z-FullVz)<.01);
    auto* Floor=World->SpawnActor<APortContainerActor>();
    Floor->GetBody()->SetSimulatePhysics(false);
    Floor->SetActorLocation(FVector(1000000,1000000,0));
    ResetDrop(1000.);
    TestTrue(TEXT("Contact batch succeeds"),AdvanceSteps(120,16));
    const double ContactZ=Drop->GetActorLocation().Z;
    TestTrue(TEXT("Real collision supports the falling cargo"),ContactZ>250. && ContactZ<270.);
    ResetDrop(1000.);
    for(int32 I=0;I<120;++I)
    {
        Owner->TerminalReplay.Credit=1./60.;
        Owner->TerminalReplay.AdvanceLivePhysics(*Owner);
        Scene->StartFrame(); Scene->WaitPhysScenes(); Scene->EndFrame();
    }
    TestTrue(TEXT("Contact result matches individual fixed physics frames"),FMath::Abs(Drop->GetActorLocation().Z-ContactZ)<.1);
    Floor->Destroy();
    ResetDrop();
    Owner->SimulationSpeed=16; Owner->ApplyPlaybackRate();
    Owner->PlaybackClock->Shutdown(nullptr); Owner->PlaybackClock->Initialize(nullptr);
    Owner->TerminalReplay.Invalidate();
    const double BeforePhysicsDebt=Owner->TerminalReplay.DisplayedSeconds();
    TestTrue(TEXT("Delayed cargo playback uses forward physics"),Owner->TerminalReplay.Tick(*Owner,.25));
    const double Advanced=Owner->TerminalReplay.DisplayedSeconds()-BeforePhysicsDebt;
    TestTrue(TEXT("Physics work is bounded to sixteen fixed steps"),Advanced>0. && Advanced<=16./60.+1.e-9);
    TestTrue(TEXT("Overload drops excess wall-time demand"),
        FMath::Abs(Advanced+Owner->TerminalReplay.Credit-16./60.)<1.e-9);
    TestTrue(TEXT("Next presentation consumes only bounded physics debt"),Owner->TerminalReplay.Tick(*Owner,0.));
    TestTrue(TEXT("No unbounded catch-up survives a stall"),
        Owner->TerminalReplay.DisplayedSeconds()-BeforePhysicsDebt<=16./60.+1.e-9);
    Owner->bAutoPaused=true;
    TestFalse(TEXT("Paused cargo returns to paused world path"),Owner->TerminalReplay.Tick(*Owner,5.));
    TestEqual(TEXT("Paused time is not retained as physics debt"),Owner->TerminalReplay.Credit,0.);
    Owner->bAutoPaused=false;
    Site->ShipContainers.Remove(Drop);
    TestFalse(TEXT("External free bodies exclude terminal physics batching"),Owner->TerminalReplay.AdvanceLivePhysics(*Owner));
    Drop->Destroy();
    Owner->TerminalReplay.Invalidate(); Initial.Reset();
    return true;
}
#endif
