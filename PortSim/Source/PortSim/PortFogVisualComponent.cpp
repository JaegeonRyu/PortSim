#include "PortFogVisualComponent.h"
#include "PortEnvironmentComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"

namespace
{
    TAutoConsoleVariable<int32> FogVisuals(TEXT("PortSim.FogVisuals"), 1,
        TEXT("Render height fog (0=off, 1=on). Does not change sensor visibility."));
}

void UPortFogVisualComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!FApp::CanEverRender() || GetWorld()->GetNetMode() == NM_DedicatedServer)
        return;

    auto* GameViewport = GetWorld()->GetGameViewport();
    Environment = GetOwner()->FindComponentByClass<UPortEnvironmentComponent>();
    if (GameViewport && Environment)
    {
        Viewport = GameViewport;
        GameViewport->OnBeginDraw().AddUObject(this, &UPortFogVisualComponent::UpdateFogForDraw);
    }
}

void UPortFogVisualComponent::EndPlay(EEndPlayReason::Type Reason)
{
    if (UGameViewportClient* GameViewport = Viewport.Get())
        GameViewport->OnBeginDraw().RemoveAll(this);
    Super::EndPlay(Reason);
}

void UPortFogVisualComponent::UpdateFogForDraw()
{
    const float VisibilityMeters = FogVisuals.GetValueOnGameThread() != 0 && Environment->IsFogEnabled()
        ? Environment->GetFogVisibilityMeters() : 0.f;
    if (VisibilityMeters == LastVisibilityMeters)
        return;
    LastVisibilityMeters = VisibilityMeters;

    if (VisibilityMeters <= 0.f)
    {
        if (Fog)
        {
            Fog->SetVisibility(false);
            FogExposure->bEnabled = false;
        }
        return;
    }

    // UE uses exp2 attenuation and scales density by 1/1000 in cm space.
    // Keep some contrast at the nominal distance for the operator camera.
    const float Density = 14.5f / VisibilityMeters;
    // Limit how far automatic exposure can darken a view filled with haze.
    // The project uses EV100. Allow more adaptation as visibility improves.
    const float MaxExposureEV100 = 2.6f + 0.35f * FMath::Log2(VisibilityMeters / 30.f);
    if (!Fog)
    {
        Fog = NewObject<UExponentialHeightFogComponent>(GetOwner());
        Fog->SetVolumetricFog(false);
        Fog->SetFogHeightFalloff(0.035f);
        // Height fog takes scene-linear luminance. The terminal's exposure
        // needs an HDR value for dense fog to read as daylight haze.
        Fog->SetFogInscatteringColor(FLinearColor(18.f, 18.f, 18.f));
        Fog->SetFogMaxOpacity(0.99f);
        Fog->SetFogDensity(Density);
        Fog->RegisterComponent();

        FogExposure = NewObject<UPostProcessComponent>(GetOwner());
        FogExposure->bUnbound = true;
        FogExposure->Settings.bOverride_AutoExposureMaxBrightness = true;
        FogExposure->Settings.AutoExposureMaxBrightness = MaxExposureEV100;
        FogExposure->RegisterComponent();
        return;
    }

    Fog->SetFogDensity(Density);
    Fog->SetVisibility(true);
    FogExposure->Settings.AutoExposureMaxBrightness = MaxExposureEV100;
    FogExposure->bEnabled = true;
}
