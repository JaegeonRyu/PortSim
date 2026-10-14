#include "QuayCrane.h"
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "PortAGVActor.h"
#include "Misc/Paths.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void AQuayCrane::BeginPickupTest()
{
    bPickupTest=true;PickupTestCase=TEXT("offset");
    FParse::Value(FCommandLine::Get(),TEXT("PortSimPickupCase="),PickupTestCase);
    if(!STSProfile.bReady){UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: invalid profile"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    const bool RMG=FParse::Param(FCommandLine::Get(),TEXT("PortSimRMGTest"));
    auto Profile=STSProfile;
    if(RMG && !Profile.LoadRMG(FPaths::ProjectConfigDir()/TEXT("RMG_Simulation.json")))
    {UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: %s"),*Profile.Error);FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(PickupTestCase==TEXT("bias"))Profile.Pickup.PoseBias.X=15;
    const FVector Home(500000,0,0), Source=Home+(RMG?FVector(-1000,0,349.5):FVector(-1600,0,PickupTestCase==TEXT("wave")?803.15:1700)), Destination=Home+(RMG?FVector(1000,2200,149.5):FVector(3000,0,149.5));
    FActorSpawnParameters Spawn;Spawn.Owner=this;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    PickupTestCrane=GetWorld()->SpawnActor<APortWorkingCrane>(Home,FRotator::ZeroRotator,Spawn);
    WorkingCranes.Add(PickupTestCrane);PickupTestCrane->SetSTSProfile(Profile);
    PickupTestCrane->Configure(1,!RMG,Source,Destination,false);
    if(PickupTestCase==TEXT("wave")) PickupTestCrane->SetWindVelocity(FVector(3.5,0,0));
    const FRotator Rotation(0,PickupTestCase==TEXT("yaw")?1.5:0,0);
    auto* Box=GetWorld()->SpawnActor<APortContainerActor>(Source+FVector(30,-20,0),Rotation,Spawn);
    Box->InitializeContainer(9000);ContainerActors.Add(Box);
    Box->SetPhysicalParameters(PickupTestCase==TEXT("eccentric")?24000:12000,PickupTestCase==TEXT("eccentric")?FVector(25,-80,0):FVector::ZeroVector);
    if(!Box->GetBody()->GetCenterOfMass().Equals(Box->GetActorLocation()+Box->GetActorQuat().RotateVector(Box->CoGOffsetCm),.1))
    {UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: physical CoG does not match centimetre input"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    Box->GetBody()->SetSimulatePhysics(false);Box->LocationOwner=ECargoOwner::Ship;
    APortAGVActor* Vehicle=nullptr;
    if(RMG)
    {
        Vehicle=GetWorld()->SpawnActor<APortAGVActor>(Source-FVector(0,0,349.5),FRotator::ZeroRotator,Spawn);
        Vehicle->InitializeVehicle(9000);AGVActors.Add(Vehicle);
        // Reproduce a real ship -> AGV handover with the reported stale vessel samples.
        Box->SetSecuredVesselMotion(Box->GetActorLocation(),Box->GetActorQuat(),
            FVector(5.41,-2.30,8.99),FVector(-.39,.19,-3.13),FVector(-.94,.58,.56));
        Box->AttachToComponent(Vehicle->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);Box->LocationOwner=ECargoOwner::AGV;
        if(!Box->GetMotionVelocity().IsNearlyZero() || !Box->GetMotionAcceleration().IsNearlyZero() ||
            !Box->GetMotionJerk().IsNearlyZero())
        {UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: stopped AGV inherited vessel motion"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Vehicle->Speed=100;
        if(!Box->GetMotionVelocity().Equals(Vehicle->GetActorRightVector()*100,.01))
        {UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: cargo did not follow AGV velocity"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Vehicle->Sensors.bReversing=true;
        if(!Box->GetMotionVelocity().Equals(-Vehicle->GetActorRightVector()*100,.01))
        {UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: cargo did not follow reversing AGV"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
        Vehicle->Speed=0;Vehicle->Sensors.bReversing=false;
        if(PickupTestCase==TEXT("agv"))Vehicle->Speed=100;
        if(PickupTestCase==TEXT("collision"))
        {
            auto* Other=GetWorld()->SpawnActor<APortWorkingCrane>(Home+FVector(0,2100,0),FRotator::ZeroRotator,Spawn);
            WorkingCranes.Add(Other);Other->Configure(2,false,Home+FVector(-1000,2100,349.5),Home+FVector(1000,2200,149.5),false);
        }
    }
    if(!PickupTestCrane->AssignCargo(Box,Source,Destination,false,RMG,Vehicle))
    {UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: assignment rejected"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(!RMG)PickupTestCrane->SetDestinationReady(false);
}

void AQuayCrane::TickPickupTest(float Dt)
{
    if(!PickupTestCrane)return;
    FParse::Value(FCommandLine::Get(),TEXT("PortSimPickupFrameSeconds="),Dt);
    if(!FMath::IsFinite(Dt)||Dt<=0||Dt>2)
    {UE_LOG(LogTemp,Error,TEXT("PORTSIM_PICKUP_FAIL: invalid frame interval"));FPlatformMisc::RequestExitWithStatus(false,1);return;}
    if(PickupTestSeconds==0)UE_LOG(LogTemp,Display,TEXT("PICKUP_TEST_STEP: %.6f simulation seconds per render frame"),Dt);
    PickupTestSeconds+=Dt;auto* C=PickupTestCrane.Get();
    if(PickupTestCase==TEXT("wave") && !C->bCarrying && C->Stage<=2)
    {
        // Analytic vessel translation: heave reversals plus horizontal surge.
        // Keep the real contact, dwell, lock and trial-lift checks enabled.
        const double W=2.*PI/7., T=PickupTestSeconds;
        const FVector Amplitude(4,2,8);
        const FVector Position=FVector(498430,-20,803.15)+Amplitude*FMath::Sin(W*T);
        auto* WaveCargo=C->CargoActor.Get();
        WaveCargo->SetSecuredVesselMotion(Position,FRotator(0,0,.235*FMath::Sin(W*T)).Quaternion(),
            Amplitude*(W*FMath::Cos(W*T)),Amplitude*(-W*W*FMath::Sin(W*T)),
            Amplitude*(-W*W*W*FMath::Cos(W*T)),Dt);
        C->UpdateMovingShipSource(WaveCargo,Position);
    }
    const bool WasAttached=C->bCarrying;
    C->Advance(Dt,false);
    auto Finish=[&](bool Pass,const FString& Why)
    {
        UE_LOG(LogTemp,Display,TEXT("PORTSIM_PICKUP_%s: %s: %s (%.2f simulated seconds)"),Pass?TEXT("PASS"):TEXT("FAIL"),*PickupTestCase,*Why,PickupTestSeconds);
        FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
    };
    const bool RMG=!C->bSTS;
    const bool Negative=PickupTestCase!=TEXT("offset")&&PickupTestCase!=TEXT("eccentric")&&PickupTestCase!=TEXT("wave");
    // Attachment's instantaneous position is checked in the runtime. The cargo
    // may legitimately move during later control steps of the same render frame.
    if(!WasAttached&&C->bCarrying && !C->Observation.AllLocked())
    {Finish(false,TEXT("Attachment bypassed lock feedback"));return;}
    if(Negative&&PickupTestCase!=TEXT("stack")&&C->bCarrying){Finish(false,TEXT("Invalid pickup attached cargo"));return;}
    bPickupSawTrial |= C->bCarrying&&C->Stage==2;
    if(!C->Fault.IsEmpty())
    {
        const FString Expected=PickupTestCase==TEXT("lock")?TEXT("Twist lock alignment failed"):
            PickupTestCase==TEXT("sensor")?TEXT("Required RMG sensor observation invalid/stale"):
            PickupTestCase==TEXT("agv")?TEXT("AGV alignment recovery timed out during RMG pickup"):
            PickupTestCase==TEXT("collision")?TEXT("RMG crane collision clearance violated"):
            PickupTestCase==TEXT("stack")?TEXT("RMG stack profile invalid or destination height mismatch"):TEXT("Pickup alignment attempts exhausted");
        if(PickupTestCase==TEXT("stack") && (!C->bCarrying||C->CompletedJobs!=0)){Finish(false,TEXT("Stack fault released cargo"));return;}
        Finish(Negative&&C->Fault==Expected,C->Fault);return;
    }
    if((!RMG && C->Stage>=3) || (RMG && C->CompletedJobs==1))
    {
        const auto Pick=C->DashboardState()->GetObjectField(TEXT("pickup"));
        const double Estimated=Pick->GetNumberField(TEXT("estimated_mass_kg"));
        const auto CoG=Pick->GetArrayField(TEXT("estimated_cog_m"));
        const FVector MeasuredCoG(CoG[0]->AsNumber()*100,CoG[1]->AsNumber()*100,0);
        const auto* Payload=RMG?ContainerActors.Last().Get():C->CargoActor.Get();
        const bool Placed=!RMG || (!C->bCarrying && Payload->LocationOwner==ECargoOwner::Yard && Payload->GetActorLocation().Equals(FVector(501000,2200,149.5),10));
        const bool Pass=Placed&&!Negative&&bPickupSawTrial&&Pick->GetBoolField(TEXT("verified"))&&
            FMath::Abs(Estimated-Payload->MassKg)<50&&MeasuredCoG.Equals(Payload->CoGOffsetCm,3);
        Finish(Pass,FString::Printf(TEXT("measured mass %.2f kg, CoG %s; offset corrected and trial lift verified; RMG cases also verify final placement"),Estimated,*MeasuredCoG.ToCompactString()));return;
    }
    if(PickupTestSeconds>(RMG?750:350))Finish(false,TEXT("Scenario timed out"));
}
