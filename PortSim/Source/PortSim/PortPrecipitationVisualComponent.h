#pragma once

#include "Components/ActorComponent.h"
#include "PortPrecipitationVisualComponent.generated.h"

class UGameViewportClient;
class UMaterialParameterCollection;
class UNiagaraComponent;
class UNiagaraSystem;
class UPortEnvironmentComponent;

USTRUCT()
struct FPortPrecipitationEmitter
{
    GENERATED_BODY()

    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> System;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Component;
    float LastSpawnRate = -1.f;
    FVector LastVelocity = FVector::ZeroVector;

    void Stop();
    void Draw(AActor* Owner, const FVector& Center, float SpawnRate, const FVector& Velocity, float WallDelta);
};

/** Advances local rain and snow only on viewport draws, and presents accumulated snow. */
UCLASS()
class UPortPrecipitationVisualComponent : public UActorComponent
{
    GENERATED_BODY()

private:
    void BeginPlay() override;
    void EndPlay(EEndPlayReason::Type Reason) override;
    void UpdateForDraw();
    void UpdateSnowCover();

    UPROPERTY(Transient) TObjectPtr<UPortEnvironmentComponent> Environment;
    UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> SurfaceParameters;
    UPROPERTY(Transient) FPortPrecipitationEmitter Rain;
    UPROPERTY(Transient) FPortPrecipitationEmitter Snow;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    double PreviousWallSeconds = 0.;
    float RainBaseSpawnRate = 0.f;
    float RainFallSpeed = 0.f;
    int32 LastSnowCover = 0;
};
