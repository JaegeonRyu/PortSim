#include "TerminalReplay.h"
#include "TerminalCheckpoint.h"
#include "PortContainerActor.h"
#include "PortSupportVehicle.h"
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
    TAutoConsoleVariable<int32> BatchPhysics(TEXT("PortSim.BatchLivePhysics"),1,
        TEXT("Batch forward-only terminal cargo physics at fixed 1/60-second steps."));
    TAutoConsoleVariable<int32> LivePhysicsSteps(TEXT("PortSim.LivePhysicsMaxSteps"),16,
        TEXT("Hard limit on live cargo physics steps per presentation (1..16)."));
    TAutoConsoleVariable<float> LivePhysicsBudget(TEXT("PortSim.LivePhysicsBudgetMs"),8.f,
        TEXT("Wall-time budget for live cargo physics per presentation (1..33 ms); one step always completes."));
    bool AdvancingPhysics=false;
    bool Predicting=false; // Game thread only. UObject work never crosses threads.
    TArray<FTerminalReplayLog> PendingLogs;
    TAutoConsoleVariable<int32> Enabled(TEXT("PortSim.BufferedPlayback"),1,
        TEXT("Forward-only fixed-step terminal playback."));
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
    // Terminal cargo and explicitly managed R/S bodies may be dynamic. External actors require their
    // own engine ticks and callbacks and retain the standard world path.
    TSet<const AActor*> CargoActors;
    for(const auto& Cargo:Owner.SiteLogistics->ShipContainers) if(IsValid(Cargo)) CargoActors.Add(Cargo);
    for(TActorIterator<AActor> It(World);It;++It)
    {
        const auto* RS=Cast<APortSupportVehicle>(*It);
        const bool ManagedRS=RS && Owner.SupportFleet.ContainsByPredicate([&](const auto& A){return A.Get()==RS;});
        TInlineComponentArray<UPrimitiveComponent*> Parts(*It);
        for(auto* Part:Parts) if(Part->IsAnySimulatingPhysics() &&
            (!CargoActors.Contains(*It) || !Part->IsA<UStaticMeshComponent>()) &&
            !(ManagedRS && RS->OwnsRecoveryPhysicsBody(Part))) return false;
    }
    PortSimPerformance::FScope Timing(TEXT("live_physics_batch"));
    const FVector Gravity(0,0,World->GetGravityZ());
    // The enclosing world's physics tick must not integrate the same time again.
    ON_SCOPE_EXIT { Scene->SetUpForFrame(&Gravity,0.f,0.f,0.f,Settings->MaxSubstepDeltaTime,Settings->MaxSubsteps,false); };
    Owner.PlaybackClock->SetBufferedPlayback(true);
    State=TEXT("live_physics_batch");
    const int32 MaxSteps=FMath::Clamp(LivePhysicsSteps.GetValueOnGameThread(),1,16);
    const float RequestedBudget=LivePhysicsBudget.GetValueOnGameThread();
    const double BudgetSeconds=(FMath::IsFinite(RequestedBudget)?FMath::Clamp(RequestedBudget,1.f,33.f):8.f)/1000.;
    // Drop excess wall-clock demand, never simulation steps. A slow machine
    // runs below the requested rate instead of building an unbounded catch-up queue.
    Credit=FMath::Clamp(Credit,0.,MaxSteps*UPortSimTimeStep::StepSeconds);
    const int32 Steps=FMath::Min(MaxSteps,FMath::FloorToInt((Credit+1.e-9)/UPortSimTimeStep::StepSeconds));
    const double Started=FPlatformTime::Seconds();
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
            if(FPlatformTime::Seconds()-Started>=BudgetSeconds) break;
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
    if(FMath::IsFinite(WallSeconds) && WallSeconds>0.) Credit+=FMath::Min(WallSeconds,1.)*Clock->GetEffectiveRate();
    // All accelerated steps share one forward solver and bounded demand policy.
    if(AdvanceLivePhysics(Owner)) return true;
    Credit=0;
    Clock->SetLiveFallback(true);
    State=TEXT("live_external_physics");
    return false;
}