#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "PortSiteLogistics.h"
#include "Components/StaticMeshComponent.h"

bool APortWorkingCrane::CanRecoverPickup() const
{
    // Only a verified, still-secured ship box may be abandoned/reacquired.
    // Partial seating locks are opened below, before any retreat motion.
    return bSTS && !bReverseTransfer && bExternalJobs && bJobActive && Stage==2 && !bCarrying &&
        STSProfile.Pickup.RecoveryMaxCycles>0 && IsValid(CargoActor) &&
        Cast<APortSiteLogistics>(CargoActor->GetOwner()) && CargoActor->LocationOwner==ECargoOwner::Ship &&
        !CargoActor->GetAttachParentActor() && !CargoActor->GetBody()->IsSimulatingPhysics() &&
        Observation.IsFresh(SimulationTime,STSProfile.SensorMaxAge) && Observation.DynamicPayloadEstimateKg()<100 &&
        Observation.SpreaderPosition.Z-Observation.CargoPosition.Z>=154.5-2*STSProfile.Pickup.VerticalTolerance;
}

bool APortWorkingCrane::StartPickupRecovery()
{
    if(Pickup.Failure!=ESTSPickupFailure::AlignmentExhausted || !CanRecoverPickup()) return false;
    PickupRecoveryReason=Pickup.Fault;
    bIsolateAfterPickupRetreat=PickupRecoveryCount>=STSProfile.Pickup.RecoveryMaxCycles;
    if(!bIsolateAfterPickupRetreat) ++PickupRecoveryCount;
    ++TotalPickupRecoveryEvents;
    PickupRecoveryStep=1; PickupRecoverySeconds=PickupRecoveryStableSeconds=0;
    PickupRecoveryLastSample=Observation.Timestamp;
    PickupRecoveryStartGap=Observation.SpreaderPosition.Z-Observation.CargoPosition.Z;
    PickupRecoveryPhase=TEXT("unlocking"); RecoveryStatus=TEXT("pickup_unlocking");
    for(int32 I=0;I<4;++I)
    {Pickup.RequestLocks[I]=false;LockProgress[I]=FMath::Min(LockProgress[I],STSProfile.Pickup.LockTime);}
    UE_LOG(LogTemp,Warning,TEXT("STS_PICKUP_RECOVERY: crane=%d cycle=%d isolate_after_clearance=%d cargo=%s pose_unchanged=1"),
        CraneID,PickupRecoveryCount,bIsolateAfterPickupRetreat,*CargoActor->ContainerID.ToString());
    return true;
}

void APortWorkingCrane::AdvancePickupRecovery(float Dt)
{
    if(!CanRecoverPickup()) { Stop(TEXT("Pickup recovery lost verified secured/support state")); return; }
    const auto& C=STSProfile.Pickup;
    const double Credit=Observation.Timestamp>PickupRecoveryLastSample?
        FMath::Clamp(Observation.Timestamp-PickupRecoveryLastSample,0.,double(STSProfile.SensorMaxAge)):0.;
    PickupRecoveryLastSample=Observation.Timestamp;
    PickupRecoverySeconds+=Dt;
    if(PickupRecoverySeconds>C.RecoveryTimeout)
    { Stop(TEXT("Pickup recovery failed to reach verified unlocked clearance"));return; }
    if(PickupRecoveryStep==1)
    {
        // Model lock opening with its actuator dwell, rather than clearing
        // loaded/locked flags and lifting through the still-seated corners.
        for(int32 I=0;I<4;++I)
        {
            LockProgress[I]=FMath::Max(0.,LockProgress[I]-Dt);
            CornerLocked[I]=LockProgress[I]>0;
        }
        MoveSTS(Local(Observation.CargoPosition)+FVector(0,0,PickupRecoveryStartGap),Dt);
        bool Open=PickupRecoverySeconds>=C.LockTime;
        for(int32 I=0;I<4;++I) Open &= LockProgress[I]==0 && !Observation.Locked[I];
        if(Open)
        {
            PickupRecoveryStep=2; PickupRecoverySeconds=0;
            PickupRecoveryPhase=TEXT("retreating");RecoveryStatus=TEXT("pickup_retreating");
        }
        return;
    }
    const double TiltSpan=FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Observation.CargoPitchRoll.X)))*Observation.CornerHalfWidthCm+
        FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Observation.CargoPitchRoll.Y)))*Observation.CornerHalfLengthCm;
    const FVector Target=Local(Observation.CargoPosition)+FVector(0,0,154.5+C.RecoveryClearance+2*C.VerticalTolerance+2*TiltSpan);
    if(Target.Z>SafeZ) {Stop(TEXT("Pickup recovery clearance exceeds reserved safe height"));return;}
    MoveSTS(Target,Dt);
    if(!Fault.IsEmpty()) return;
    bool Clear=true;
    for(int32 I=0;I<4;++I) Clear &= !Observation.Locked[I] && Observation.CornerError[I].Z>=C.RecoveryClearance;
    // Verify the retreat using fresh relative motion, not an elapsed timer alone.
    Clear &= Observation.IsFresh(SimulationTime,STSProfile.SensorMaxAge) &&
        C.PickupSpeedWithin(Observation.SpreaderVelocity-Observation.CargoVelocity);
    const bool WasStable=PickupRecoveryPhase==TEXT("stabilizing");
    PickupRecoveryPhase=Clear?TEXT("stabilizing"):TEXT("retreating");
    RecoveryStatus=Clear?TEXT("pickup_stabilizing"):TEXT("pickup_retreating");
    if(!Clear || !WasStable) {PickupRecoveryStableSeconds=0;return;}
    PickupRecoveryStableSeconds+=Credit;
    if(PickupRecoveryStableSeconds<C.RecoveryStableTime) return;
    PickupRecoveryStep=0;
    if(bIsolateAfterPickupRetreat)
    {
        PickupRecoveryPhase=TEXT("isolated");
        Stop(PickupRecoveryReason+TEXT("; isolated after verified unlock and retreat"),EPortCraneFault::PickupUnavailable);
        return;
    }
    // Start a new measured acquisition; cargo identity, mass, owner, pose,
    // assigned AGV, yard booking and operating safety limits remain intact.
    Pickup=FSTSPickupController();
    PickupRecoveryPhase=TEXT("reacquiring");RecoveryStatus=TEXT("pickup_reacquiring");
    UE_LOG(LogTemp,Display,TEXT("STS_PICKUP_REACQUIRE: crane=%d cycle=%d cargo=%s"),
        CraneID,PickupRecoveryCount,*CargoActor->ContainerID.ToString());
}
