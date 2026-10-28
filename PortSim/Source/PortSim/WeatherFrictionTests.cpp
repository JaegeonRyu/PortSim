#if WITH_DEV_AUTOMATION_TESTS
#include "WeatherFriction.h"
#include "PortEnvironmentComponent.h"
#include "PortAGVActor.h"
#include "PortWorkingCrane.h"
#include "PortContainerActor.h"
#include "PortSiteLogistics.h"
#include "AGVReference.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Chaos/PhysicalMaterials.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeatherFrictionTest,"PortSim.Environment.WeatherFriction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FWeatherFrictionTest::RunTest(const FString& Parameters)
{
    FPortFrictionSettings Settings;
    TestTrue(TEXT("Packaged friction assumptions load"),Settings.Load(FPaths::ProjectConfigDir()/TEXT("Weather_Friction.json")));
    FPortSurfaceState Dry,Wet,Snow,Ice; Wet.Wetness=1; Snow.Snow=1; Ice.Ice=1;
    TestTrue(TEXT("Weather reduces static tire grip monotonically"),Settings.Tire.Static(Dry)>Settings.Tire.Static(Wet) &&
        Settings.Tire.Static(Wet)>Settings.Tire.Static(Snow) && Settings.Tire.Static(Snow)>Settings.Tire.Static(Ice));
    FPortSurfaceState Mixed; Mixed.Wetness=1; Mixed.Snow=.5f; Mixed.Ice=.5f;
    TestTrue(TEXT("Coverage blends wet then snow then ice"),FMath::IsNearlyEqual(Settings.Tire.Static(Mixed),.215f,.0001f));
    TestTrue(TEXT("Static/kinetic separation retained"),Settings.Steel.Kinetic(Ice)<Settings.Steel.Static(Ice));
    FPortSurfaceState Invalid; Invalid.Wetness=std::numeric_limits<float>::quiet_NaN(); Invalid.Snow=-4; Invalid.Ice=8; Invalid.Sanitize();
    TestTrue(TEXT("Invalid weather input bounded"),Invalid.Wetness==0 && Invalid.Snow==0 && Invalid.Ice==1);
    auto InvalidSettings=Settings; InvalidSettings.Tire.IceStatic=-1; InvalidSettings.Tire.IceKinetic=5; InvalidSettings.Sanitize();
    TestTrue(TEXT("Kinetic cannot exceed static"),InvalidSettings.Tire.Kinetic(Ice)>0 && InvalidSettings.Tire.Kinetic(Ice)<=InvalidSettings.Tire.Static(Ice));
    TestTrue(TEXT("Combined traction uses the force circle"),FMath::IsNearlyEqual(PortFriction::LongitudinalBudget(5,3),4.,.0001));
    TestEqual(TEXT("No remaining longitudinal grip beyond lateral demand"),PortFriction::LongitudinalBudget(3,5),0.);

    FPortSurfaceAccumulation LargeStep,SmallStep;
    LargeStep.Initialize(Dry,Settings.SurfaceDynamics); SmallStep=LargeStep;
    LargeStep.Advance(Settings.SurfaceDynamics,25,30,.3,600);
    for(int I=0;I<600;++I) SmallStep.Advance(Settings.SurfaceDynamics,25,30,.3,1);
    TestTrue(TEXT("Wet/snow/ice accumulation is independent of playback step"),
        FMath::IsNearlyEqual(LargeStep.WaterMm,SmallStep.WaterMm,1.e-6) &&
        FMath::IsNearlyEqual(LargeStep.SnowMm,SmallStep.SnowMm,1.e-6) && FMath::IsNearlyEqual(LargeStep.Ice,SmallStep.Ice,1.e-6));
    LargeStep.Advance(Settings.SurfaceDynamics,0,0,0,1800);
    for(int I=0;I<1800;++I) SmallStep.Advance(Settings.SurfaceDynamics,0,0,0,1);
    TestTrue(TEXT("Drying and melt-event boundaries remain playback invariant"),
        FMath::IsNearlyEqual(LargeStep.WaterMm,SmallStep.WaterMm,1.e-6) &&
        FMath::IsNearlyEqual(LargeStep.SnowMm,SmallStep.SnowMm,1.e-6) && FMath::IsNearlyEqual(LargeStep.Ice,SmallStep.Ice,1.e-6));
    auto InvalidStep=LargeStep; InvalidStep.Advance(Settings.SurfaceDynamics,100,100,1,std::numeric_limits<double>::quiet_NaN());
    TestEqual(TEXT("Invalid simulation delta does not corrupt reservoirs"),InvalidStep.WaterMm,LargeStep.WaterMm);
    FPortSurfaceAccumulation Melting; Melting.Initialize(Snow,Settings.SurfaceDynamics);
    Melting.Advance(Settings.SurfaceDynamics,0,0,0,60);
    TestTrue(TEXT("Melting snow wets the road without rainfall"),Melting.SnowMm<Settings.SurfaceDynamics.FullSnowDepthMm && Melting.WaterMm>0);

    const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
        .RequiresHitProxies(false).CreateNavigation(false).CreateAISystem(false);
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Friction test world"),World)) return false;
    auto* Vehicle=World->SpawnActor<APortAGVActor>();
    auto* Crane=World->SpawnActor<APortWorkingCrane>();
    auto* Cargo=World->SpawnActor<APortContainerActor>();
    auto* Site=World->SpawnActor<APortSiteLogistics>();
    auto* Environment=NewObject<UPortEnvironmentComponent>(Site);
    Site->TrafficVehicles.Add(Vehicle); Site->Equipment.Add(Crane); Site->ShipContainers.Add(Cargo);
    auto Sync=[&]()
    {
        auto* Material=Environment->GetContactMaterial();
        Site->SetSurfaceConditions(Environment->GetSurfaceConditions(),Environment->GetFrictionSettings(),Material);
    };
    auto Apply=[&](const FPortSurfaceState& S)
    {
        Environment->SetSurfaceConditions(S.Wetness,S.Snow,S.Ice);
        Sync();
    };
    Apply(Dry);
    const float DryBrake=Vehicle->BrakingCmPerSecondSquared(),DryCurve=Vehicle->CurveSpeedCmPerSecond();
    TestTrue(TEXT("Dry hardware acceleration remains authoritative"),FMath::IsNearlyEqual(Vehicle->AccelerationCmPerSecondSquared(),AGVReference::AccelerationLimit(0)));
    auto* DryMaterial=Environment->GetContactMaterial();
    TestTrue(TEXT("Unchanged weather reuses material each frame"),Environment->GetContactMaterial()==DryMaterial);
    Environment->SetFogEnabled(true); Sync();
    TestTrue(TEXT("Fog alone does not change surface friction or material"),Environment->GetContactMaterial()==DryMaterial && Vehicle->Friction.Tire.Static(Vehicle->Surface)==Settings.Tire.Static(Dry));
    Environment->SetRainEnabled(true); Environment->SetRainfallMmPerHour(25); Sync();
    TestEqual(TEXT("Changing rain rate alone does not jump stored water"),Vehicle->Surface.Wetness,0.f);
    Environment->AdvanceEnvironment(30); Sync();
    const float PartialWet=Vehicle->Surface.Wetness;
    TestTrue(TEXT("Rain progressively wets the road and reduces tire coefficient"),PartialWet>0 && PartialWet<1 && Vehicle->Friction.Tire.Static(Vehicle->Surface)<Settings.Tire.Static(Dry));
    Environment->SetRainfallMmPerHour(50); Environment->AdvanceEnvironment(60); Sync();
    TestTrue(TEXT("Heavier rain accumulates a fully wet film"),Vehicle->Surface.Wetness>PartialWet && FMath::IsNearlyEqual(Vehicle->Friction.Tire.Static(Vehicle->Surface),.5f,.0001f));
    TestTrue(TEXT("Rain does not add an arbitrary second hardware derating"),FMath::IsNearlyEqual(Vehicle->AccelerationCmPerSecondSquared(),AGVReference::AccelerationLimit(0)));
    TestTrue(TEXT("Rain updates crane and cargo contact material automatically"),Crane->Surface.Wetness==1 &&
        Cargo->GetBody()->GetBodyInstance()->GetSimplePhysicalMaterial()==Environment->GetContactMaterial() &&
        FMath::IsNearlyEqual(Environment->GetContactMaterial()->StaticFriction,Settings.Steel.Static(Wet)));
    Environment->SetRainEnabled(false); Sync();
    const float RemainingWater=Vehicle->Surface.WaterFilmMm;
    TestTrue(TEXT("Rain OFF retains accumulated road water"),RemainingWater>0 && Vehicle->Surface.Wetness>0);
    Environment->AdvanceEnvironment(300); Sync();
    TestTrue(TEXT("Road dries gradually after rainfall ends"),Vehicle->Surface.WaterFilmMm>0 && Vehicle->Surface.WaterFilmMm<RemainingWater && Vehicle->Surface.WaterChangeMmPerHour<0);
    Environment->AdvanceEnvironment(7200); Sync();
    TestTrue(TEXT("Evaporation and drainage eventually restore dry grip"),Vehicle->Surface.Wetness==0 && FMath::IsNearlyEqual(Vehicle->Friction.Tire.Static(Vehicle->Surface),.8f,.0001f));
    TestTrue(TEXT("Continuous weather uses one shared material object"),Environment->GetContactMaterial()==DryMaterial);
    Environment->SetTemperatureCelsius(-5);
    Environment->SetSnowEnabled(true); Environment->SetSnowfallMmPerHour(100); Environment->AdvanceEnvironment(360); Sync();
    TestEqual(TEXT("Snow selection supplies full coverage without manual surface values"),Vehicle->Surface.Snow,1.f);
    TestTrue(TEXT("Snow selection applies configured tire coefficient"),
        FMath::IsNearlyEqual(Vehicle->Friction.Tire.Static(Vehicle->Surface),.2f,.0001f));
    TestTrue(TEXT("Snow grip reduces curve entry speed"),Vehicle->CurveSpeedCmPerSecond()<DryCurve);
    Environment->SetIceEnabled(true); Environment->SetIceIntensity(1); Environment->AdvanceEnvironment(120); Sync();
    TestTrue(TEXT("Ice dominates wet/snow surface and increases braking distance"),Vehicle->Surface.Ice==1 &&
        FMath::IsNearlyEqual(Vehicle->Friction.Tire.Static(Vehicle->Surface),.08f) && Vehicle->BrakingCmPerSecondSquared()<DryBrake);
    Environment->SetIceEnabled(false); Environment->SetSnowEnabled(false); Environment->SetFogEnabled(false);
    Environment->SetTemperatureCelsius(5);
    Environment->SetSurfaceConditions(.75f,0,.25f); Environment->SetRainEnabled(false); Sync();
    TestTrue(TEXT("Manual values initialize real stored water and ice"),FMath::IsNearlyEqual(Vehicle->Surface.WaterFilmMm,.375f,.0001f) && Vehicle->Surface.Ice==.25f);
    Environment->AdvanceEnvironment(60); Sync();
    TestTrue(TEXT("Initialized ice melts rather than disappearing instantly"),Vehicle->Surface.Ice>0 && Vehicle->Surface.Ice<.25f);
    Apply(Wet);
    TestTrue(TEXT("Wet static coefficient reaches vehicle"),FMath::IsNearlyEqual(Vehicle->Friction.Tire.Static(Vehicle->Surface),.5f));
    TestTrue(TEXT("Cargo Chaos contact receives shared weather material"),Cargo->GetBody()->GetBodyInstance()->GetSimplePhysicalMaterial()==Environment->GetContactMaterial());
    TestTrue(TEXT("Physics uses minimum pair coefficient"),Environment->GetContactMaterial()->FrictionCombineMode==EFrictionCombineMode::Min);
    Apply(Ice);
    auto* IceMaterial=Environment->GetContactMaterial();
    Environment->FrictionSettings.Tire.IceStatic=.06f; Sync();
    TestTrue(TEXT("Editing only tire coefficients refreshes controllers without replacing the material"),Environment->GetContactMaterial()==IceMaterial &&
        FMath::IsNearlyEqual(Vehicle->Friction.Tire.Static(Vehicle->Surface),.06f));
    Environment->FrictionSettings.Steel.IceStatic=.04f; Sync();
    TestTrue(TEXT("Editing metal coefficient updates the actual Chaos material handle"),
        FMath::IsNearlyEqual(Environment->GetContactMaterial()->GetPhysicsMaterial().Get()->StaticFriction,.04f,.0001f));
    Environment->FrictionSettings=Settings; Sync();
    TestTrue(TEXT("Ice reduces actual drive acceleration and braking"),Vehicle->AccelerationCmPerSecondSquared()<DryBrake && Vehicle->BrakingCmPerSecondSquared()<DryBrake);
    TestTrue(TEXT("Ice reduces curve entry speed"),Vehicle->CurveSpeedCmPerSecond()<DryCurve);
    TestTrue(TEXT("LiDAR stopping distance grows at equal speed"),AGVReference::DynamicLiDARSafetyDistance(600,Vehicle->BrakingCmPerSecondSquared())>
        AGVReference::DynamicLiDARSafetyDistance(600,DryBrake));
    TestTrue(TEXT("Crane horizontal drive observes rail adhesion"),Crane->RailAccelerationLimit(100)<30);
    TestTrue(TEXT("Low motor request is not boosted by adhesion model"),FMath::IsNearlyEqual(Crane->RailAccelerationLimit(5),5.,.0001));
    Vehicle->ResetVehicle(FVector::ZeroVector);
    Vehicle->SetFMSCommand(FVector(0,10000,0),0,1,false,5);
    Vehicle->MoveToPosition(FVector(0,10000,0),.1f,false);
    TestTrue(TEXT("Ice motion remains longitudinal and does not snap to target"),Vehicle->GetActorLocation().Y>0 && Vehicle->GetActorLocation().Y<2 && FMath::Abs(Vehicle->GetActorLocation().X)<.001);
    Vehicle->WindVelocity=FVector(20,0,0);
    const float WindGrip=Vehicle->TireGripCmPerSecondSquared(); Vehicle->WindVelocity=FVector::ZeroVector;
    TestTrue(TEXT("Wind consumes force reserve without changing friction coefficient"),WindGrip<Vehicle->TireGripCmPerSecondSquared() && Vehicle->Friction.Tire.Static(Vehicle->Surface)==Settings.Tire.Static(Ice));
    const double Demand=PortFriction::SeatingDemand(10000,.03,.02,.04,0,100);
    const double Normal=10000*PortFriction::Gravity*Settings.SeatingWeightFraction;
    TestTrue(TEXT("Moderate seating demand accepted on dry steel"),PortFriction::CanSeat(Demand,Settings.Utilization*Settings.Steel.Static(Dry)*Normal,false));
    TestFalse(TEXT("Same seating motion rejected on ice before locks"),PortFriction::CanSeat(Demand,Settings.Utilization*Settings.Steel.Static(Ice)*Normal,false));
    TestTrue(TEXT("Mechanical locks retain load independently of surface friction"),PortFriction::CanSeat(Demand,0,true));
    World->DestroyWorld(false);
    return true;
}
#endif
