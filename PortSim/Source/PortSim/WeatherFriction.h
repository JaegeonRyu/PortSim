#pragma once
#include "CoreMinimal.h"
#include "WeatherFriction.generated.h"

// Engineering assumptions, not measured site/manufacturer coefficients. SI units.
USTRUCT(BlueprintType)
struct PORTSIM_API FPortSurfaceState
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) float Wetness=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) float Snow=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0",ClampMax="1")) float Ice=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float WaterFilmMm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float SnowDepthMm=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float WaterChangeMmPerHour=0;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) float SnowChangeMmPerHour=0;
    void Sanitize();
};

// Air-temperature degree-day assumptions; no pavement heat/energy balance.
USTRUCT(BlueprintType)
struct PORTSIM_API FPortSurfaceDynamics
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float FullWetWaterMm=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxWaterFilmMm=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float DrainHalfLifeSeconds=600;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float EvaporationMmPerHour=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float FullSnowDepthMm=10;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaxSnowDepthMm=500;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MaximumSnowfallMmPerHour=100;
    // Melt rates below apply at this positive air temperature.
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float MeltReferenceTemperatureCelsius=5;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float SnowMeltMmPerHour=20;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float SnowWaterRatio=.1f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float IceBuildSeconds=120;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float IceMeltSeconds=1800;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float IceWaterEquivalentMm=1;
    void Sanitize();
};

struct PORTSIM_API FPortSurfaceAccumulation
{
    double WaterMm=0,SnowMm=0,Ice=0,WaterRate=0,SnowRate=0;
    void Initialize(const FPortSurfaceState& Initial,const FPortSurfaceDynamics& Settings);
    void Advance(const FPortSurfaceDynamics& Settings,double RainMmPerHour,double SnowfallMmPerHour,
        double IcingTarget,double Seconds,double TemperatureCelsius=18);
    FPortSurfaceState Sample(const FPortSurfaceDynamics& Settings) const;
};

USTRUCT(BlueprintType)
struct PORTSIM_API FPortFrictionCoefficients
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float DryStatic=.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float WetStatic=.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float SnowStatic=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float IceStatic=.05f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float DryKinetic=.3f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float WetKinetic=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float SnowKinetic=.08f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float IceKinetic=.03f;
    float Static(const FPortSurfaceState& State) const;
    float Kinetic(const FPortSurfaceState& State) const;
    void Sanitize();
};

USTRUCT(BlueprintType)
struct PORTSIM_API FPortFrictionSettings
{
    GENERATED_BODY()
    FPortFrictionSettings();
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FPortFrictionCoefficients Tire;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FPortFrictionCoefficients Rail;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FPortFrictionCoefficients Steel;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FPortSurfaceDynamics SurfaceDynamics;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Utilization=.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float AGVEmptyMassKg=40000;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float DrivenWeightFraction=.5f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float TireRollingResistance=.012f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float RailRollingResistance=.002f;
    // Pre-lock downforce as a fraction of spreader weight. Hoist carries the remainder.
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float SeatingWeightFraction=.1f;
    void Sanitize();
    bool Load(const FString& Path);
};

namespace PortFriction
{
    constexpr double Gravity=9.80665;
    // Friction-circle residual acceleration, after external force and rolling resistance.
    PORTSIM_API double LongitudinalBudget(double GripMps2,double LateralMps2);
    PORTSIM_API double SeatingDemand(double MassKg,double RelativeSpeedMps,double ToleranceM,
        double TargetAccelerationMps2,double TiltDegrees,double WindForceN);
    PORTSIM_API bool CanSeat(double DemandN,double CapacityN,bool MechanicallyLocked);
}
