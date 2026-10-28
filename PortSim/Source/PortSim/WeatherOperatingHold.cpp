#include "PortEnvironmentComponent.h"

void FPortWeatherHold::Update(const UPortEnvironmentComponent& E,float Seconds)
{
    const auto& L=E.OperatingLimits;
    const bool Recovering=Active;
    FString Unsafe;
    auto Check=[&](bool Failed,const TCHAR* Message){ if(Failed && Unsafe.IsEmpty()) Unsafe=Message; };
    Check(E.GetEffectiveWindSpeedMetersPerSecond()>=(Recovering?L.ResumeWindMps:L.StopWindMps),TEXT("Strong wind"));
    Check(E.IsFogEnabled() && E.GetFogVisibilityMeters()<=(Recovering?L.ResumeVisibilityM:L.StopVisibilityM),TEXT("Low visibility"));
    Check(E.IsRainEnabled() && E.GetRainfallMmPerHour()>=(Recovering?L.ResumeRainMmPerHour:L.StopRainMmPerHour),TEXT("Heavy rain"));
    Check(E.IsSnowEnabled() && E.GetSnowfallMmPerHour()>=(Recovering?L.ResumeSnowMmPerHour:L.StopSnowMmPerHour),TEXT("Heavy snow"));
    const auto Surface=E.GetSurfaceConditions();
    Check(Surface.Ice>=(Recovering?L.ResumeIce:L.StopIce),TEXT("Icy surface"));
    Check(E.GetFrictionSettings().Tire.Static(Surface)<=(Recovering?L.ResumeTireMu:L.StopTireMu),TEXT("Insufficient tire grip"));
    Check(E.IsSeaMotionEnabled() && E.GetSignificantWaveHeightMeters()>=(Recovering?L.ResumeWaveHeightM:L.StopWaveHeightM),TEXT("High waves"));
    if(!Unsafe.IsEmpty()) { Active=true; RecoveryElapsed=0; Reason=Unsafe; return; }
    if(!Active) return;
    if(FMath::IsFinite(Seconds) && Seconds>0) RecoveryElapsed+=Seconds;
    if(RecoveryElapsed>=FMath::Max(0.f,L.RecoverySeconds)) { Active=false; RecoveryElapsed=0; Reason.Empty(); }
}
