#include "QuayCrane.h"
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
        if(PendingCargoTotal()!=TerminalLayout::ReceivingSlotCount() || PendingCargo[1]!=1 || PendingCargo[2]!=1)
        { Fail(TEXT("Ship increase must reserve a minimum of one box per ship")); return; }
        SetPendingCargo(1,TerminalLayout::ReceivingSlotCount());
        if(PendingCargoTotal()>TerminalLayout::ReceivingSlotCount()) { Fail(TEXT("Aggregate overflow")); return; }
        SetPendingCargo(0,0); SetPendingCargo(1,2); SetPendingCargo(2,3);
        if(PendingCargoTotal()!=6 || SiteLogistics->InitialShipCount()!=ConfiguredCargo[0]+(ConfiguredShips>1?ConfiguredCargo[1]:0)+(ConfiguredShips>2?ConfiguredCargo[2]:0))
        { Fail(TEXT("Pending settings changed the active manifest")); return; }
        AutoElapsed=90061;
        if(ElapsedText()!=TEXT("25시 01분 01초")) { Fail(TEXT("Elapsed formatting wraps days")); return; }
        AutoElapsed=0;
        HUD->NotifyHitBoxClick(TEXT("SpeedUp"));
        if(SimulationSpeed!=2) { Fail(TEXT("Speed button")); return; }
        HUD->NotifyHitBoxClick(TEXT("RunSimulation"));
        if(!bAutoRunning) { Fail(TEXT("Run button")); return; }
        ControlsTestStage=1; ControlsTestFrames=0; break;
    case 1:
        if(AutoElapsed<2) break;
        HUD->NotifyHitBoxClick(TEXT("PauseSimulation"));
        ControlsTestTime=AutoElapsed; ControlsTestStage=2; ControlsTestFrames=0; break;
    case 2:
        if(AutoElapsed!=ControlsTestTime) { Fail(TEXT("Paused execution clock advanced")); return; }
        if(ControlsTestFrames<30) break;
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
        ControlsTestStage=4; ControlsTestFrames=0; break;
    case 4:
        if(AutoElapsed!=0 || bAutoRunning || SiteLogistics->InTransit()!=0) { Fail(TEXT("Stopped run restarted")); return; }
        if(ControlsTestFrames<30) break;
        UE_LOG(LogTemp,Display,TEXT("PORTSIM_CONTROLS_PASS: limits, pending settings, empty yard, buttons, pause/resume/stop and shared elapsed time"));
        FPlatformMisc::RequestExitWithStatus(false,0); break;
    }
}
