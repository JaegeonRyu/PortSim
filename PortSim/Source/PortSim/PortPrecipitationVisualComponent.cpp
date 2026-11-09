#include "PortPrecipitationVisualComponent.h"
#include "PortEnvironmentComponent.h"
#include "QuayCrane.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "Misc/App.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "SimulationPerformance.h"

namespace
{
    TAutoConsoleVariable<int32> RainVisuals(TEXT("PortSim.RainVisuals"), 1,
        TEXT("Local rain quality: 0=off, 1=low, 2=full. Does not change simulation weather."));
    TAutoConsoleVariable<int32> SnowVisuals(TEXT("PortSim.SnowVisuals"), 1,
        TEXT("Render falling snow (0=off, 1=on). Does not change accumulation."));
    TAutoConsoleVariable<int32> SnowCoverVisuals(TEXT("PortSim.SnowCoverVisuals"), 1,
        TEXT("Render accumulated snow (0=off, 1=on). Does not change surface friction."));
    const FName SpawnRateParameter(TEXT("User.SpawnRate"));
    const FName VelocityParameter(TEXT("User.FallVelocity"));
    const FName SnowCoverParameter(TEXT("SnowCover"));
}

void FPortPrecipitationEmitter::Stop()
{
    if (LastSpawnRate < 0.f)
        return;
    Component->DeactivateImmediate();
    LastSpawnRate = -1.f;
}

void FPortPrecipitationEmitter::Draw(AActor* Owner, const FVector& Center, float SpawnRate,
    const FVector& Velocity, float WallDelta)
{
    if (!Component)
    {
        Component = NewObject<UNiagaraComponent>(Owner);
        Component->SetAutoActivate(false);
        Component->SetAsset(System);
        Component->SetForceSolo(true);
        Component->SetCastShadow(false);
        Component->SetVisibleInRayTracing(false);
        Component->SetReceivesDecals(false);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetWorldLocation(Center);
        Component->RegisterComponent();
    }

    const FVector Location = Component->GetComponentLocation();
    // Clear world-space drops left behind by a camera jump.
    const bool bReset = LastSpawnRate < 0.f || FVector::DistSquared(Center, Location) > FMath::Square(2200.);
    if (bReset && Component->IsActive())
        Component->DeactivateImmediate();
    if (!Center.Equals(Location, 15.))
        Component->SetWorldLocation(Center);
    if (bReset || SpawnRate != LastSpawnRate)
    {
        Component->SetVariableFloat(SpawnRateParameter, SpawnRate);
        LastSpawnRate = SpawnRate;
    }
    if (bReset || !LastVelocity.Equals(Velocity, 3.))
    {
        Component->SetVariableVec3(VelocityParameter, Velocity);
        LastVelocity = Velocity;
    }
    if (bReset || !Component->IsActive())
    {
        Component->Activate(bReset);
        Component->SetComponentTickEnabled(false);
    }
    Component->AdvanceSimulation(1, WallDelta);
}

void UPortPrecipitationVisualComponent::BeginPlay()
{
    Super::BeginPlay();
    if (!FApp::CanEverRender() || GetWorld()->GetNetMode() == NM_DedicatedServer)
        return;

    auto* GameViewport = GetWorld()->GetGameViewport();
    if (!GameViewport)
        return;

    Environment = CastChecked<AQuayCrane>(GetOwner())->GetEnvironment();
    // Preload at startup to avoid loading assets when a weather toggle is pressed.
    Rain.System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/PortSim/Assets/Weather/NS_PortRain.NS_PortRain"));
    Snow.System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/PortSim/Assets/Weather/NS_PortSnow.NS_PortSnow"));
    SurfaceParameters = LoadObject<UMaterialParameterCollection>(nullptr,
        TEXT("/Game/PortSim/Assets/Weather/MPC_PortSurface.MPC_PortSurface"));
    Viewport = GameViewport;
    GameViewport->OnBeginDraw().AddUObject(this, &UPortPrecipitationVisualComponent::UpdateForDraw);
}

void UPortPrecipitationVisualComponent::EndPlay(EEndPlayReason::Type Reason)
{
    if (UGameViewportClient* GameViewport = Viewport.Get())
        GameViewport->OnBeginDraw().RemoveAll(this);
    if (SurfaceParameters && LastSnowCover > 0)
        GetWorld()->GetParameterCollectionInstance(SurfaceParameters)->SetScalarParameterValue(SnowCoverParameter, 0.f);
    Super::EndPlay(Reason);
}

void UPortPrecipitationVisualComponent::UpdateSnowCover()
{
    if (!SurfaceParameters)
        return;

    // The committed accumulation persists after snowfall stops. Quantize to
    // 1% steps so small changes do not update render state on every draw.
    const int32 Cover = SnowCoverVisuals.GetValueOnGameThread() != 0
        ? FMath::RoundToInt(Environment->GetSurfaceConditions().Snow * 100.f) : 0;
    if (Cover == LastSnowCover)
        return;
    GetWorld()->GetParameterCollectionInstance(SurfaceParameters)->SetScalarParameterValue(SnowCoverParameter, Cover / 100.f);
    LastSnowCover = Cover;
}

void UPortPrecipitationVisualComponent::UpdateForDraw()
{
    UpdateSnowCover();
    const int32 Quality = RainVisuals.GetValueOnGameThread();
    const bool bRain = Quality > 0 && Rain.System && Environment->IsRainEnabled();
    const float Snowfall = Environment->IsSnowEnabled() ? Environment->GetSnowfallMmPerHour() : 0.f;
    const bool bSnow = SnowVisuals.GetValueOnGameThread() != 0 && Snow.System && Snowfall > 0.f;
    const auto* Terminal = CastChecked<AQuayCrane>(GetOwner());
    const auto* PC = bRain || bSnow ? Cast<APlayerController>(Terminal->GetController()) : nullptr;
    const APlayerCameraManager* Camera = PC && PC->IsLocalController() && PC->GetViewTarget() == Terminal
        ? PC->PlayerCameraManager : nullptr;
    if (!Camera || Camera->GetCameraCacheView().ProjectionMode == ECameraProjectionMode::Orthographic)
    {
        Rain.Stop();
        Snow.Stop();
        PreviousWallSeconds = 0.;
        return;
    }

    const double Now = FPlatformTime::Seconds();
    const float Elapsed = PreviousWallSeconds == 0. ? 1.f / 60.f : float(Now - PreviousWallSeconds);
    const float WallDelta = FMath::Clamp(Elapsed, 0.001f, 1.f / 30.f);
    PreviousWallSeconds = Now;
    const FVector Position = Camera->GetCameraLocation();
    const FVector Center = Position + Camera->GetCameraRotation().Vector() * 850.;
    const float AltitudeScale = FMath::GetMappedRangeValueClamped(
        FVector2f(3000.f, 25000.f), FVector2f(1.f, 0.35f), float(Position.Z));
    const FVector Wind = Environment->GetEffectiveWindVelocity();

    if (bRain)
    {
        PortSimPerformance::FScope Timing(TEXT("rain_visual_update"));
        const float Rainfall = Environment->GetRainfallMmPerHour();
        const bool bLightRain = Rainfall < 10.f;
        const bool bHeavyRain = Rainfall >= 50.f;
        const float TargetRate = bLightRain ? 100.f : bHeavyRain ? 500.f : 250.f;
        const float TargetSpeed = bLightRain ? 550.f : bHeavyRain ? 1450.f : 850.f;
        const float Remaining = Rain.LastSpawnRate < 0.f || FMath::IsNearlyEqual(RainBaseSpawnRate, TargetRate, 0.25f)
            ? 0.f : FMath::Exp(-7.5f * Elapsed);
        RainBaseSpawnRate = FMath::Lerp(TargetRate, RainBaseSpawnRate, Remaining);
        RainFallSpeed = FMath::Lerp(TargetSpeed, RainFallSpeed, Remaining);
        const float SpawnRate = FMath::RoundToFloat(RainBaseSpawnRate * AltitudeScale * (Quality == 1 ? 1.f : 1.8f));
        const FVector Drift = (Wind * 45.).GetClampedToMaxSize(450.);
        Rain.Draw(GetOwner(), Center, SpawnRate, FVector(Drift.X, Drift.Y, -RainFallSpeed), WallDelta);
    }
    else
        Rain.Stop();

    if (bSnow)
    {
        PortSimPerformance::FScope Timing(TEXT("snow_visual_update"));
        const float Strength = FMath::Sqrt(FMath::Clamp(Snowfall / 100.f, 0.f, 1.f));
        // The overview needs only a hint of flakes at the camera's altitude.
        const float OverviewScale = FMath::GetMappedRangeValueClamped(
            FVector2f(25000.f, 85000.f), FVector2f(1.f, 0.22f), float(Position.Z));
        const float SpawnRate = FMath::RoundToFloat(300.f * Strength * AltitudeScale * OverviewScale * (bRain ? 0.65f : 1.f));
        const FVector Drift = (Wind * 65.).GetClampedToMaxSize(350.);
        const float FallSpeed = FMath::Lerp(120.f, 210.f, Strength);
        Snow.Draw(GetOwner(), Center, SpawnRate, FVector(Drift.X, Drift.Y, -FallSpeed), WallDelta);
    }
    else
        Snow.Stop();
}
