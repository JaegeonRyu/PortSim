#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

void APortWorkingCrane::SamplePickupGeometry()
{
    // Virtual geometric pose/contact sensors. No image recognition or manufacturer accuracy claim.
    auto& O=Observation;const auto& C=STSProfile.Pickup;
    O.bTargetVisible=STSSensorContains(TEXT("spreader_camera"),CargoActor->GetActorLocation()) &&
        !FParse::Param(FCommandLine::Get(),TEXT("PortSimSTSPoseFault"));
    const FQuat SpreaderRotation=Spreader->GetComponentQuat(), CargoRotation=CargoActor->GetActorQuat();
    O.RelativeYawDegrees=FMath::FindDeltaAngleDegrees(CargoRotation.Rotator().Yaw,SpreaderRotation.Rotator().Yaw);
    O.TargetTiltDegrees=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(CargoActor->GetActorUpVector().Z,-1.,1.)));
    bool All=true;
    for(int32 I=0;I<4;++I)
    {
        const FVector Corner((I&1)?100:-100,(I&2)?520:-520,0);
        const FVector RotationGap=SpreaderRotation.RotateVector(Corner)-CargoRotation.RotateVector(Corner)-FVector(0,0,154.5);
        const FVector ActualError=Orientation.UnrotateVector(HeadPosition()-CargoActor->GetActorLocation()+RotationGap);
        PhysicalSeating[I]=ActualError.Size2D()<=C.CornerTolerance && FMath::Abs(ActualError.Z)<=C.VerticalTolerance && O.TargetTiltDegrees<=C.TiltTolerance;
        O.CornerError[I]=Orientation.UnrotateVector(O.SpreaderPosition-O.CargoPosition+RotationGap);
        int32 Missing=INDEX_NONE;FParse::Value(FCommandLine::Get(),TEXT("PortSimSTSSeatFault="),Missing);
        O.CornerSeated[I]=PhysicalSeating[I]&&I!=Missing; All &= O.CornerSeated[I];
    }
    O.bLanded=All&&C.PickupSpeedWithin(O.SpreaderVelocity-O.CargoVelocity);
}

void APortWorkingCrane::AdvancePickup(float Dt)
{
    Pickup.Update(STSProfile.Pickup,Observation,SimulationTime,Dt,STSProfile.SensorMaxAge,Slots[SourceSlot],STSProfile.RatedPayloadKg);
    if(!Pickup.Fault.IsEmpty())
    {
        double MaximumCornerHorizontal=0,MaximumCornerVertical=0;
        for(const FVector& Error:Observation.CornerError)
        {
            MaximumCornerHorizontal=FMath::Max(MaximumCornerHorizontal,double(Error.Size2D()));
            MaximumCornerVertical=FMath::Max(MaximumCornerVertical,double(FMath::Abs(Error.Z)));
        }
        Stop(FString::Printf(TEXT("%s: phase=%s visible=%d seat_entries=%d lock_entries=%d max_seat_s=%.3f relative_speed_cmps=%.3f tilt_deg=%.3f yaw_deg=%.3f corner_xy_cm=%.3f corner_z_cm=%.3f target=%s spreader=%s cargo=%s spreader_velocity=%s cargo_velocity=%s cargo_acceleration=%s cargo_jerk=%s drive_velocity=%s"),
            *Pickup.Fault,Pickup.PhaseName(),Observation.bTargetVisible,Pickup.SeatEntries,Pickup.LockEntries,Pickup.MaximumSeatStableTime,
            (Observation.SpreaderVelocity-Observation.CargoVelocity).Size(),Observation.TargetTiltDegrees,
            Observation.RelativeYawDegrees,MaximumCornerHorizontal,MaximumCornerVertical,*Pickup.Target.ToCompactString(),
            *Observation.SpreaderPosition.ToCompactString(),*Observation.CargoPosition.ToCompactString(),
            *Observation.SpreaderVelocity.ToCompactString(),*Observation.CargoVelocity.ToCompactString(),
            *Observation.CargoAcceleration.ToCompactString(),*Observation.CargoJerk.ToCompactString(),
            *Observation.DriveVelocity.ToCompactString()));
        return;
    }
    if(Pickup.Phase==ESTSPickupPhase::Complete)
    {
        UE_LOG(LogTemp,Display,TEXT("STS_PICKUP_VERIFIED: crane=%d measured_kg=%.2f cog_cm=%s attempts=%d"),CraneID,Pickup.EstimatedMass,*Pickup.EstimatedCoG.ToCompactString(),Pickup.Attempts);
        Stage=3;StageTime=SettleTime=0;return;
    }
    if(Pickup.Phase==ESTSPickupPhase::Attach)
    {
        if(!Observation.AllLocked()){Stop(TEXT("Pickup attachment requires four lock feedback signals"));return;}
        if(bDestinationReady&&!DestinationClear()){Stop(TEXT("Destination slot occupied"));return;}
        // Mechanical coupling preserves the achieved pose; never snap a misaligned box onto the head.
        CargoActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
        CargoActor->GetBody()->SetSimulatePhysics(false);
        CargoActor->AttachToComponent(Spreader,FAttachmentTransformRules::KeepWorldTransform);
        LockedCargoTransform=CargoActor->GetActorTransform().GetRelativeTransform(Spreader->GetComponentTransform());
        CargoActor->LocationOwner=ECargoOwner::STS;bCarrying=true;
        Pickup.Attached(Observation.SpreaderPosition);SampleSTS(true);return;
    }
    MoveSTS(Local(Pickup.Target),Dt);
    if(!Fault.IsEmpty())return;
    // Lock actuators respond separately and require the physical contact, not just a pose estimate.
    if(!bCarrying)for(int32 I=0;I<4;++I)
    {
        if(Pickup.RequestLocks[I]&&PhysicalSeating[I])LockProgress[I]+=Dt;
        else LockProgress[I]=0;
        CornerLocked[I]=LockProgress[I]>=STSProfile.Pickup.LockTime&&I!=LockFault;
    }
}
