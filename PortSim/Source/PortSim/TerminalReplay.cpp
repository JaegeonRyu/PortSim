#include "TerminalReplay.h"
#include "TerminalCheckpoint.h"
#include "PortContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/PhysicsSettings.h"
#include "Physics/Experimental/PhysScene_Chaos.h"
#include "QuayCrane.h"
#include "PortSiteLogistics.h"
#include "PortSimTimeStep.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "SimulationPerformance.h"
#include "Misc/OutputDeviceRedirector.h"

namespace
{
    TAutoConsoleVariable<int32> BatchPhysics(TEXT("PortSim.BatchLivePhysics"),0,
        TEXT("Batch forward-only terminal cargo physics at fixed 1/60-second steps."));
    bool AdvancingPhysics=false;
    bool Predicting=false; // Game thread only. UObject work never crosses threads.
    TArray<FTerminalReplayLog> PendingLogs;
    TAutoConsoleVariable<int32> Enabled(TEXT("PortSim.BufferedPlayback"),1,
        TEXT("Fixed-step terminal playback with checkpoints and forward-only cargo physics."));
    TAutoConsoleVariable<int32> Window(TEXT("PortSim.ReplayWindowSteps"),16,TEXT("Fixed 1/60-second checkpoint window, 1..60."));
    TAutoConsoleVariable<int32> Budget(TEXT("PortSim.ReplayBudgetSteps"),16,TEXT("Baseline prediction steps per presentation, 1..60; elapsed-time demand may raise this."));
}

bool FTerminalReplay::IsPredicting() { return Predicting; }
bool FTerminalReplay::IsAdvancingPhysics() { return AdvancingPhysics; }
void FTerminalReplay::Log(FName Category,ELogVerbosity::Type Verbosity,FString Message)
{
    if(Predicting) PendingLogs.Add({Category,Verbosity,MoveTemp(Message)});
    else if(GLog) GLog->Serialize(*Message,Verbosity,Category);
}
double FTerminalReplay::BufferedSeconds() const
{ int32 Steps=0; for(int32 N:FutureSteps) Steps+=N; return Steps/60.; }
void FTerminalReplay::Invalidate()
{
    Future.Reset(); FutureSteps.Reset(); FutureLogs.Reset(); Current.Reset(); Credit=0; bBoundary=false; ++Revision; State=TEXT("live_input_hold");
}

// Same model ordering as the live unified-terminal branch, with fixed dt and
// no camera/input or world timers. The caller supplies physics and publication.
void AQuayCrane::AdvanceTerminalReplayStep(float Dt)
{
    const bool Frozen=bAutoPaused || bEmergencyStop || !bAutoRunning || !SiteLogistics->Fault.IsEmpty();
    if(!Frozen) Environment->AdvanceEnvironment(Dt);
    if(bAutoRunning) WeatherHold.Update(*Environment,Frozen?0.f:Dt);
    else WeatherHold=FPortWeatherHold();
    if(!Frozen && !WeatherHold.Active) ApplySeaMotion();
    SiteLogistics->SetWindVelocity(Environment->GetEffectiveWindVelocity());
    SiteLogistics->SetFogVisibilityMeters(Environment->IsFogEnabled()?Environment->GetFogVisibilityMeters():MAX_flt);
    SiteLogistics->SetRainState(Environment->GetRainState());
    SiteLogistics->SetSurfaceConditions(Environment->GetSurfaceConditions(),Environment->GetFrictionSettings(),Environment->GetContactMaterial());
    STSSimulationTime+=Dt;
    SampleSTSSensors();
    SiteLogistics->BeginTrafficFrame();
    if(bAutoRunning && !IsOperationPaused() && !bEmergencyStop && SiteLogistics->Fault.IsEmpty()) AutoElapsed+=Dt;
    SiteLogistics->Advance(Dt,IsOperationPaused() || bEmergencyStop || !bAutoRunning);
    AutoCompleted=Deliveries=SiteLogistics->Delivered;
    if(bAutoRunning && SiteLogistics->ShipRemaining()==0 && SiteLogistics->IsIdle())
    {
        bAutoRunning=false;
        Status=FString::Printf(TEXT("All configured ships unloaded: %d containers, %.1f simulation seconds"),Deliveries,AutoElapsed);
    }
}

bool FTerminalReplay::AdvanceLivePhysics(AQuayCrane& Owner)
{
    if(!BatchPhysics.GetValueOnGameThread() && !FParse::Param(FCommandLine::Get(),TEXT("PortSimBatchLivePhysics"))) return false;
    auto* World=Owner.GetWorld();
    auto* Scene=World?World->GetPhysicsScene():nullptr;
    const auto* Settings=UPhysicsSettings::Get();
    if(!Scene || World->GetNetMode()!=NM_Standalone || Settings->bTickPhysicsAsync ||
        Settings->PhysicsPrediction.bEnablePhysicsPrediction || !Scene->IsCompletionEventComplete()) return false;
    // Only this terminal's cargo may be dynamic. External actors require their
    // own engine ticks and callbacks and retain the standard world path.
    TSet<const AActor*> CargoActors;
    for(const auto& Cargo:Owner.SiteLogistics->ShipContainers) if(IsValid(Cargo)) CargoActors.Add(Cargo);
    for(TActorIterator<AActor> It(World);It;++It)
    {
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for(auto* Part:Parts) if(Part->IsAnySimulatingPhysics() &&
            (!CargoActors.Contains(*It) || !Part->IsA<UStaticMeshComponent>())) return false;
    }
    PortSimPerformance::FScope Timing(TEXT("live_physics_batch"));
    const FVector Gravity(0,0,World->GetGravityZ());
    // The enclosing world's physics tick must not integrate the same time again.
    ON_SCOPE_EXIT { Scene->SetUpForFrame(&Gravity,0.f,0.f,0.f,Settings->MaxSubstepDeltaTime,Settings->MaxSubsteps,false); };
    Owner.PlaybackClock->SetBufferedPlayback(true);
    State=TEXT("live_physics_batch");
    const int32 Steps=static_cast<int32>(FMath::Clamp(FMath::FloorToDouble((Credit+1.e-9)*60.),0.,120.));
    const uint64 InputRevision=Revision;
    {
        TGuardValue<bool> Guard(AdvancingPhysics,true);
        for(int32 I=0;I<Steps;++I)
        {
            Owner.AdvanceTerminalReplayStep(1.f/60.f);
            Scene->SetUpForFrame(&Gravity,1.f/60.f,Settings->MinPhysicsDeltaTime,
                Settings->MaxPhysicsDeltaTime,Settings->MaxSubstepDeltaTime,Settings->MaxSubsteps,Settings->bSubstepping);
            Scene->StartFrame();
            Scene->WaitPhysScenes();
            Scene->EndFrame(); // Publish real contact/velocity results before the next controller observation.
            ++DisplayedSteps;
            if(Revision!=InputRevision) break;
            Credit-=UPortSimTimeStep::StepSeconds;
            if(!Owner.bAutoRunning || Owner.bAutoPaused || Owner.bEmergencyStop || !Owner.SiteLogistics->Fault.IsEmpty()) break;
        }
    }
    Owner.SiteLogistics->ExportDashboard(Owner.IsOperationPaused());
    PortSimPerformance::Report(Owner.SiteLogistics->SimulationTime,Owner.SiteLogistics->Delivered,World);
    return true;
}

bool FTerminalReplay::Tick(AQuayCrane& Owner,double WallSeconds)
{
    auto* Clock=Owner.PlaybackClock.Get();
    auto* Site=Owner.SiteLogistics.Get();
    if(!Clock || !Site) return false;
    const double Now=FPlatformTime::Seconds();
    if(SampleWall==0 || Site->SimulationTime<SampleSimulation)
    { SampleWall=Now; SampleSimulation=Site->SimulationTime; ActualRate=0; }
    else if(Now-SampleWall>=.5)
    {
        ActualRate=float((Site->SimulationTime-SampleSimulation)/(Now-SampleWall));
        SampleWall=Now; SampleSimulation=Site->SimulationTime;
    }
    Clock->SetBufferedPlayback(false);
    Clock->SetLiveFallback(false);
    Site->ProcessDashboardCommands(); // Accepted commands invalidate before future use.
    // Test policies must be installed before calculating the first future.
    const bool Full=FParse::Param(FCommandLine::Get(),TEXT("PortSimFullUnloadTest"));
    if(Full || FParse::Param(FCommandLine::Get(),TEXT("PortSimSiteTest")))
    {
        int32 Limit=Site->Vehicles.Num()*2;
        FParse::Value(FCommandLine::Get(),TEXT("PortSimSiteShipmentLimit="),Limit);
        Site->DispatchLimit=(Full || FParse::Param(FCommandLine::Get(),TEXT("PortSimContinuousTrafficTest")))?
            Site->InitialShipCount():FMath::Clamp(Limit,9,Site->InitialShipCount());
    }
    if(FParse::Param(FCommandLine::Get(),TEXT("PortSimFirstWaveTest")))
    {
        int32 Limit=9; FParse::Value(FCommandLine::Get(),TEXT("PortSimWaveShipmentCount="),Limit);
        Site->DispatchLimit=FMath::Clamp(Limit,1,Site->InitialShipCount());
    }
    else if(FParse::Param(FCommandLine::Get(),TEXT("PortSimCarLikeTest"))) Site->DispatchLimit=1;
    if(!Enabled.GetValueOnGameThread() || FParse::Param(FCommandLine::Get(),TEXT("PortSimNoReplay")) ||
        !Owner.bAutoRunning || Owner.bAutoPaused || Owner.bEmergencyStop || !Site->Fault.IsEmpty() ||
        Clock->GetEffectiveRate()<=1.f)
    {
        Future.Reset(); FutureSteps.Reset(); FutureLogs.Reset(); Current.Reset(); Credit=0; bBoundary=false;
        State=Clock->GetEffectiveRate()<Owner.SimulationSpeed?TEXT("live_input_hold"):TEXT("live");
        return false;
    }
    if(FMath::IsFinite(WallSeconds) && WallSeconds>0.) Credit+=WallSeconds*Owner.SimulationSpeed;
    if(!FTerminalCheckpoint::CanPredict(Owner) || (bBoundary && Future.IsEmpty()))
    {
        Future.Reset(); FutureSteps.Reset(); FutureLogs.Reset(); Current.Reset(); bBoundary=false; State=TEXT("live_chaos");
        if(AdvanceLivePhysics(Owner)) return true;
        Credit-=UPortSimTimeStep::StepSeconds; // This frame will advance once through the live world.
        return false;
    }
    Clock->SetBufferedPlayback(true);
    if(!Current) Current=FTerminalCheckpoint::Capture(Owner);
    auto Displayed=Current;
    const FString CommittedResults=Site->ResultsCsv;
    if(!Displayed) { Invalidate(); Clock->SetLiveFallback(true); return false; }
    const int32 StepsPerPicture=FMath::Clamp(FMath::RoundToInt(Owner.SimulationSpeed),1,16);
    // Keep elapsed playback time even when a frame is late or the buffer runs dry.
    // Bound work per presentation (not elapsed time) so input remains serviceable.
    const int32 DueSteps=static_cast<int32>(FMath::Clamp(FMath::FloorToDouble((Credit+1.e-9)*60.),0.,960.));
    const int32 Reserve=FMath::Max(StepsPerPicture*2,FMath::Clamp(Window.GetValueOnGameThread(),1,60));
    const int32 Target=FMath::Max(Reserve,DueSteps+StepsPerPicture);
    int32 Available=FMath::RoundToInt(BufferedSeconds()*60.);
    bool Predicted=false,RestoreFailed=false;
    if(!bBoundary && Available<Target)
    {
        Predicting=true;
        ON_SCOPE_EXIT { PendingLogs.Reset(); Predicting=false; };
        Predicted=true;
        if(!Future.IsEmpty() && !Future.Last()->Restore())
        { Future.Reset(); FutureSteps.Reset(); FutureLogs.Reset(); bBoundary=true; }
        else
        {
            const int32 Count=FMath::Min(Target-Available,FMath::Max(DueSteps+StepsPerPicture,FMath::Max(StepsPerPicture,FMath::Clamp(Budget.GetValueOnGameThread(),1,60))));
            for(int32 I=0;I<Count;)
            {
                PendingLogs.Reset();
                // Intermediate pictures already due for this presentation will never
                // be shown. Keep their fixed model steps and logs, but capture
                // only the due endpoint and the small future reserve.
                const int32 Batch=FMath::Min(FMath::Max(StepsPerPicture,DueSteps-Available),Count-I);
                const auto BatchStart=Future.IsEmpty()?Displayed:Future.Last();
                int32 SafeSteps=0;
                for(int32 J=0;J<Batch;++J)
                {
                    Owner.AdvanceTerminalReplayStep(1.f/60.f); ++I;
                    if(!FTerminalCheckpoint::CanPredict(Owner)) { bBoundary=true; break; }
                    ++SafeSteps;
                }
                if(bBoundary)
                {
                    // Retain the safe prefix of a coalesced batch. Chaos itself
                    // is never rewound/replayed; the released body has not yet
                    // received an engine physics tick inside this transaction.
                    if(SafeSteps>0)
                    {
                        if(!BatchStart->Restore()) { RestoreFailed=true; break; }
                        PendingLogs.Reset();
                        for(int32 J=0;J<SafeSteps;++J) Owner.AdvanceTerminalReplayStep(1.f/60.f);
                        auto Safe=FTerminalCheckpoint::Capture(Owner);
                        if(Safe)
                        {
                            Future.Add(MoveTemp(Safe)); FutureSteps.Add(SafeSteps); Available+=SafeSteps;
                            FutureLogs.Add(MoveTemp(PendingLogs));
                        }
                    }
                    break;
                }
                auto Next=FTerminalCheckpoint::Capture(Owner);
                if(!Next) { bBoundary=true; break; }
                Future.Add(MoveTemp(Next)); FutureSteps.Add(Batch); Available+=Batch;
                FutureLogs.Add(MoveTemp(PendingLogs));
                if(!Owner.bAutoRunning || !Site->Fault.IsEmpty()) { bBoundary=true; break; }
            }
        }
    }

    if(RestoreFailed)
    { Invalidate(); Site->Stop(TEXT("Checkpoint object lifecycle changed during prediction")); return true; }
    if(Future.IsEmpty())
    {
        if(Predicted && !Displayed->Restore())
        { Invalidate(); Site->Stop(TEXT("Checkpoint object lifecycle changed during prediction")); return true; }
        // A release happened on the first predicted step. The displayed
        // checkpoint has already been restored; execute that step live once.
        bBoundary=false; State=TEXT("live_chaos"); Clock->SetBufferedPlayback(false);
        Current.Reset(); Credit-=UPortSimTimeStep::StepSeconds;
        return false;
    }
    if(DueSteps==0 && Available<Reserve && !bBoundary)
    {
        if(Predicted && !Displayed->Restore())
        { Invalidate(); Site->Stop(TEXT("Checkpoint object lifecycle changed during prediction")); return true; }
        State=TEXT("preparing");
    }
    else
    {
        State=TEXT("replay");
        const int32 Wanted=DueSteps;
        int32 Take=0,Consumed=0;
        while(Take<FutureSteps.Num() && Consumed+FutureSteps[Take]<=Wanted)
        { Consumed+=FutureSteps[Take]; ++Take; }
        if(Take>0)
        {
            const FString PreviousResults=CommittedResults;
            if(!Future[Take-1]->Restore()) { Invalidate(); Site->Stop(TEXT("Replay checkpoint expired")); return true; }
            Current=Future[Take-1];
            for(int32 I=0;I<Take;++I) for(auto& Entry:FutureLogs[I])
                Log(Entry.Category,Entry.Verbosity,MoveTemp(Entry.Message));
            FutureLogs.RemoveAt(0,Take);
            Future.RemoveAt(0,Take); FutureSteps.RemoveAt(0,Take); DisplayedSteps+=Consumed; Credit-=Consumed/60.;
            if(Site->ResultsCsv!=PreviousResults && !Site->SaveReports()) Site->Stop(TEXT("Committed replay report failed"));
        }
        if(Take==0 && Predicted && !Displayed->Restore())
        { Invalidate(); Site->Stop(TEXT("Checkpoint object lifecycle changed during prediction")); return true; }
        if(Wanted>Consumed && Future.IsEmpty()) { State=TEXT("preparing"); }
    }
    // These outputs are now derived exclusively from the displayed checkpoint.
    Site->ExportDashboard(Owner.IsOperationPaused());
    PortSimPerformance::Report(Site->SimulationTime,Site->Delivered,Owner.GetWorld());
    return true;
}
