#include "PortEnvironmentComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"

namespace PortSea
{
    float Triangular(FRandomStream& Random,float Minimum,float Mode,float Maximum)
    {
        const float U=Random.FRand(),Width=Maximum-Minimum,ModeFraction=(Mode-Minimum)/Width;
        return U<ModeFraction?Minimum+FMath::Sqrt(U*Width*(Mode-Minimum)):
            Maximum-FMath::Sqrt((1-U)*Width*(Maximum-Mode));
    }
}

void FPortSeaState::Initialize(int32 Seed)
{
    FRandomStream Random(Seed^0x51EA51EA);
    bSeaMotionEnabled=true;
    WaveHeightMeters=PortSea::Triangular(Random,MinimumWaveHeightMeters,TypicalWaveHeightMeters,MaximumWaveHeightMeters);
    SwellHeightMeters=PortSea::Triangular(Random,MinimumSwellHeightMeters,TypicalSwellHeightMeters,MaximumSwellHeightMeters);
    WavePeriodSeconds=Random.FRandRange(4.5f,7.f);
    SwellPeriodSeconds=Random.FRandRange(8.f,13.f);
    // Local compass convention only. This is not a georeferenced forecast.
    WaveDirectionDegrees=Random.FRandRange(105.f,165.f);
    SwellDirectionDegrees=Random.FRandRange(120.f,180.f);
    WavePhaseRadians=Random.FRandRange(0.f,2*PI);
    SwellPhaseRadians=Random.FRandRange(0.f,2*PI);
    ElapsedSeconds=0;
}

void FPortSeaState::Advance(double SimulationSeconds)
{
    if(!bSeaMotionEnabled || !FMath::IsFinite(SimulationSeconds) || SimulationSeconds<=0) return;
    ElapsedSeconds+=SimulationSeconds;
}

FPortVesselMotion FPortSeaState::SampleVesselMotion(int32 VesselIndex) const
{
    FPortVesselMotion Result;
    if(!bSeaMotionEnabled) return Result;
    const double WaveOmega=2*PI/FMath::Max(.1f,WavePeriodSeconds);
    const double SwellOmega=2*PI/FMath::Max(.1f,SwellPeriodSeconds);
    const double WavePhase=WavePhaseRadians+ElapsedSeconds*WaveOmega+VesselIndex*.67;
    const double SwellPhase=SwellPhaseRadians+ElapsedSeconds*SwellOmega+VesselIndex*.31;
    const double WaveSin=FMath::Sin(WavePhase),WaveCos=FMath::Cos(WavePhase);
    const double SwellSin=FMath::Sin(SwellPhase),SwellCos=FMath::Cos(SwellPhase);
    const FVector WaveFlow=FPortWindState::FlowDirection(WaveDirectionDegrees);
    const FVector SwellFlow=FPortWindState::FlowDirection(SwellDirectionDegrees);
    const double WaveHorizontal=.025*WaveHeightMeters, SwellHorizontal=.04*SwellHeightMeters;
    const double WaveHeave=.15*WaveHeightMeters, SwellHeave=.22*SwellHeightMeters;
    const FVector TranslationMeters=WaveFlow*WaveHorizontal*WaveSin+SwellFlow*SwellHorizontal*SwellSin+
        FVector(0,0,WaveHeave*WaveSin+SwellHeave*SwellSin);
    const FVector VelocityMeters=WaveFlow*WaveHorizontal*WaveOmega*WaveCos+
        SwellFlow*SwellHorizontal*SwellOmega*SwellCos+
        FVector(0,0,WaveHeave*WaveOmega*WaveCos+SwellHeave*SwellOmega*SwellCos);
    // Vessel length is world Y: roll is about Y and pitch about X.
    const double RollDegrees=.45*WaveHeightMeters*FMath::Sin(WavePhase+.35)+
        .65*SwellHeightMeters*FMath::Sin(SwellPhase+.20);
    const double PitchDegrees=.18*WaveHeightMeters*FMath::Sin(WavePhase-.25)+
        .25*SwellHeightMeters*FMath::Sin(SwellPhase-.15);
    const double RollRateRadians=FMath::DegreesToRadians(.45*WaveHeightMeters*WaveOmega*FMath::Cos(WavePhase+.35)+
        .65*SwellHeightMeters*SwellOmega*FMath::Cos(SwellPhase+.20));
    const double PitchRateRadians=FMath::DegreesToRadians(.18*WaveHeightMeters*WaveOmega*FMath::Cos(WavePhase-.25)+
        .25*SwellHeightMeters*SwellOmega*FMath::Cos(SwellPhase-.15));
    Result.TranslationCentimeters=TranslationMeters*100.;
    Result.LinearVelocityCentimetersPerSecond=VelocityMeters*100.;
    Result.Rotation=FQuat(FVector::XAxisVector,FMath::DegreesToRadians(PitchDegrees))*
        FQuat(FVector::YAxisVector,FMath::DegreesToRadians(RollDegrees));
    Result.AngularVelocityRadiansPerSecond=FVector(PitchRateRadians,RollRateRadians,0);
    Result.HeaveMeters=TranslationMeters.Z;
    Result.RollDegrees=RollDegrees;
    Result.PitchDegrees=PitchDegrees;
    return Result;
}

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

void FPortWindState::ScheduleNextGust(bool Initial)
{
    SecondsUntilGust=Initial?GustRandom.FRandRange(30.f,90.f):
        GustRandom.FRandRange(MinimumGustInterval,MaximumGustInterval);
}

void FPortWindState::StartGust()
{
    bGustActive=true;
    GustElapsedSeconds=0;
    GustDurationSeconds=GustRandom.FRandRange(MinimumGustDuration,MaximumGustDuration);
    GustPeakSpeedMetersPerSecond=GustRandom.FRandRange(MinimumGustPeak,MaximumGustPeak);
    GustDirectionOffsetDegrees=GustRandom.FRandRange(-MaximumGustDirectionOffset,MaximumGustDirectionOffset);
    GustDirectionDegrees=NormalizeDirection(WindDirectionDegrees+GustDirectionOffsetDegrees);
    GustSpeedMetersPerSecond=0;
    ++GustCount;
}

float FPortWindState::GustEnvelope(double Phase)
{
    // 25% smooth attack, 40% peak plateau and 35% smooth decay. At the
    // minimum six-second duration the plateau is 2.4 seconds, close to the
    // standard three-second maximum-gust observation without an impulse.
    Phase=FMath::Clamp(Phase,0.,1.);
    auto Smooth=[](double X){return X*X*(3.-2.*X);};
    if(Phase<.25) return Smooth(Phase/.25);
    if(Phase<=.65) return 1.f;
    return Smooth((1.-Phase)/.35);
}

void FPortWindState::Initialize(int32 Seed)
{
    Random.Initialize(Seed);
    // Keep gust scheduling independent so enabling gusts never changes the
    // pre-existing mean-wind sequence for a given seed.
    GustRandom.Initialize(Seed^0x6D2B79F5);
    bWindEnabled=true;
    WindSpeedMetersPerSecond=SampleSpeed();
    WindDirectionDegrees=NormalizeDirection(Random.FRandRange(0.f,360.f));
    WindDirectionVector=FlowDirection(WindDirectionDegrees);
    TargetWindSpeedMetersPerSecond=WindSpeedMetersPerSecond;
    TargetWindDirectionDegrees=WindDirectionDegrees;
    SecondsUntilTarget=Random.FRandRange(20.f,60.f);
    bGustActive=false;
    GustSpeedMetersPerSecond=0;
    GustPeakSpeedMetersPerSecond=0;
    GustDirectionDegrees=WindDirectionDegrees;
    GustDirectionOffsetDegrees=0;
    GustElapsedSeconds=GustDurationSeconds=0;
    GustCount=0;
    ScheduleNextGust(true);
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

void FPortWindState::AdvanceMean(double SimulationSeconds)
{
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

void FPortWindState::AdvanceGust(double SimulationSeconds)
{
    while(SimulationSeconds>0)
    {
        if(!bGustActive)
        {
            if(SecondsUntilGust<=UE_SMALL_NUMBER)
            {
                StartGust();
                continue;
            }
            const double Step=FMath::Min(SimulationSeconds,SecondsUntilGust);
            SecondsUntilGust-=Step;
            SimulationSeconds-=Step;
            continue;
        }
        const double Remaining=FMath::Max(0.,GustDurationSeconds-GustElapsedSeconds);
        const double Step=FMath::Min(SimulationSeconds,Remaining);
        GustElapsedSeconds+=Step;
        SimulationSeconds-=Step;
        GustSpeedMetersPerSecond=GustPeakSpeedMetersPerSecond*GustEnvelope(
            GustDurationSeconds>0?GustElapsedSeconds/GustDurationSeconds:1.);
        if(GustElapsedSeconds+UE_SMALL_NUMBER>=GustDurationSeconds)
        {
            bGustActive=false;
            GustSpeedMetersPerSecond=0;
            GustElapsedSeconds=GustDurationSeconds;
            ScheduleNextGust(false);
        }
    }
}

void FPortWindState::Advance(double SimulationSeconds)
{
    // OFF freezes mean wind, gust state and both schedules. Toggle/reset never
    // rerolls weather.
    if(!bWindEnabled || !FMath::IsFinite(SimulationSeconds) || SimulationSeconds<=0) return;
    AdvanceMean(SimulationSeconds);
    AdvanceGust(SimulationSeconds);
    GustDirectionDegrees=NormalizeDirection(WindDirectionDegrees+GustDirectionOffsetDegrees);
}

FVector FPortWindState::EffectiveVelocity() const
{
    if(!bWindEnabled) return FVector::ZeroVector;
    return WindDirectionVector*WindSpeedMetersPerSecond+
        FlowDirection(GustDirectionDegrees)*GustSpeedMetersPerSecond;
}

float FPortWindState::EffectiveDirectionDegrees() const
{
    const FVector Velocity=EffectiveVelocity();
    if(Velocity.IsNearlyZero()) return WindDirectionDegrees;
    // Velocity points TO; meteorological bearing points FROM.
    return NormalizeDirection(FMath::RadiansToDegrees(FMath::Atan2(-Velocity.Y,-Velocity.X)));
}

UPortEnvironmentComponent::UPortEnvironmentComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
}

void UPortEnvironmentComponent::BeginPlay()
{
    Super::BeginPlay();
    int32 Seed=static_cast<int32>(FGuid::NewGuid().A);
    FParse::Value(FCommandLine::Get(),TEXT("PortSimEnvironmentSeed="),Seed);
    Wind.Initialize(Seed);
    Sea.Initialize(Seed);
    LastReportedGustCount=Wind.GustCount;
    UE_LOG(LogTemp,Display,TEXT("PORTSIM_SEA_STATE: seed=%d Hs_m=%.3f wave_m=%.3f wave_period_s=%.3f swell_m=%.3f swell_period_s=%.3f"),
        Seed,Sea.SignificantWaveHeightMeters(),Sea.WaveHeightMeters,Sea.WavePeriodSeconds,
        Sea.SwellHeightMeters,Sea.SwellPeriodSeconds);
}

void UPortEnvironmentComponent::AdvanceEnvironment(float SimulationSeconds)
{
    Wind.Advance(SimulationSeconds);
    Sea.Advance(SimulationSeconds);
    if(Wind.GustCount!=LastReportedGustCount)
    {
        LastReportedGustCount=Wind.GustCount;
        UE_LOG(LogTemp,Display,TEXT("PORTSIM_GUST_START: count=%d mean_mps=%.3f increment_mps=%.3f duration_s=%.3f from_deg=%.3f"),
            Wind.GustCount,Wind.WindSpeedMetersPerSecond,Wind.GustPeakSpeedMetersPerSecond,
            Wind.GustDurationSeconds,Wind.GustDirectionDegrees);
    }
}

FVector UPortEnvironmentComponent::GetEffectiveWindVelocity() const
{
    return Wind.EffectiveVelocity();
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
