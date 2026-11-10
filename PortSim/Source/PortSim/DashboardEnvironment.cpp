#include "PortSiteLogistics.h"
#include "PortEnvironmentComponent.h"
#include "PortSimulationClock.h"
#include "PortSolarLighting.h"
#include "Dom/JsonObject.h"

TSharedPtr<FJsonObject> APortSiteLogistics::CaptureEnvironment() const
{
    const auto* E=GetOwner()?GetOwner()->FindComponentByClass<UPortEnvironmentComponent>():nullptr;
    if(!E) return nullptr;
    auto O=MakeShared<FJsonObject>();
    const FDateTime LocalTime=E->GetLocalDateTime();
    const double TimeOfDay=LocalTime.GetTimeOfDay().GetTotalSeconds();
    const FPortSolarPosition Sun=PortSolarLighting::Calculate(LocalTime);
    O->SetStringField(TEXT("local_datetime"),LocalTime.ToString(TEXT("%Y-%m-%dT%H:%M:%S"))+TEXT("+09:00"));
    O->SetNumberField(TEXT("sun_elevation_deg"),Sun.ElevationDegrees);
    O->SetNumberField(TEXT("sun_azimuth_deg"),Sun.AzimuthDegrees);
    O->SetNumberField(TEXT("estimated_clear_sky_horizontal_lux"),Sun.EstimateHorizontalIlluminanceLux());
    O->SetNumberField(TEXT("time_of_day_seconds"),TimeOfDay);
    O->SetNumberField(TEXT("temperature_celsius"),PortSimulationClock::TemperatureCelsius(TimeOfDay,
        E->IsSnowEnabled(),E->GetSnowfallMmPerHour(),E->IsRainEnabled(),E->GetRainfallMmPerHour()));
    O->SetBoolField(TEXT("wind_enabled"),E->IsWindEnabled());
    O->SetNumberField(TEXT("wind_mps"),E->IsWindEnabled()?E->GetWindSpeedMetersPerSecond():0);
    O->SetNumberField(TEXT("effective_wind_mps"),E->GetEffectiveWindSpeedMetersPerSecond());
    O->SetNumberField(TEXT("wind_from_deg"),E->GetWindDirectionDegrees());
    O->SetNumberField(TEXT("effective_wind_from_deg"),E->GetEffectiveWindDirectionDegrees());
    O->SetBoolField(TEXT("gust_active"),E->IsGustActive());
    O->SetNumberField(TEXT("rain_mm_h"),E->IsRainEnabled()?E->GetRainfallMmPerHour():0);
    O->SetNumberField(TEXT("snow_mm_h"),E->IsSnowEnabled()?E->GetSnowfallMmPerHour():0);
    O->SetNumberField(TEXT("ice"),E->IsIceEnabled()?E->GetIceIntensity():0);
    O->SetNumberField(TEXT("fog_m"),E->IsFogEnabled()?E->GetFogVisibilityMeters():0);
    const auto S=E->GetSurfaceConditions(); const auto& F=E->GetFrictionSettings();
    O->SetNumberField(TEXT("water_film_mm"),S.WaterFilmMm);
    O->SetNumberField(TEXT("snow_depth_mm"),S.SnowDepthMm);
    O->SetNumberField(TEXT("wetness"),S.Wetness);
    O->SetNumberField(TEXT("snow_coverage"),S.Snow);
    O->SetNumberField(TEXT("ice_coverage"),S.Ice);
    O->SetNumberField(TEXT("water_change_mm_h"),S.WaterChangeMmPerHour);
    O->SetNumberField(TEXT("snow_change_mm_h"),S.SnowChangeMmPerHour);
    O->SetBoolField(TEXT("drying"),S.WaterChangeMmPerHour<0);
    O->SetNumberField(TEXT("tire_mu_static"),F.Tire.Static(S));
    O->SetNumberField(TEXT("tire_mu_kinetic"),F.Tire.Kinetic(S));
    O->SetNumberField(TEXT("rail_mu_static"),F.Rail.Static(S));
    O->SetNumberField(TEXT("wind_max_mps"),FPortWindState::MaximumSpeed);
    O->SetNumberField(TEXT("snow_max_mm_h"),F.SurfaceDynamics.MaximumSnowfallMmPerHour);
    O->SetNumberField(TEXT("water_max_mm"),F.SurfaceDynamics.MaxWaterFilmMm);
    O->SetNumberField(TEXT("snow_max_mm"),F.SurfaceDynamics.MaxSnowDepthMm);
    return O;
}
