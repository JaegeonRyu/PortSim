#include "QuayCrane.h"
#include "PortWorkingCrane.h"
#include "TerminalLayout.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
    constexpr int32 ShadowBudget = 6;
    constexpr float ShadowRangeCm = 23000.f; // Includes the built-in 200 m STS camera.
    constexpr float ShadowRetentionCm = 1500.f;

    bool NeedsWorkLights(bool bAlreadyOn, float DaylightLux, float FogMetres, float RainRate, float SnowRate)
    {
        // The environment has no cloud optical-depth model. Use precipitation as a
        // proxy for loss of daylight, and independently respond to low fog visibility.
        const float RainTransmission = FMath::Lerp(1.f, .12f, FMath::Clamp(RainRate / 75.f, 0.f, 1.f));
        const float SnowTransmission = FMath::Lerp(1.f, .22f, FMath::Clamp(SnowRate / 80.f, 0.f, 1.f));
        const float EstimatedLux = DaylightLux * FMath::Min(RainTransmission, SnowTransmission);
        return EstimatedLux < (bAlreadyOn ? 750.f : 450.f) || FogMetres < (bAlreadyOn ? 550.f : 400.f);
    }

    USpotLightComponent* AddSpot(USceneComponent* Parent, const TCHAR* Name, const FVector& MountM,
        const FVector& AimM, float Candela, float RangeM, float ConeDegrees, float TemperatureK, float Bounce=0.f)
    {
        AActor* Owner = Parent->GetOwner();
        auto* Spot = NewObject<USpotLightComponent>(Owner, MakeUniqueObjectName(Owner, USpotLightComponent::StaticClass(), FName(Name)));
        Owner->AddInstanceComponent(Spot);
        Spot->SetupAttachment(Parent);
        Spot->SetAbsolute(false, false, true);
        Spot->SetMobility(EComponentMobility::Movable);
        // The trolley mesh is scaled to its physical size. Mount offsets are still in metres.
        Spot->SetRelativeLocationAndRotation(MountM * 100. / Parent->GetComponentScale(), (AimM - MountM).Rotation());
        Spot->SetUseInverseSquaredFalloff(true);
        Spot->SetIntensityUnits(ELightUnits::Candelas);
        Spot->SetIntensity(Candela);
        Spot->SetAttenuationRadius(RangeM * 100.f);
        Spot->SetInnerConeAngle(ConeDegrees * .40f);
        Spot->SetOuterConeAngle(ConeDegrees);
        Spot->SetUseTemperature(true);
        Spot->SetTemperature(TemperatureK);
        Spot->SetSpecularScale(.40f);
        Spot->SetCastShadows(false);
        Spot->SetCastRaytracedShadows(ECastRayTracedShadow::Disabled);
        Spot->SetIndirectLightingIntensity(Bounce);
        Spot->SetVolumetricScatteringIntensity(0.f);
        Spot->SetVisibility(false);
        Spot->RegisterComponent();
        return Spot;
    }

    UHierarchicalInstancedStaticMeshComponent* AddFixtureMesh(USceneComponent* Root, const TCHAR* Name,
        const TCHAR* Shape, const TCHAR* Material)
    {
        auto* Mesh = NewObject<UHierarchicalInstancedStaticMeshComponent>(Root->GetOwner(), FName(Name));
        Root->GetOwner()->AddInstanceComponent(Mesh);
        Mesh->SetupAttachment(Root);
        Mesh->SetMobility(Root->Mobility);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);
        Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Shape));
        Mesh->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, Material));
        Mesh->RegisterComponent();
        return Mesh;
    }
}

void AQuayCrane::BuildNightAmbientLight()
{
    NightAmbientLight = NewObject<UDirectionalLightComponent>(this, TEXT("PortNightAmbient"));
    AddInstanceComponent(NightAmbientLight);
    NightAmbientLight->SetMobility(EComponentMobility::Movable);
    NightAmbientLight->SetAtmosphereSunLight(false);
    NightAmbientLight->SetWorldRotation(FRotator(-57., 145., 0.));
    NightAmbientLight->SetLightColor(FLinearColor(.43f, .61f, 1.f));
    NightAmbientLight->SetCastShadows(false);
    NightAmbientLight->SetSpecularScale(0.f);
    NightAmbientLight->SetIndirectLightingIntensity(0.f);
    NightAmbientLight->SetVolumetricScatteringIntensity(0.f);
    NightAmbientLight->SetIntensity(0.f);
    NightAmbientLight->RegisterComponent();
}

void AQuayCrane::UpdateNightAmbientLight(double SolarElevation)
{
    if (!NightAmbientLight) return;
    // Low-level skyglow from the surrounding port and city. This approximates
    // distant diffuse light while Lumen handles bounce from nearby floodlights.
    constexpr float NightAmbientLux = 3.f;
    const float Illuminance = NightAmbientLux * (1.0 - FMath::SmoothStep(-12.0, -4.0, SolarElevation));
    if (!FMath::IsNearlyEqual(NightAmbientLight->Intensity, Illuminance, .005f))
        NightAmbientLight->SetIntensity(Illuminance);
}

void AQuayCrane::BuildTerminalLighting(USceneComponent* SiteRoot)
{
    if (!FApp::CanEverRender() || GetNetMode() == NM_DedicatedServer) return;

    auto* Poles = AddFixtureMesh(SiteRoot, TEXT("TerminalLightPoles"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
        TEXT("/Game/PortSim/Assets/Materials/M_Steel.M_Steel"));
    auto* Heads = AddFixtureMesh(SiteRoot, TEXT("TerminalFloodlightHousings"), TEXT("/Engine/BasicShapes/Cube.Cube"),
        TEXT("/Game/PortSim/Assets/Materials/M_SiteWhite.M_SiteWhite"));

    auto AddPole = [&](const FVector& Base, float Height, float Diameter)
    {
        Poles->AddInstance(FTransform(FRotator::ZeroRotator, (Base + FVector(0,0,Height * .5f)) * 100.,
            FVector(Diameter, Diameter, Height)));
    };
    constexpr float MastHeight = 30.f;
    auto AddMastHead = [&](const FVector& Mount, const FVector& Aim, float Candela, float Range, float Cone)
    {
        Heads->AddInstance(FTransform((Aim - Mount).Rotation(), Mount * 100., FVector(.55,.88,.35)));
        TerminalLights.Add(AddSpot(SiteRoot, TEXT("YardMastFloodlight"), Mount, Aim, Candela, Range, Cone, 4100.f, .38f));
    };
    auto AddTower = [&](float PlanX, float Y, const FVector& Inward)
    {
        const FVector Base(TerminalLayout::SiteX(PlanX), Y, .22f);
        AddPole(Base, MastHeight, .65f);
        const FVector Top = Base + FVector(0,0,MastHeight);
        const FVector Along(-Inward.Y, Inward.X, 0);
        for (float Offset : {-70.f, 0.f, 70.f})
        {
            const FVector Mount = Top + Inward * .65 + Along * (Offset / 60.f);
            const FVector Aim = Base + Inward * (Offset == 0.f ? 48. : 89.) + Along * Offset;
            AddMastHead(Mount, Aim, Offset == 0.f ? 315000.f : 630000.f, 195.f, 37.f);
        }
        if (Inward == FVector::ForwardVector)
            AddMastHead(Top - Inward * .65, Base - Inward * 50., 240000.f, 150.f, 51.f);
    };

    // Yard margins outside the AGV apron and RMG rail ends. Leave the central crossing open.
    for (float Y : {-445.f, -305.f, -190.f, -115.f, 25.f, 165.f, 305.f})
    {
        AddTower(160.f, Y, FVector::ForwardVector);
        AddTower(608.f, Y, -FVector::ForwardVector);
    }
    for (float X : {290.f, 475.f})
    {
        AddTower(X, -519.f, FVector::RightVector);
        AddTower(X, 354.f, -FVector::RightVector);
    }
    // The central road has two lane centres 4.5 m either side of this median.
    // Keep the median towers away from the perimeter intersections.
    for (float X : {265.f, 410.f, 555.f})
    {
        const FVector Base(TerminalLayout::SiteX(X), TerminalLayout::CentralRoadY, .22);
        AddPole(Base, MastHeight, .65f);
        for (double Along : {-1., 1.})
            for (double Across : {-1., 1.})
                AddMastHead(Base + FVector(Along * .75, Across * .75, MastHeight),
                    Base + FVector(Along * 43., Across * 80., 0.), 495000.f, 175.f, 38.f);
    }

    // Roadside infill at the uncovered approaches, standby area and gate.
    // Coordinates are plan metres, with the aiming points on the paved routes.
    const struct { FVector2D Pole; FVector2D Target; } RoadLights[] = {
        {{132,-477},{113,-490}}, {{132,-146},{124,-157}}, {{153,359},{132,347}},
        {{116,388},{87,405}}, {{116,489},{87,493}}, {{116,577},{87,581}},
        {{650,-146},{635,-160}}, {{653,353},{635,347}},
        {{754,609},{773,625}}, {{834,609},{817,625}},
    };
    for (const auto& Road : RoadLights)
    {
        const FVector Base(TerminalLayout::SiteX(Road.Pole.X), Road.Pole.Y, .22);
        const FVector Aim(TerminalLayout::SiteX(Road.Target.X), Road.Target.Y, .22);
        const FVector Arm = (Aim - Base).GetSafeNormal2D();
        const FVector Mount = Base + FVector(0,0,11.) + Arm * .8;
        AddPole(Base, 11.f, .24f);
        Heads->AddInstance(FTransform((Aim - Mount).Rotation(), Mount * 100., FVector(.38,.5,.22)));
        TerminalLights.Add(AddSpot(SiteRoot, TEXT("RoadFloodlight"), Mount, Aim, 27000.f, 53.f, 42.f, 3100.f));
    }

    for (APortWorkingCrane* Crane : WorkingCranes)
        Crane->AddTerminalWorkLights(TerminalLights, TerminalShadowLights);
}

void APortWorkingCrane::AddTerminalWorkLights(TArray<TWeakObjectPtr<USpotLightComponent>>& Lights,
    TArray<TWeakObjectPtr<USpotLightComponent>>& ShadowLights)
{
    const double GirderMountZ = BeamZ / 100. - 1.4;
    if (bSTS)
    {
        Lights.Add(AddSpot(RootComponent, TEXT("STSDeckFloodlight"), {-28,-10,GirderMountZ}, {-43,3,7}, 118000.f, 78.f, 37.f, 3500.f));
        Lights.Add(AddSpot(RootComponent, TEXT("STSHandoverFloodlight"), {28,10,GirderMountZ}, {46,0,1}, 100000.f, 71.f, 36.f, 3800.f));
    }
    else
        Lights.Add(AddSpot(RootComponent, TEXT("RMGGantryFloodlight"), {15,3,GirderMountZ}, {-4,0,1}, 32000.f, 48.f, 41.f, 3650.f));

    const FVector Mount(0, 2.6, -1.05);
    const FVector Aim(0, 0, -BeamZ / 100.);
    auto* Task = AddSpot(Trolley, TEXT("TrolleyTaskFloodlight"), Mount, Aim,
        bSTS ? 74000.f : 39000.f, bSTS ? 66.f : 43.f, bSTS ? 29.f : 35.f, 4400.f);
    Lights.Add(Task);
    ShadowLights.Add(Task);
}

void AQuayCrane::UpdateTerminalLightingForDraw()
{
    if (TerminalLights.IsEmpty()) return;
    const bool bOn = NeedsWorkLights(bTerminalLightsOn, TerminalDaylightLux,
        Environment->IsFogEnabled() ? Environment->GetFogVisibilityMeters() : MAX_flt,
        Environment->IsRainEnabled() ? Environment->GetRainfallMmPerHour() : 0.f,
        Environment->IsSnowEnabled() ? Environment->GetSnowfallMmPerHour() : 0.f);
    if (bOn != bTerminalLightsOn)
    {
        bTerminalLightsOn = bOn;
        NextTerminalShadowUpdate = 0.;
        for (const auto& Entry : TerminalLights)
            if (auto* Light = Entry.Get()) Light->SetVisibility(bOn);
        if (!bOn)
            for (const auto& Entry : TerminalShadowLights)
                if (auto* Light = Entry.Get()) Light->SetCastShadows(false);
        UE_LOG(LogTemp, Display, TEXT("PORTSIM_TERMINAL_LIGHTS: on=%d fixtures=%d daylight_lux=%.1f fog_m=%.0f"),
            bOn, TerminalLights.Num(), TerminalDaylightLux,
            Environment->IsFogEnabled() ? Environment->GetFogVisibilityMeters() : 0.f);
    }

    const double WallNow = FPlatformTime::Seconds();
    if (!bOn || WallNow < NextTerminalShadowUpdate) return;
    NextTerminalShadowUpdate = WallNow + .35;
    const auto* PC = Cast<APlayerController>(GetController());
    if (!PC) return;

    FVector CameraLocation;
    FRotator ViewRotation;
    PC->GetPlayerViewPoint(CameraLocation, ViewRotation);
    struct FShadowCandidate { USpotLightComponent* Light; double Distance; };
    TArray<FShadowCandidate, TInlineAllocator<64>> Nearby;
    for (const auto& Entry : TerminalShadowLights)
    {
        if (auto* Light = Entry.Get())
        {
            const double Distance = FVector::Distance(CameraLocation, Light->GetComponentLocation());
            const double Adjusted = Distance - (Light->CastShadows ? ShadowRetentionCm : 0.f);
            if (Adjusted <= ShadowRangeCm) Nearby.Add({Light, Adjusted});
        }
    }
    Nearby.Sort([](const FShadowCandidate& A, const FShadowCandidate& B) { return A.Distance < B.Distance; });
    const int32 Count = FMath::Min(Nearby.Num(), ShadowBudget);
    for (const auto& Entry : TerminalShadowLights)
    {
        if (auto* Light = Entry.Get())
        {
            bool bCasts = false;
            for (int32 Index = 0; Index < Count; ++Index) bCasts |= Nearby[Index].Light == Light;
            if (Light->CastShadows != bCasts) Light->SetCastShadows(bCasts);
        }
    }
}

void AQuayCrane::ClearTerminalLighting()
{
    // The site and crane actors own their fixtures and destroy them on site rebuild.
    TerminalLights.Reset();
    TerminalShadowLights.Reset();
    bTerminalLightsOn = false;
    NextTerminalShadowUpdate = 0.;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerminalWorkLightSwitchTest, "PortSim.Environment.TerminalWorkLightSwitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTerminalWorkLightSwitchTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Twilight turns the lights on"), NeedsWorkLights(false, 400.f, MAX_flt, 0.f, 0.f));
    TestFalse(TEXT("Clear daylight turns the lights off"), NeedsWorkLights(true, 20000.f, MAX_flt, 0.f, 0.f));
    TestTrue(TEXT("Fog turns the lights on during daylight"), NeedsWorkLights(false, 20000.f, 300.f, 0.f, 0.f));
    TestTrue(TEXT("Fog must clear past the release threshold"), NeedsWorkLights(true, 20000.f, 480.f, 0.f, 0.f));
    TestFalse(TEXT("The same visibility does not turn off lights back on"), NeedsWorkLights(false, 20000.f, 480.f, 0.f, 0.f));
    TestTrue(TEXT("Heavy rain darkens early twilight enough to switch on"), NeedsWorkLights(false, 2000.f, MAX_flt, 75.f, 0.f));
    TestTrue(TEXT("Heavy snow darkens early twilight enough to switch on"), NeedsWorkLights(false, 2000.f, MAX_flt, 0.f, 80.f));
    return true;
}
#endif
