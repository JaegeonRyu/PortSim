#include "PortEnvironmentComponent.h"
#include "Misc/Guid.h"

float FPortWindState::NormalizeDirection(float Degrees)
{
    return FMath::Fmod(FMath::Fmod(Degrees,360.f)+360.f,360.f);
}

float FPortWindState::SampleSpeed()
{
    // Triangular distribution: 1..7 m/s, mode 3.5 m/s (normal-weather assumption).
    // Unlike a uniform distribution, the 3..4 m/s band is the most frequent.
    const float U=Random.FRand();
    constexpr float Width=MaximumSpeed-MinimumSpeed;
    constexpr float ModeFraction=(TypicalSpeed-MinimumSpeed)/Width;
    return FMath::Clamp(U<ModeFraction?
        MinimumSpeed+FMath::Sqrt(U*Width*(TypicalSpeed-MinimumSpeed)):
        MaximumSpeed-FMath::Sqrt((1-U)*Width*(MaximumSpeed-TypicalSpeed)),MinimumSpeed,MaximumSpeed);
}

void FPortWindState::SelectTarget()
{
    TargetWindSpeedMetersPerSecond=SampleSpeed();
    TargetWindDirectionDegrees=NormalizeDirection(WindDirectionDegrees+Random.FRandRange(-60.f,60.f));
    SecondsUntilTarget=Random.FRandRange(20.f,60.f);
}

void FPortWindState::Initialize(int32 Seed)
{
    Random.Initialize(Seed);
    bWindEnabled=true;
    WindSpeedMetersPerSecond=SampleSpeed();
    WindDirectionDegrees=NormalizeDirection(Random.FRandRange(0.f,360.f));
    WindDirectionVector=FlowDirection(WindDirectionDegrees);
    TargetWindSpeedMetersPerSecond=WindSpeedMetersPerSecond;
    TargetWindDirectionDegrees=WindDirectionDegrees;
    SecondsUntilTarget=Random.FRandRange(20.f,60.f);
}

float FPortWindState::InterpolateDirection(float Current,float Target,double Seconds)
{
    const double Alpha=1.-FMath::Exp(-FMath::Max(0.,Seconds)/12.);
    return NormalizeDirection(Current+FMath::FindDeltaAngleDegrees(Current,Target)*Alpha);
}

FVector FPortWindState::FlowDirection(float FromDegrees)
{
    const double Radians=FMath::DegreesToRadians(double(FromDegrees));
    return FVector(-FMath::Cos(Radians),-FMath::Sin(Radians),0);
}

FString FPortWindState::DirectionName(float Degrees)
{
    static const TCHAR* Names[]={TEXT("N"),TEXT("NNE"),TEXT("NE"),TEXT("ENE"),TEXT("E"),TEXT("ESE"),TEXT("SE"),TEXT("SSE"),
        TEXT("S"),TEXT("SSW"),TEXT("SW"),TEXT("WSW"),TEXT("W"),TEXT("WNW"),TEXT("NW"),TEXT("NNW")};
    return Names[FMath::FloorToInt((NormalizeDirection(Degrees)+11.25f)/22.5f)%16];
}

void FPortWindState::Advance(double SimulationSeconds)
{
    // OFF freezes raw values, targets and schedule. Toggle/reset never rerolls weather.
    if(!bWindEnabled || !FMath::IsFinite(SimulationSeconds) || SimulationSeconds<=0) return;
    while(SimulationSeconds>0)
    {
        if(SecondsUntilTarget<=0) SelectTarget();
        // Split exactly at target boundaries: equal simulated time gives equal weather
        // across frame rates and playback rates. Exponential smoothing cannot overshoot.
        const double Step=FMath::Min(SimulationSeconds,SecondsUntilTarget);
        const double Alpha=1.-FMath::Exp(-Step/8.);
        WindSpeedMetersPerSecond=FMath::Clamp(float(WindSpeedMetersPerSecond+
            (TargetWindSpeedMetersPerSecond-WindSpeedMetersPerSecond)*Alpha),MinimumSpeed,MaximumSpeed);
        WindDirectionDegrees=InterpolateDirection(WindDirectionDegrees,TargetWindDirectionDegrees,Step);
        SecondsUntilTarget-=Step;
        SimulationSeconds-=Step;
    }
    WindDirectionVector=FlowDirection(WindDirectionDegrees);
}

UPortEnvironmentComponent::UPortEnvironmentComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
}

void UPortEnvironmentComponent::BeginPlay()
{
    Super::BeginPlay();
    Wind.Initialize(static_cast<int32>(FGuid::NewGuid().A));
}

void UPortEnvironmentComponent::AdvanceEnvironment(float SimulationSeconds)
{
    Wind.Advance(SimulationSeconds);
}

FVector UPortEnvironmentComponent::GetEffectiveWindVelocity() const
{
    return Wind.bWindEnabled?Wind.WindDirectionVector*Wind.WindSpeedMetersPerSecond:FVector::ZeroVector;
}

FVector FPortWindAerodynamics::DragForceNewtons(FVector WindVelocityMetersPerSecond,FVector BodyVelocityCentimetersPerSecond,
    FQuat BodyRotation,FVector DimensionsMeters,double DragCoefficient)
{
    if(WindVelocityMetersPerSecond.ContainsNaN() || BodyVelocityCentimetersPerSecond.ContainsNaN() ||
        DimensionsMeters.ContainsNaN() || !FMath::IsFinite(DragCoefficient) || DragCoefficient<=0) return FVector::ZeroVector;
    const FVector RelativeWind=WindVelocityMetersPerSecond-BodyVelocityCentimetersPerSecond*.01;
    const double Speed=RelativeWind.Size();
    if(Speed<=KINDA_SMALL_NUMBER) return FVector::ZeroVector;
    const FVector Direction=RelativeWind/Speed;
    const FVector LocalDirection=BodyRotation.UnrotateVector(Direction).GetAbs();
    const FVector Size=DimensionsMeters.GetAbs();
    const double ProjectedArea=LocalDirection.X*Size.Y*Size.Z+
        LocalDirection.Y*Size.X*Size.Z+LocalDirection.Z*Size.X*Size.Y;
    return .5*AirDensityKgPerCubicMeter*DragCoefficient*ProjectedArea*Speed*RelativeWind;
}
