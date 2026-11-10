#pragma once

#include "Components/ActorComponent.h"
#include "PortFogVisualComponent.generated.h"

class UExponentialHeightFogComponent;
class UGameViewportClient;
class UPortEnvironmentComponent;
class UPostProcessComponent;

/** Presents the committed weather on the game viewport. */
UCLASS()
class UPortFogVisualComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    void SetDaylightIlluminance(float Lux) { DaylightIlluminance=Lux; }

private:
    void BeginPlay() override;
    void EndPlay(EEndPlayReason::Type Reason) override;
    void UpdateFogForDraw();

    UPROPERTY(Transient) TObjectPtr<UPortEnvironmentComponent> Environment;
    UPROPERTY(Transient) TObjectPtr<UExponentialHeightFogComponent> Fog;
    UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> FogExposure;
    TWeakObjectPtr<UGameViewportClient> Viewport;
    float LastVisibilityMeters = 0.f;
    float DaylightIlluminance = 0.f;
    float LastDaylightIlluminance = 0.f;
};
