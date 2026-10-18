#include "QuayCrane.h"
#include "PortEnvironmentComponent.h"
#include "PortSiteLogistics.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
FString RunSettingsPath()
{
    FString Path=FPaths::ProjectSavedDir()/TEXT("SimulationSettings.json");
    FParse::Value(FCommandLine::Get(),TEXT("PortSimSettings="),Path);
    return Path;
}
}

int32 AQuayCrane::PendingCargoTotal() const
{ int32 Total=0; for(int32 I=0;I<PendingShips;++I) Total+=PendingCargo[I]; return Total; }
int32 AQuayCrane::PendingCargoLimit(int32 Vessel) const
{ return 5594-PendingCargoTotal()+PendingCargo[Vessel]; }
void AQuayCrane::SetPendingShips(int32 Count)
{
    const int32 Old=PendingShips;
    PendingShips=FMath::Clamp(Count,1,3);
    for(int32 I=Old;I<PendingShips;++I) PendingCargo[I]=1;
    int32 Excess=PendingCargoTotal()-5594;
    for(int32 I=0;I<PendingShips && Excess>0;++I)
    { const int32 Take=FMath::Min(Excess,PendingCargo[I]-1); PendingCargo[I]-=Take; Excess-=Take; }
}
void AQuayCrane::SetPendingCargo(int32 Vessel,int32 Count)
{
    if(Vessel>=0 && Vessel<PendingShips)
        PendingCargo[Vessel]=FMath::Clamp(Count,1,PendingCargoLimit(Vessel));
}
void AQuayCrane::LoadRunSettings()
{
    FString Json; TSharedPtr<FJsonObject> Object;
    if(FFileHelper::LoadFileToString(Json,*RunSettingsPath()) &&
        FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Object) && Object.IsValid())
    {
        double Ships=3; Object->TryGetNumberField(TEXT("ships"),Ships);
        PendingShips=FMath::IsFinite(Ships)?int32(FMath::Clamp(Ships,1.,3.)):3;
        int32 Budget=5594;
        for(int32 I=0;I<PendingShips;++I)
        {
            double Count=200; Object->TryGetNumberField(FString::Printf(TEXT("cargo_%d"),I+1),Count);
            PendingCargo[I]=FMath::IsFinite(Count)?int32(FMath::Clamp(Count,1.,double(Budget-(PendingShips-I-1)))):1;
            Budget-=PendingCargo[I];
        }
        if(Environment && !FParse::Param(FCommandLine::Get(),TEXT("unattended")))
        {
            bool Enabled=false; double Value=0;
            if(Object->TryGetBoolField(TEXT("wind_enabled"),Enabled)) Environment->SetWindEnabled(Enabled);
            if(Object->TryGetNumberField(TEXT("wind_speed_mps"),Value)) Environment->SetWindSpeedMetersPerSecond(Value);
            if(Object->TryGetBoolField(TEXT("sea_enabled"),Enabled)) Environment->SetSeaMotionEnabled(Enabled);
            if(Object->TryGetNumberField(TEXT("sea_intensity"),Value)) Environment->SetSeaIntensity(Value);
            if(Object->TryGetBoolField(TEXT("rain_enabled"),Enabled)) Environment->SetRainEnabled(Enabled);
            if(Object->TryGetNumberField(TEXT("rainfall_mm_per_hour"),Value)) Environment->SetRainfallMmPerHour(Value);
            if(Object->TryGetBoolField(TEXT("fog_enabled"),Enabled)) Environment->SetFogEnabled(Enabled);
            if(Object->TryGetNumberField(TEXT("fog_visibility_m"),Value)) Environment->SetFogVisibilityMeters(Value);
        }
    }
    // Command-line overrides allow repeatable tests without changing saved settings.
    int32 Ships=PendingShips;
    FParse::Value(FCommandLine::Get(),TEXT("PortSimShips="),Ships); SetPendingShips(Ships);
    for(int32 I=0;I<PendingShips;++I)
    {
        int32 Count=PendingCargo[I];
        if(FParse::Value(FCommandLine::Get(),*FString::Printf(TEXT("PortSimCargo%d="),I+1),Count)) SetPendingCargo(I,Count);
    }
    if(Environment)
    {
        float Value=0;
        if(FParse::Value(FCommandLine::Get(),TEXT("PortSimWindSpeed="),Value)) Environment->SetWindSpeedMetersPerSecond(Value);
        if(FParse::Value(FCommandLine::Get(),TEXT("PortSimSeaIntensity="),Value)) Environment->SetSeaIntensity(Value);
        if(FParse::Value(FCommandLine::Get(),TEXT("PortSimFogVisibility="),Value)) Environment->SetFogVisibilityMeters(Value);
        if(FParse::Value(FCommandLine::Get(),TEXT("PortSimRainRate="),Value)) Environment->SetRainfallMmPerHour(Value);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimWind"))) Environment->SetWindEnabled(true);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimNoWind"))) Environment->SetWindEnabled(false);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimSea"))) Environment->SetSeaMotionEnabled(true);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimNoSea"))) Environment->SetSeaMotionEnabled(false);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimFog"))) Environment->SetFogEnabled(true);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimNoFog"))) Environment->SetFogEnabled(false);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimRain"))) Environment->SetRainEnabled(true);
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimNoRain"))) Environment->SetRainEnabled(false);
    }
    ConfiguredShips=PendingShips;
    for(int32 I=0;I<3;++I) ConfiguredCargo[I]=PendingCargo[I];
}
bool AQuayCrane::SaveRunSettings()
{
    auto Object=MakeShared<FJsonObject>(); Object->SetNumberField(TEXT("ships"),PendingShips);
    for(int32 I=0;I<3;++I) Object->SetNumberField(FString::Printf(TEXT("cargo_%d"),I+1),PendingCargo[I]);
    if(Environment)
    {
        Object->SetBoolField(TEXT("wind_enabled"),Environment->IsWindEnabled());
        Object->SetNumberField(TEXT("wind_speed_mps"),Environment->GetWindSpeedMetersPerSecond());
        Object->SetBoolField(TEXT("sea_enabled"),Environment->IsSeaMotionEnabled());
        Object->SetNumberField(TEXT("sea_intensity"),Environment->GetSeaIntensity());
        Object->SetBoolField(TEXT("rain_enabled"),Environment->IsRainEnabled());
        Object->SetNumberField(TEXT("rainfall_mm_per_hour"),Environment->GetRainfallMmPerHour());
        Object->SetBoolField(TEXT("fog_enabled"),Environment->IsFogEnabled());
        Object->SetNumberField(TEXT("fog_visibility_m"),Environment->GetFogVisibilityMeters());
    }
    FString Json; FJsonSerializer::Serialize(Object,TJsonWriterFactory<>::Create(&Json));
    const FString Path=RunSettingsPath();
    bRunSettingsSaved=FFileHelper::SaveStringToFile(Json,*(Path+TEXT(".tmp")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) &&
        IFileManager::Get().Move(*Path,*(Path+TEXT(".tmp")),true,true);
    return bRunSettingsSaved;
}
void AQuayCrane::StopSimulation()
{
    SaveRunSettings();
    if (bUnifiedTerminal)
    {
        bAutoRunning=false; bAutoPaused=false;
        DestroyTerminalActors();
        ConfiguredShips=PendingShips;
        for(int32 I=0;I<3;++I) ConfiguredCargo[I]=PendingCargo[I];
        BuildTerminalSite();
        BuildConfiguredVessels();
        SiteCameraIndex=-1;
    }
    ResetSimulation(); bAutoRunning=false; bAutoPaused=false;
    AutoElapsed=0; Status=TEXT("리셋 완료 — 설정 적용됨. 실행 버튼으로 시작하세요.");
    if(SiteLogistics) SiteLogistics->ExportDashboard(true,true);
}
FString AQuayCrane::ElapsedText() const
{
    const int64 Seconds=FMath::Max<int64>(0,int64(AutoElapsed));
    return FString::Printf(TEXT("%02lld시 %02lld분 %02lld초"),Seconds/3600,(Seconds/60)%60,Seconds%60);
}

