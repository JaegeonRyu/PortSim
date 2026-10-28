#include "WeatherFriction.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
    float Bounded(float V,float Low,float High,float Fallback)
    { return FMath::IsFinite(V)?FMath::Clamp(V,Low,High):Fallback; }
    float Blend(float Dry,float Wet,float Snow,float Ice,FPortSurfaceState S)
    {
        S.Sanitize();
        return FMath::Lerp(FMath::Lerp(FMath::Lerp(Dry,Wet,S.Wetness),Snow,S.Snow),Ice,S.Ice);
    }
}
void FPortSurfaceState::Sanitize()
{ Wetness=Bounded(Wetness,0,1,0); Snow=Bounded(Snow,0,1,0); Ice=Bounded(Ice,0,1,0); }
void FPortFrictionCoefficients::Sanitize()
{
    DryStatic=Bounded(DryStatic,.001f,1.5f,.4f);
    WetStatic=Bounded(WetStatic,.001f,DryStatic,FMath::Min(.2f,DryStatic));
    SnowStatic=Bounded(SnowStatic,.001f,WetStatic,FMath::Min(.12f,WetStatic));
    IceStatic=Bounded(IceStatic,.001f,SnowStatic,FMath::Min(.05f,SnowStatic));
    DryKinetic=Bounded(DryKinetic,.001f,DryStatic,DryStatic);
    WetKinetic=Bounded(WetKinetic,.001f,FMath::Min(WetStatic,DryKinetic),FMath::Min(WetStatic,DryKinetic));
    SnowKinetic=Bounded(SnowKinetic,.001f,FMath::Min(SnowStatic,WetKinetic),FMath::Min(SnowStatic,WetKinetic));
    IceKinetic=Bounded(IceKinetic,.001f,FMath::Min(IceStatic,SnowKinetic),FMath::Min(IceStatic,SnowKinetic));
}
float FPortFrictionCoefficients::Static(const FPortSurfaceState& S) const
{ auto C=*this; C.Sanitize(); return Blend(C.DryStatic,C.WetStatic,C.SnowStatic,C.IceStatic,S); }
float FPortFrictionCoefficients::Kinetic(const FPortSurfaceState& S) const
{ auto C=*this; C.Sanitize(); return Blend(C.DryKinetic,C.WetKinetic,C.SnowKinetic,C.IceKinetic,S); }
FPortFrictionSettings::FPortFrictionSettings()
{
    Tire.DryStatic=.8f; Tire.WetStatic=.5f; Tire.SnowStatic=.2f; Tire.IceStatic=.08f;
    Tire.DryKinetic=.65f; Tire.WetKinetic=.4f; Tire.SnowKinetic=.15f; Tire.IceKinetic=.05f;
    Rail.DryStatic=.25f; Rail.WetStatic=.12f; Rail.SnowStatic=.07f; Rail.IceStatic=.03f;
    Rail.DryKinetic=.2f; Rail.WetKinetic=.09f; Rail.SnowKinetic=.05f; Rail.IceKinetic=.02f;
}
void FPortFrictionSettings::Sanitize()
{
    Tire.Sanitize(); Rail.Sanitize(); Steel.Sanitize();
    SurfaceDynamics.Sanitize();
    Utilization=Bounded(Utilization,.1f,1,.8f); AGVEmptyMassKg=Bounded(AGVEmptyMassKg,1000,200000,40000);
    DrivenWeightFraction=Bounded(DrivenWeightFraction,.1f,1,.5f);
    TireRollingResistance=Bounded(TireRollingResistance,0,.1f,.012f);
    RailRollingResistance=Bounded(RailRollingResistance,0,.02f,.002f);
    SeatingWeightFraction=Bounded(SeatingWeightFraction,.01f,1,.1f);
}
bool FPortFrictionSettings::Load(const FString& Path)
{
    FString Text; TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text,*Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root.IsValid()) return false;
    auto Number=[](const TSharedPtr<FJsonObject>& O,const TCHAR* Key,float& Value)
    { double N; if(O->TryGetNumberField(Key,N)) Value=float(N); };
    auto Coeff=[&](const TCHAR* Key,FPortFrictionCoefficients& C)
    {
        const TSharedPtr<FJsonObject>* O=nullptr; if(!Root->TryGetObjectField(Key,O)) return;
        Number(*O,TEXT("dry_static"),C.DryStatic); Number(*O,TEXT("wet_static"),C.WetStatic);
        Number(*O,TEXT("snow_static"),C.SnowStatic); Number(*O,TEXT("ice_static"),C.IceStatic);
        Number(*O,TEXT("dry_kinetic"),C.DryKinetic); Number(*O,TEXT("wet_kinetic"),C.WetKinetic);
        Number(*O,TEXT("snow_kinetic"),C.SnowKinetic); Number(*O,TEXT("ice_kinetic"),C.IceKinetic);
    };
    Coeff(TEXT("tire"),Tire); Coeff(TEXT("rail"),Rail); Coeff(TEXT("steel"),Steel);
    const TSharedPtr<FJsonObject>* Dynamics=nullptr;
    if(Root->TryGetObjectField(TEXT("surface_dynamics"),Dynamics))
    {
        auto& D=SurfaceDynamics;
        Number(*Dynamics,TEXT("full_wet_water_mm"),D.FullWetWaterMm);
        Number(*Dynamics,TEXT("max_water_film_mm"),D.MaxWaterFilmMm);
        Number(*Dynamics,TEXT("drain_half_life_seconds"),D.DrainHalfLifeSeconds);
        Number(*Dynamics,TEXT("evaporation_mm_per_hour"),D.EvaporationMmPerHour);
        Number(*Dynamics,TEXT("full_snow_depth_mm"),D.FullSnowDepthMm);
        Number(*Dynamics,TEXT("max_snow_depth_mm"),D.MaxSnowDepthMm);
        Number(*Dynamics,TEXT("maximum_snowfall_mm_per_hour"),D.MaximumSnowfallMmPerHour);
        Number(*Dynamics,TEXT("melt_reference_temperature_celsius"),D.MeltReferenceTemperatureCelsius);
        Number(*Dynamics,TEXT("snow_melt_mm_per_hour"),D.SnowMeltMmPerHour);
        Number(*Dynamics,TEXT("snow_water_ratio"),D.SnowWaterRatio);
        Number(*Dynamics,TEXT("ice_build_seconds"),D.IceBuildSeconds);
        Number(*Dynamics,TEXT("ice_melt_seconds"),D.IceMeltSeconds);
        Number(*Dynamics,TEXT("ice_water_equivalent_mm"),D.IceWaterEquivalentMm);
    }
    Number(Root,TEXT("utilization"),Utilization); Number(Root,TEXT("agv_empty_mass_kg"),AGVEmptyMassKg);
    Number(Root,TEXT("driven_weight_fraction"),DrivenWeightFraction);
    Number(Root,TEXT("tire_rolling_resistance"),TireRollingResistance);
    Number(Root,TEXT("rail_rolling_resistance"),RailRollingResistance);
    Number(Root,TEXT("seating_weight_fraction"),SeatingWeightFraction);
    Sanitize(); return true;
}
double PortFriction::LongitudinalBudget(double Grip,double Lateral)
{ return FMath::Sqrt(FMath::Max(0.,Grip*Grip-Lateral*Lateral)); }
double PortFriction::SeatingDemand(double Mass,double Speed,double Tolerance,double Acceleration,double Tilt,double Wind)
{
    return Mass*(Speed*Speed/(2*FMath::Max(.001,Tolerance))+FMath::Abs(Acceleration)+
        Gravity*FMath::Sin(FMath::DegreesToRadians(FMath::Abs(Tilt))))+FMath::Abs(Wind);
}
bool PortFriction::CanSeat(double Demand,double Capacity,bool Locked)
{ return Locked || (FMath::IsFinite(Demand) && FMath::IsFinite(Capacity) && Capacity>=0 && Demand<=Capacity); }

void FPortSurfaceDynamics::Sanitize()
{
    FullWetWaterMm=Bounded(FullWetWaterMm,.01f,100,.5f);
    MaxWaterFilmMm=Bounded(MaxWaterFilmMm,FullWetWaterMm,1000,FMath::Max(5.f,FullWetWaterMm));
    DrainHalfLifeSeconds=Bounded(DrainHalfLifeSeconds,1,864000,600);
    EvaporationMmPerHour=Bounded(EvaporationMmPerHour,0,100,.5f);
    FullSnowDepthMm=Bounded(FullSnowDepthMm,.1f,1000,10);
    MaxSnowDepthMm=Bounded(MaxSnowDepthMm,FullSnowDepthMm,10000,FMath::Max(500.f,FullSnowDepthMm));
    MaximumSnowfallMmPerHour=Bounded(MaximumSnowfallMmPerHour,1,1000,100);
    MeltReferenceTemperatureCelsius=Bounded(MeltReferenceTemperatureCelsius,.1f,40,5);
    SnowMeltMmPerHour=Bounded(SnowMeltMmPerHour,0,1000,20);
    SnowWaterRatio=Bounded(SnowWaterRatio,.01f,1,.1f);
    IceBuildSeconds=Bounded(IceBuildSeconds,1,864000,120);
    IceMeltSeconds=Bounded(IceMeltSeconds,1,864000,1800);
    IceWaterEquivalentMm=Bounded(IceWaterEquivalentMm,0,100,1);
}
void FPortSurfaceAccumulation::Initialize(const FPortSurfaceState& Initial,const FPortSurfaceDynamics& Settings)
{
    auto S=Initial; S.Sanitize(); auto D=Settings; D.Sanitize();
    WaterMm=S.Wetness*D.FullWetWaterMm; SnowMm=S.Snow*D.FullSnowDepthMm; Ice=S.Ice;
    WaterRate=SnowRate=0;
}
FPortSurfaceState FPortSurfaceAccumulation::Sample(const FPortSurfaceDynamics& Settings) const
{
    auto D=Settings; D.Sanitize(); FPortSurfaceState S;
    S.Wetness=FMath::Clamp(WaterMm/D.FullWetWaterMm,0.,1.);
    S.Snow=FMath::Clamp(SnowMm/D.FullSnowDepthMm,0.,1.); S.Ice=FMath::Clamp(Ice,0.,1.);
    S.WaterFilmMm=WaterMm; S.SnowDepthMm=SnowMm;
    S.WaterChangeMmPerHour=WaterRate; S.SnowChangeMmPerHour=SnowRate;
    return S;
}
void FPortSurfaceAccumulation::Advance(const FPortSurfaceDynamics& Settings,double Rain,double Snowfall,double Icing,double Seconds,double TemperatureCelsius)
{
    if(!FMath::IsFinite(Seconds) || Seconds<=0) return;
    auto D=Settings; D.Sanitize();
    Rain=FMath::IsFinite(Rain)?FMath::Clamp(Rain,0.,1000.):0;
    Snowfall=FMath::IsFinite(Snowfall)?FMath::Clamp(Snowfall,0.,1000.):0;
    Icing=FMath::IsFinite(Icing)?FMath::Clamp(Icing,0.,1.):0;
    if(!FMath::IsFinite(TemperatureCelsius)) return;
    const double Temperature=FMath::Clamp(TemperatureCelsius,-25.,40.);
    const double MeltScale=FMath::Max(0.,Temperature)/D.MeltReferenceTemperatureCelsius;
    // Icing is an imposed cold-weather scenario. Warm air overrides its target;
    // switching it off in freezing conditions does not erase existing ice.
    if(Temperature>0) Icing=0;
    const double OldWater=WaterMm,OldSnow=SnowMm;
    // Events: snow reservoir is exhausted, ice reaches its current target.
    // Split there so fast playback and small steps produce the same water balance.
    for(double Remaining=Seconds;Remaining>1.e-9;)
    {
        const double SnowInput=Snowfall/3600.;
        const double MeltCapacity=D.SnowMeltMmPerHour/3600.*MeltScale;
        const double Melt=SnowMm>0?MeltCapacity:FMath::Min(SnowInput,MeltCapacity);
        const double SnowVelocity=SnowInput-Melt;
        const double IceVelocity=Temperature<=0 && Ice<Icing?1./D.IceBuildSeconds:
            (Temperature>0 && Ice>0?-MeltScale/D.IceMeltSeconds:0);
        const double SnowEvent=SnowVelocity<0?SnowMm/(-SnowVelocity):Remaining;
        const double IceEvent=IceVelocity!=0?FMath::Abs((Icing-Ice)/IceVelocity):Remaining;
        const double Step=FMath::Min3(Remaining,SnowEvent,IceEvent);
        if(Step<=1.e-9)
        {
            if(SnowEvent<=1.e-9) SnowMm=0;
            if(IceEvent<=1.e-9) Ice=Icing;
            continue;
        }
        const double WaterInput=Rain/3600.+Melt*D.SnowWaterRatio-
            IceVelocity*D.IceWaterEquivalentMm-D.EvaporationMmPerHour/3600.;
        const double Drain=FMath::Loge(2.)/D.DrainHalfLifeSeconds;
        const double Decay=FMath::Exp(-Drain*Step);
        WaterMm=FMath::Clamp(WaterMm*Decay+(WaterInput/Drain)*(1.-Decay),0.,double(D.MaxWaterFilmMm));
        SnowMm=FMath::Clamp(SnowMm+SnowVelocity*Step,0.,double(D.MaxSnowDepthMm));
        Ice=FMath::Clamp(Ice+IceVelocity*Step,0.,1.);
        if(Step==SnowEvent && SnowVelocity<0) SnowMm=0;
        if(Step==IceEvent && IceVelocity!=0) Ice=Icing;
        Remaining-=Step;
    }
    WaterRate=(WaterMm-OldWater)*3600./Seconds;
    SnowRate=(SnowMm-OldSnow)*3600./Seconds;
}
