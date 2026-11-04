#if WITH_DEV_AUTOMATION_TESTS
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "PortAGVActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRMGAGVHandoverTest,"PortSim.RMG.AGVHandover",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FRMGAGVHandoverTest::RunTest(const FString& Parameters)
{
    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    ON_SCOPE_EXIT { World->DestroyWorld(false); };
    auto* Vehicle=World->SpawnActor<APortAGVActor>();
    Vehicle->Sensors.bFMSConnected=Vehicle->Sensors.bDockingNode=Vehicle->Sensors.bTransponderLocked=true;
    auto* Crane=World->SpawnActor<APortWorkingCrane>();
    const FVector Landing=Vehicle->CargoPosition();
    Crane->Configure(1,false,Landing,Landing,false);
    for(const auto& Pad:Crane->Pads) Pad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    auto* Cargo=World->SpawnActor<APortContainerActor>();
    Cargo->GetBody()->SetSimulatePhysics(false);
    Cargo->SetActorLocation(Landing);
    Cargo->AttachToComponent(Crane->Spreader,FAttachmentTransformRules::KeepWorldTransform);
    Crane->CargoActor=Cargo; Crane->HandoverAGV=Vehicle;
    Crane->bReverseTransfer=true; Crane->bCarrying=true; Crane->bJobActive=true;
    Crane->Stage=5; Crane->SourceSlot=0;
    auto& O=Crane->Observation;
    O.bValid=true; O.Timestamp=Crane->SimulationTime;
    O.bAGVAligned=O.bCargoSupported=O.bLanded=true;
    O.SpreaderVelocity=FVector::ZeroVector;
    for(int32 I=0;I<4;++I) { O.Locked[I]=true; Crane->CornerLocked[I]=true; }
    O.bCargoSupported=false;
    TestFalse(TEXT("No support confirmation keeps crane locks engaged"),Crane->SecureRMGToAGV());
    O.bCargoSupported=true; O.bLanded=false;
    TestFalse(TEXT("Unseated cargo cannot transfer"),Crane->SecureRMGToAGV());
    O.bLanded=true; O.Locked[2]=false;
    TestFalse(TEXT("Missing lock confirmation blocks transfer"),Crane->SecureRMGToAGV());
    O.Locked[2]=true; Vehicle->Speed=20;
    TestFalse(TEXT("Moving AGV cannot receive cargo"),Crane->SecureRMGToAGV());
    Vehicle->Speed=0;
    Vehicle->SetActorLocation(FVector(5000,0,0));
    TestFalse(TEXT("Stale support sample cannot replace physical AGV support"),Crane->SecureRMGToAGV());
    Vehicle->SetActorLocation(FVector::ZeroVector);
    TestTrue(TEXT("Rejected transfers retain spreader attachment"),Cargo->GetAttachParentActor()==Crane);
    if(!TestTrue(TEXT("Seated supported load secures to AGV"),Crane->SecureRMGToAGV())) return false;
    TestTrue(TEXT("Receiving restraint is attached with physics disabled"),
        Cargo->GetAttachParentActor()==Vehicle && !Cargo->GetBody()->IsSimulatingPhysics());
    TestEqual(TEXT("Ownership transfers before empty hoist"),Cargo->LocationOwner,ECargoOwner::AGV);
    TestEqual(TEXT("Payload transfers before AGV departure"),Vehicle->PayloadKg,Cargo->MassKg);
    // Continue the empty-hoist phase after the vehicle has left the dock.
    Crane->bCarrying=false; Crane->Stage=6;
    for(bool& Lock:Crane->CornerLocked) Lock=false;
    Crane->Head=Crane->Local(Landing)+FVector(0,0,154.5);
    Crane->StageHoldTarget=Crane->Head;
    Crane->UpdateParts();
    const double HeadBefore=Crane->HeadPosition().Z;
    Vehicle->SetActorLocation(FVector(5000,0,0)); Vehicle->Speed=100;
    const FVector CargoBefore=Cargo->GetActorLocation();
    for(int32 I=0;I<30;++I) Crane->Advance(1.f/60.f,false);
    TestTrue(TEXT("Departure does not stop empty hoist for alignment"),Crane->Fault.IsEmpty() && Crane->AGVAlignmentWait==0);
    TestTrue(TEXT("Empty spreader keeps rising after AGV departure"),Crane->HeadPosition().Z>HeadBefore);
    TestTrue(TEXT("Empty hoist cannot move the transferred load"),Cargo->GetActorLocation().Equals(CargoBefore,.001));
    Crane->Stage=7; Crane->SettleTime=0;
    Crane->Advance(1.f/60.f,false);
    TestFalse(TEXT("RMG finishes independently of departed cargo position"),Crane->IsBusy());
    TestTrue(TEXT("Completion does not reject moving AGV cargo"),Crane->Fault.IsEmpty());
    return true;
}
#endif
