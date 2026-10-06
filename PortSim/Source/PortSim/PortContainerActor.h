#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PortContainerActor.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class ECargoOwner : uint8 { Ship, STS, AGV, RMG, Yard };

UCLASS(Blueprintable)
class PORTSIM_API APortContainerActor : public AActor
{
    GENERATED_BODY()
public:
    APortContainerActor();
    virtual void BeginPlay() override;
    void InitializeContainer(int32 Number);
    void ResetCargo(FVector Position);
    void ApplyContainerAppearance();
    void SetPhysicalParameters(float Mass, FVector CoGOffset);
    void ApplyWind(FVector WindVelocityMetersPerSecond,float DragCoefficient=1.2f);
    void SetSecuredVesselMotion(FVector Position,FQuat Rotation,FVector VelocityCentimetersPerSecond,
        FVector AccelerationCentimetersPerSecondSquared=FVector::ZeroVector,
        FVector JerkCentimetersPerSecondCubed=FVector::ZeroVector);
    /** Interpolate the latest vessel-motion frame for fixed-rate crane substeps. */
    void ApplySecuredMotionFraction(double Fraction);
    FVector GetMotionVelocity() const;
    FVector GetMotionAcceleration() const;
    FVector GetMotionJerk() const;
    UStaticMeshComponent* GetBody() const { return Body; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Container") FName ContainerID;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Container", meta=(ClampMin="1")) float MassKg=12000.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Container") FVector CoGOffsetCm=FVector::ZeroVector;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Container") ECargoOwner LocationOwner=ECargoOwner::Ship;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Container") TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Container") TObjectPtr<UStaticMeshComponent> Visual;
    FVector KinematicVelocityCentimetersPerSecond=FVector::ZeroVector;
    FVector KinematicAccelerationCentimetersPerSecondSquared=FVector::ZeroVector;
    FVector KinematicJerkCentimetersPerSecondCubed=FVector::ZeroVector;
private:
    FTransform PreviousSecuredTransform=FTransform::Identity, CurrentSecuredTransform=FTransform::Identity;
    FVector PreviousSecuredVelocity=FVector::ZeroVector, CurrentSecuredVelocity=FVector::ZeroVector;
    FVector PreviousSecuredAcceleration=FVector::ZeroVector, CurrentSecuredAcceleration=FVector::ZeroVector;
    FVector PreviousSecuredJerk=FVector::ZeroVector, CurrentSecuredJerk=FVector::ZeroVector;
    bool bHasSecuredMotionFrame=false;
};
