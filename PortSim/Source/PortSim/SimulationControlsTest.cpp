#include "QuayCrane.h"
#include "PortEnvironmentComponent.h"
#include "PortSiteLogistics.h"
#include "TerminalLayout.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void AQuayCrane::TickControlsTest(float Dt)
{
    auto Fail=[](const FString& Reason)
    { UE_LOG(LogTemp,Error,TEXT("PORTSIM_CONTROLS_FAIL: %s"),*Reason); FPlatformMisc::RequestExitWithStatus(false,1); };
    auto* PC=Cast<APlayerController>(GetController());
    auto* HUD=PC?Cast<APortSimHUD>(PC->GetHUD()):nullptr;
    if(!HUD || !SiteLogistics) { Fail(TEXT("Missing HUD or logistics")); return; }
    FString Error;
    if(!SiteLogistics->Validate(Error)) { Fail(Error); return; }
    ++ControlsTestFrames;
    switch(ControlsTestStage)
    {
    case 0:
        if(bAutoRunning || AutoElapsed!=0 || SiteLogistics->InitialYard!=0 || SiteLogistics->ReceivingCapacity!=TerminalLayout::ReceivingSlotCount())
        { Fail(TEXT("Must start stopped with an empty size-segregated yard")); return; }
        SetPendingShips(1); SetPendingCargo(0,TerminalLayout::ReceivingSlotCount()); SetPendingShips(3);
        // New ships retain their manual/file counts. Check capacity and positive
        // allocation, rather than requiring the old reset-to-exactly-one policy.
        if(PendingCargoTotal()!=TerminalLayout::ReceivingSlotCount() || PendingCargo[1]<1 || PendingCargo[2]<1)
        { Fail(TEXT("Ship increase must preserve capacity and at least one box per ship")); return; }
        SetPendingCargo(1,TerminalLayout::ReceivingSlotCount());
        if(PendingCargoTotal()>TerminalLayout::ReceivingSlotCount()) { Fail(TEXT("Aggregate overflow")); return; }
        SetPendingCargo(0,0); SetPendingCargo(1,2); SetPendingCargo(2,3);
        if(PendingCargoTotal()!=6 || SiteLogistics->InitialShipCount()!=ConfiguredCargo[0]+(ConfiguredShips>1?ConfiguredCargo[1]:0)+(ConfiguredShips>2?ConfiguredCargo[2]:0))
        { Fail(TEXT("Pending settings changed the active manifest")); return; }
        AutoElapsed=90061;
        if(ElapsedText()!=TEXT("25시 01분 01초")) { Fail(TEXT("Elapsed formatting wraps days")); return; }
        AutoElapsed=0;
        if(!Environment) { Fail(TEXT("Missing environment controls")); return; }
        {
            const FDateTime Before=Environment->GetLocalDateTime();
            HUD->NotifyHitBoxClick(TEXT("StartTimeUp"));
            ControlsTestStartTime=Environment->GetLocalDateTime();
            const FDateTime After=Before+FTimespan::FromMinutes(30);
            if(ControlsTestStartTime!=Before.GetDate()+After.GetTimeOfDay())
            { Fail(TEXT("Start time did not advance 30 minutes on the same date")); return; }
            HUD->NotifyHitBoxClick(TEXT("StartTimeDown"));
            if(Environment->GetLocalDateTime()!=Before) { Fail(TEXT("Start time did not move back 30 minutes")); return; }
            HUD->NotifyHitBoxClick(TEXT("StartTimeUp"));
        }
        for(const FName Toggle:{FName(TEXT("ToggleWindSetting")),FName(TEXT("ToggleSeaSetting")),
            FName(TEXT("ToggleRainSetting")),FName(TEXT("ToggleFogSetting")),
            FName(TEXT("ToggleSnowSetting")),FName(TEXT("ToggleIceSetting"))})
        {
            const bool Before=Toggle==TEXT("ToggleWindSetting")?Environment->IsWindEnabled():
                Toggle==TEXT("ToggleSeaSetting")?Environment->IsSeaMotionEnabled():
                Toggle==TEXT("ToggleRainSetting")?Environment->IsRainEnabled():
                Toggle==TEXT("ToggleFogSetting")?Environment->IsFogEnabled():
                Toggle==TEXT("ToggleSnowSetting")?Environment->IsSnowEnabled():Environment->IsIceEnabled();
            HUD->NotifyHitBoxClick(Toggle);
            const bool After=Toggle==TEXT("ToggleWindSetting")?Environment->IsWindEnabled():
                Toggle==TEXT("ToggleSeaSetting")?Environment->IsSeaMotionEnabled():
                Toggle==TEXT("ToggleRainSetting")?Environment->IsRainEnabled():
                Toggle==TEXT("ToggleFogSetting")?Environment->IsFogEnabled():
                Toggle==TEXT("ToggleSnowSetting")?Environment->IsSnowEnabled():Environment->IsIceEnabled();
            if(Before==After) { Fail(TEXT("Environment UI toggle did not change state")); return; }
            HUD->NotifyHitBoxClick(Toggle);
        }
        HUD->NotifyHitBoxClick(TEXT("SpeedUp"));
        if(SimulationSpeed!=2) { Fail(TEXT("Speed button")); return; }
        HUD->NotifyHitBoxClick(TEXT("RunSimulation"));
        if(!bAutoRunning) { Fail(TEXT("Run button")); return; }
        HUD->NotifyHitBoxClick(TEXT("StartTimeUp"));
        if(CanAdjustStartTime() || Environment->GetLocalDateTime()!=ControlsTestStartTime)
        { Fail(TEXT("Start time editable during a run")); return; }
        {
            TGuardValue<bool> FinishedRun(bAutoRunning,false);
            HUD->NotifyHitBoxClick(TEXT("StartTimeDown"));
            if(CanAdjustStartTime() || Environment->GetLocalDateTime()!=ControlsTestStartTime)
            { Fail(TEXT("Start time editable after completion without reset")); return; }
        }
        ControlsTestStage=1; ControlsTestFrames=0; break;
    case 1:
        if(AutoElapsed<2) break;
        HUD->NotifyHitBoxClick(TEXT("PauseSimulation"));
        ControlsTestTime=AutoElapsed; ControlsTestStage=2; ControlsTestFrames=0; break;
    case 2:
        if(AutoElapsed!=ControlsTestTime) { Fail(TEXT("Paused execution clock advanced")); return; }
        if(ControlsTestFrames<30) break;
        {
            const FDateTime Before=Environment->GetLocalDateTime();
            HUD->NotifyHitBoxClick(TEXT("StartTimeDown"));
            if(CanAdjustStartTime() || Environment->GetLocalDateTime()!=Before)
            { Fail(TEXT("Start time editable during pause")); return; }
        }
        SiteLogistics->ExportDashboard(true,true);
        {
            FString Json; TSharedPtr<FJsonObject> O;
            if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectSavedDir()/TEXT("Dashboard/state.json"))) ||
                !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),O) ||
                O->GetNumberField(TEXT("elapsed_seconds"))!=AutoElapsed || O->GetNumberField(TEXT("playback_rate"))!=2)
            { Fail(TEXT("HUD/dashboard clocks differ")); return; }
        }
        HUD->NotifyHitBoxClick(TEXT("PauseSimulation")); ControlsTestStage=3; ControlsTestFrames=0; break;
    case 3:
        if(ControlsTestFrames<30) break;
        if(AutoElapsed<=ControlsTestTime) { Fail(TEXT("Resume did not advance time")); return; }
        HUD->NotifyHitBoxClick(TEXT("StopSimulation"));
        if(bAutoRunning || bAutoPaused || AutoElapsed!=0 || Deliveries!=0 || !SiteLogistics->Validate(Error))
        { Fail(TEXT("Stop did not restore valid empty initial yard: ")+Error); return; }
        if(!CanAdjustStartTime() || Environment->GetLocalDateTime()!=ControlsTestStartTime)
        { Fail(TEXT("Reset did not restore the selected and editable start time")); return; }
        if(SiteLogistics->Surface.WaterFilmMm!=Environment->GetSurfaceConditions().WaterFilmMm ||
            (Environment->IsRainEnabled() && SiteLogistics->Surface.WaterFilmMm<=0))
        { Fail(TEXT("Reset equipment lost the accumulated live surface")); return; }
        ControlsTestStage=4; ControlsTestFrames=0; break;
    case 4:
        if(AutoElapsed!=0 || bAutoRunning || SiteLogistics->InTransit()!=0) { Fail(TEXT("Stopped run restarted")); return; }
        if(ControlsTestFrames<30) break;
        UE_LOG(LogTemp,Display,TEXT("PORTSIM_CONTROLS_PASS: limits, pending settings, empty yard, buttons, pause/resume/stop and shared elapsed time"));
        FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
}
