#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PortEnvironmentComponent.generated.h"

/** Display-only weather model. Own random stream never perturbs logistics randomness. */
struct PORTSIM_API FPortWindState
{
    static constexpr float MinimumSpeed=1.f;
    static constexpr float TypicalSpeed=3.5f;
    static constexpr float MaximumSpeed=7.f;
    bool bWindEnabled=true;
    float WindSpeedMetersPerSecond=TypicalSpeed;
    float WindDirectionDegrees=0;
    FVector WindDirectionVector=FVector(-1,0,0);
    float TargetWindSpeedMetersPerSecond=TypicalSpeed;
    float TargetWindDirectionDegrees=0;
    double SecondsUntilTarget=0;

    void Initialize(int32 Seed);
    void Advance(double SimulationSeconds);
    static float NormalizeDirection(float Degrees);
    static float InterpolateDirection(float Current,float Target,double Seconds);
    static FString DirectionName(float Degrees);
    // Local compass convention: North=world +X, East=world +Y, Up=+Z.
    // This is a simulation convention, not a georeferenced Busan bearing.
    // Degrees describe wind FROM; this unit vector points TO (opposite direction).
    static FVector FlowDirection(float FromDegrees);
private:
    FRandomStream Random;
    float SampleSpeed();
    void SelectTarget();
};

/** One environment owned by the terminal controller; no equipment/physics dependencies. */
UCLASS(ClassGroup=(PortSim), meta=(BlueprintSpawnableComponent))
class PORTSIM_API UPortEnvironmentComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UPortEnvironmentComponent();
    virtual void BeginPlay() override;
    // Called once by AQuayCrane with the already accelerated world DeltaSeconds.
    // Do not multiply speed or delta by SimulationSpeed a second time.
    void AdvanceEnvironment(float SimulationSeconds);

    UFUNCTION(BlueprintPure, Category="Environment|Wind") bool IsWindEnabled() const { return Wind.bWindEnabled; }
    UFUNCTION(BlueprintPure, Category="Environment|Wind") float GetWindSpeedMetersPerSecond() const { return Wind.WindSpeedMetersPerSecond; }
    UFUNCTION(BlueprintPure, Category="Environment|Wind") float GetWindDirectionDegrees() const { return Wind.WindDirectionDegrees; }
    /** Actual wind travel direction, opposite to meteorological wind-from degrees. */
    UFUNCTION(BlueprintPure, Category="Environment|Wind") FVector GetWindDirectionVector() const { return Wind.WindDirectionVector; }
    /** Velocity in metres/second, NOT Unreal centimetres/second. OFF returns zero. No force is applied. */
    UFUNCTION(BlueprintPure, Category="Environment|Wind") FVector GetEffectiveWindVelocity() const;
    UFUNCTION(BlueprintPure, Category="Environment|Wind") FString GetWindDirectionName() const { return FPortWindState::DirectionName(Wind.WindDirectionDegrees); }
    UFUNCTION(BlueprintCallable, Category="Environment|Wind") void ToggleWind() { Wind.bWindEnabled=!Wind.bWindEnabled; }
    const FPortWindState& GetWindState() const { return Wind; }
private:
    FPortWindState Wind;
};
