#pragma once
#include "CoreMinimal.h"
#include "PortEquipmentActor.h"
#include "AGVDynamics.h"
#include "PortSupportVehicle.generated.h"

UENUM(BlueprintType)
enum class EPortSupportType : uint8 { ReachStacker, YardTractor, EmptyHandler, Forklift, YardChassis };
class APortContainerActor;

/** Individually identifiable support equipment; dispatch is handled separately. */
UCLASS()
class PORTSIM_API APortSupportVehicle : public APortEquipmentActor
{
    GENERATED_BODY()
public:
    void Configure(EPortSupportType Type,int32 Number);
    void PositionRecoverySpreader(FVector Position,FRotator Rotation,float LengthCm);
    bool AdvanceRecoveryDrive(FVector Target,float Dt,bool Clear,bool Final,const FPortSurfaceState& Surface,const FPortFrictionSettings& Friction);
    bool AdvanceRecoveryHydraulics(FVector Target,FRotator Rotation,float LengthCm,float LoadKg,float Dt);
    void RefreshRecoveryLoad(float LoadKg,FVector WorldCoG,float Dt);
    void SetRecoveryPhysicsPaused(bool Paused);
    void ResetRecoveryPhysics();
    void EnterFreePhysics();
    FVector RecoveryHeadPosition() const;
    bool LockRecoveryCargo(APortContainerActor* Cargo);
    bool ReleaseRecoveryCargo(APortContainerActor* Cargo);
    bool HasRecoveryGrip() const { return GrippedCargo.IsValid(); }
    bool IsRecoveryFreePhysics() const { return bFreePhysics; }
    bool OwnsRecoveryPhysicsBody(const UPrimitiveComponent* Part) const;
    UPROPERTY(EditAnywhere,Category="R/S|Physics assumptions") FAGVDynamicsSettings TireSettings;
    UPROPERTY(EditAnywhere,Category="R/S|Physics assumptions",meta=(ClampMin="65000",ClampMax="72000")) float EmptyMassKg=70000;
    UPROPERTY(EditAnywhere,Category="R/S|Reference") float RatedCapacityKg=45000;
    UPROPERTY(EditAnywhere,Category="R/S|Reference") float MaximumSideShiftM=.8f;
    UPROPERTY(EditAnywhere,Category="R/S|Hydraulic assumptions",meta=(ClampMin="1")) float ReliefPressureMPa=25;
    UPROPERTY(EditAnywhere,Category="R/S|Hydraulic assumptions",meta=(ClampMin="1")) float PumpLitresPerMinute=720;
    UPROPERTY(EditAnywhere,Category="R/S|Hydraulic assumptions",meta=(ClampMin="0.001")) float CylinderAreaM2=.025f;
    FAGVDynamicsState TireDynamics;
    FVector HydraulicPositionM=FVector(5.4,0,6.5),HydraulicVelocityMps=FVector::ZeroVector,HydraulicPressurePa=FVector::ZeroVector;
    float PayloadKg=0,HydraulicFlowM3ps=0;
    float SideShiftM=0;
    bool bHydraulicLimited=false,bFreePhysics=false;
    FString PhysicsFault;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) EPortSupportType EquipmentType=EPortSupportType::YardChassis;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 EquipmentNumber=0;
private:
    UPROPERTY() TObjectPtr<UStaticMeshComponent> RecoveryBody;
    UPROPERTY() TObjectPtr<USceneComponent> RecoverySceneRoot;
    UPROPERTY() TObjectPtr<USceneComponent> RecoveryGrip;
    TWeakObjectPtr<APortContainerActor> GrippedCargo;
    FQuat GripRotation=FQuat::Identity;
    FTransform RecoveryBodyRelative;
    FVector CargoCoGM=FVector::ZeroVector,PausedLinear=FVector::ZeroVector,PausedAngular=FVector::ZeroVector;
    bool bPhysicsPaused=false;
};
