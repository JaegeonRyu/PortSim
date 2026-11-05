#include "PortRainVisualComponent.h"
#include "QuayCrane.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "SimulationPerformance.h"

namespace
{
    TAutoConsoleVariable<int32> RainVisuals(TEXT("PortSim.RainVisuals"), 1,
        TEXT("Local rain quality: 0=off, 1=low, 2=full. Does not change simulation weather."));
    const FName SpawnRateParameter(TEXT("User.SpawnRate"));
    const FName VelocityParameter(TEXT("User.FallVelocity"));
}

void UPortRainVisualComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!FApp::CanEverRender() || GetWorld()->GetNetMode() == NM_DedicatedServer)
        return;

    auto* GameViewport = GetWorld()->GetGameViewport();
    if (!GameViewport)
        return;

    Terminal = CastChecked<AQuayCrane>(GetOwner());
    // Load at level startup so toggling rain does not load the asset mid-frame.
    RainSystem = LoadObject<UNiagaraSystem>(nullptr,
        TEXT("/Game/PortSim/Assets/Weather/NS_PortRain.NS_PortRain"));
    if (RainSystem)
    {
        Viewport = GameViewport;
        GameViewport->OnBeginDraw().AddUObject(this, &UPortRainVisualComponent::UpdateRainForDraw);
    }
}

void UPortRainVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UGameViewportClient* GameViewport = Viewport.Get())
        GameViewport->OnBeginDraw().RemoveAll(this);
    Super::EndPlay(EndPlayReason);
}

void UPortRainVisualComponent::UpdateRainForDraw()
{
    const int32 Quality = RainVisuals.GetValueOnGameThread();
    const UPortEnvironmentComponent& Environment = *Terminal->GetEnvironment();
    const auto* PC = Quality > 0 && Environment.IsRainEnabled()
        ? Cast<APlayerController>(Terminal->GetController()) : nullptr;
    const APlayerCameraManager* Camera = PC && PC->IsLocalController() && PC->GetViewTarget() == Terminal
        ? PC->PlayerCameraManager : nullptr;
    if (!Camera || Camera->GetCameraCacheView().ProjectionMode == ECameraProjectionMode::Orthographic)
    {
        if (PreviousWallSeconds > 0.)
            Rain->DeactivateImmediate();
        PreviousWallSeconds = 0.;
        return;
    }

    PortSimPerformance::FScope Timing(TEXT("rain_visual_update"));
    const double Now = FPlatformTime::Seconds();
    const bool bStarting = PreviousWallSeconds == 0.;
    const float Elapsed = bStarting ? 1.f / 60.f : float(Now - PreviousWallSeconds);
    const float WallDelta = FMath::Clamp(Elapsed, 0.001f, 1.f / 30.f);
    PreviousWallSeconds = Now;
    const FVector CameraPosition = Camera->GetCameraLocation();
    const FVector Center = CameraPosition + Camera->GetCameraRotation().Vector() * 850.;
    // Keep a lighter cue in the aerial perspective view.
    const float AltitudeScale = FMath::GetMappedRangeValueClamped(
        FVector2f(3000.f, 25000.f), FVector2f(1.f, 0.35f), float(CameraPosition.Z));
    const float Rainfall = Environment.GetRainfallMmPerHour();
    const bool bLightRain = Rainfall < 10.f;
    const bool bHeavyRain = Rainfall >= 50.f;
    const float TargetRate = bLightRain ? 100.f : bHeavyRain ? 500.f : 250.f;
    // Visual fall speeds in cm/s. Keep the existing speed for moderate rain.
    const float TargetFallSpeed = bLightRain ? 550.f : bHeavyRain ? 1450.f : 850.f;
    if (bStarting || FMath::IsNearlyEqual(BaseSpawnRate, TargetRate, 0.25f))
    {
        BaseSpawnRate = TargetRate;
        FallSpeed = TargetFallSpeed;
    }
    else
    {
        // Cover about 95% of the change in 0.4 seconds of wall time.
        const float Remaining = FMath::Exp(-7.5f * Elapsed);
        BaseSpawnRate = FMath::Lerp(TargetRate, BaseSpawnRate, Remaining);
        FallSpeed = FMath::Lerp(TargetFallSpeed, FallSpeed, Remaining);
    }
    const float QualityScale = Quality == 1 ? 1.f : 1.8f;
    const float SpawnRate = FMath::RoundToFloat(BaseSpawnRate * AltitudeScale * QualityScale);
    const FVector Wind = (Environment.GetEffectiveWindVelocity() * 45.).GetClampedToMaxSize(450.);
    const FVector Velocity(Wind.X, Wind.Y, -FallSpeed);

    if (!Rain)
    {
        Rain = NewObject<UNiagaraComponent>(Terminal);
        Rain->SetAutoActivate(false);
        Rain->SetAsset(RainSystem);
        Rain->SetForceSolo(true);
        Rain->SetCastShadow(false);
        Rain->SetVisibleInRayTracing(false);
        Rain->SetReceivesDecals(false);
        Rain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Rain->SetWorldLocation(Center);
        Rain->RegisterComponent();
    }

    // A preset jump should clear the old world-space drops immediately.
    const bool bMoved = !Center.Equals(Rain->GetComponentLocation(), 15.);
    const bool bReset = bStarting || FVector::DistSquared(Center, Rain->GetComponentLocation()) > FMath::Square(2200.);
    if (bReset && Rain->IsActive())
        Rain->DeactivateImmediate();
    if (bMoved)
        Rain->SetWorldLocation(Center);
    if (bReset || SpawnRate != LastSpawnRate)
    {
        Rain->SetVariableFloat(SpawnRateParameter, SpawnRate);
        LastSpawnRate = SpawnRate;
    }
    if (bReset || !LastVelocity.Equals(Velocity, 3.))
    {
        Rain->SetVariableVec3(VelocityParameter, Velocity);
        LastVelocity = Velocity;
    }
    if (bReset || !Rain->IsActive())
    {
        Rain->Activate(bReset);
        Rain->SetComponentTickEnabled(false);
    }
    // The engine can skip a draw while catching up with physics. Advance only on
    // actual viewport draws, using one bounded step of elapsed wall time.
    Rain->AdvanceSimulation(1, WallDelta);
}
