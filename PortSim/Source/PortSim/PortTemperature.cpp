#include "PortEnvironmentComponent.h"

void UPortEnvironmentComponent::SetTemperatureCelsius(float Celsius)
{
    if(FMath::IsFinite(Celsius)) TemperatureSettingCelsius=FMath::Clamp(Celsius,MinimumTemperatureCelsius,MaximumTemperatureCelsius);
}

float UPortEnvironmentComponent::GetTemperatureHour() const
{
    const double Start=FMath::IsFinite(TemperatureStartHour)?FMath::Clamp(TemperatureStartHour,0.f,24.f):8.f;
    return float(FMath::Fmod(Start+TemperatureElapsedSeconds/3600.,24.));
}

float UPortEnvironmentComponent::GetTemperatureCelsius() const
{
    const float Base=FMath::IsFinite(TemperatureSettingCelsius)?TemperatureSettingCelsius:18.f;
    // Assumed daily air-temperature cycle: maximum 14:00, minimum 02:00.
    // No surface temperature or heat/energy balance is implied.
    const float Amplitude=FMath::IsFinite(DailyTemperatureAmplitudeCelsius)?FMath::Clamp(DailyTemperatureAmplitudeCelsius,0.f,20.f):4.f;
    const float Offset=bDailyTemperatureCycle?Amplitude*FMath::Cos((GetTemperatureHour()-14.f)*2.f*PI/24.f):0.f;
    return FMath::Clamp(Base+Offset,MinimumTemperatureCelsius,MaximumTemperatureCelsius);
}
