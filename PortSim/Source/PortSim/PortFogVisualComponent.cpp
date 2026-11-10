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
    const bool bVisibilityChanged=VisibilityMeters!=LastVisibilityMeters;
    if (!bVisibilityChanged && (VisibilityMeters<=0.f || DaylightIlluminance==LastDaylightIlluminance))
        return;
    LastVisibilityMeters = VisibilityMeters;

    if (VisibilityMeters <= 0.f)
    {
        if (Fog)
        {
            Fog->SetVisibility(false);
            FogExposure->bEnabled=false;
        }
        return;
    }

    if (!Fog)
    {
        Fog = NewObject<UExponentialHeightFogComponent>(GetOwner());
        Fog->SetVolumetricFog(false);
        Fog->SetFogHeightFalloff(0.035f);
        Fog->SetFogMaxOpacity(0.99f);
        Fog->RegisterComponent();

        FogExposure=NewObject<UPostProcessComponent>(GetOwner());
        FogExposure->bUnbound=true;
        FogExposure->Settings.bOverride_AutoExposureMaxBrightness=true;
        FogExposure->RegisterComponent();
    }

    if (bVisibilityChanged)
    {
        // UE uses exp2 attenuation and scales density by 1/1000 in cm space.
        Fog->SetFogDensity(14.5f / VisibilityMeters);
        Fog->SetVisibility(true);
    }
    // Approximate broadband scattering by the droplets with a neutral diffuse
    // luminance. This is proportional to ambient daylight, including twilight.
    const float Luminance=0.5f*DaylightIlluminance/UE_PI;
    Fog->SetFogInscatteringColor(FLinearColor(Luminance,Luminance,Luminance));
    // In bright haze, keep eye adaptation from metering the filled frame down to grey.
    // Lift the limit with visibility. The 12 EV floor leaves dim scenes free to adapt.
    FogExposure->bEnabled=true;
    FogExposure->Settings.AutoExposureMaxBrightness=FMath::Max(12.f,FMath::Log2(FMath::Max(1.f,Luminance))+0.5f)
        +0.8f*FMath::Log2(VisibilityMeters/30.f);
    LastDaylightIlluminance=DaylightIlluminance;
}
