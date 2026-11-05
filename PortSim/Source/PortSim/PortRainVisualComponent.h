#pragma once

#include "Components/ActorComponent.h"
#include "PortRainVisualComponent.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class AQuayCrane;
class UGameViewportClient;

/** Camera-local rain presentation. Advances only when the game viewport draws. */
UCLASS()
class UPortRainVisualComponent : public UActorComponent
{
    GENERATED_BODY()

private:
    void BeginPlay() override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    void UpdateRainForDraw();

    UPROPERTY(Transient) TObjectPtr<AQuayCrane> Terminal;
    UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> RainSystem;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Rain;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    double PreviousWallSeconds = 0.;
    float BaseSpawnRate = 0.f;
    float FallSpeed = 0.f;
    float LastSpawnRate = -1.f;
    FVector LastVelocity = FVector::ZeroVector;
};
