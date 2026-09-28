#if WITH_DEV_AUTOMATION_TESTS
#include "QuayCrane.h"
#include "PortEnvironmentComponent.h"
#include "PortSiteLogistics.h"
#include "PortAGVActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

struct FPortWindInputTestState
{
    int32 Step=0,Frame=0;
    float Elapsed=0;
    FVector CameraBefore=FVector::ZeroVector;
    FRotator ViewBefore=FRotator::ZeroRotator;
    FPortWindState SavedWind;
};

// Headless integration test injects real PlayerController key events, then checks
// the ordinary Tick handlers on subsequent frames (not direct toggle callbacks).
void AQuayCrane::TickWindInputTest(float Dt)
{
    if(!WindInputTestState) WindInputTestState=MakeShared<FPortWindInputTestState>();
    auto& T=*WindInputTestState;
    T.Elapsed+=Dt;
    if(T.Elapsed<2) return;
    auto* PC=Cast<APlayerController>(GetController());
    if(!PC) return;
    auto Check=[&](bool Pass,const TCHAR* What)
    {
        UE_LOG(LogTemp,Display,TEXT("WIND_INPUT_%s: step=%d %s"),Pass?TEXT("PASS"):TEXT("FAIL"),T.Step,What);
        if(!Pass) { T.Step=1000; FPlatformMisc::RequestExitWithStatus(false,1); }
        return Pass;
    };
    if(T.Step>=1000) return;
    const FKey Keys[]={EKeys::P,EKeys::Equals,EKeys::Hyphen,EKeys::Add,EKeys::Zero,EKeys::Zero,
        EKeys::Zero,EKeys::Add,EKeys::NumPadZero,EKeys::Add,EKeys::Zero,EKeys::R,EKeys::Zero,
        EKeys::H,EKeys::H,EKeys::SpaceBar,EKeys::SpaceBar,EKeys::Home,EKeys::End,EKeys::Tab,
        EKeys::W,EKeys::S,EKeys::A,EKeys::D,EKeys::Q,EKeys::E,EKeys::W,EKeys::RightMouseButton,
        EKeys::P,EKeys::U,EKeys::L,EKeys::F};
    if(T.Step>=UE_ARRAY_COUNT(Keys))
    {
        if(Check(FollowedAGV.IsValid(),TEXT("Loaded AGV follow tracks an actual vehicle")))
        {
            FString Error;
            if(!Check(ValidateTerminalActors(Error),TEXT("Terminal actor/inventory validation"))) return;
            UE_LOG(LogTemp,Display,TEXT("PORTSIM_WIND_INPUT_PASS: Ctrl-left/right+0 exclusive; zero/numpad speed reset; raw weather reset persistence; H/button; E-stop; cameras; U/L; loaded AGV follow"));
            T.Step=1000; FPlatformMisc::RequestExitWithStatus(false,0);
        }
        return;
    }
    // Let normal logistics produce an actual loaded vehicle before testing F.
    if(T.Step==31 && T.Frame==0 && !SiteLogistics->LoadedVehicle())
    {
        if(T.Elapsed>1500) Check(false,TEXT("Timed out waiting for a loaded AGV"));
        return;
    }
    const bool LeftCtrl=T.Step==4 || T.Step==10;
    const bool RightCtrl=T.Step==5 || T.Step==12;
    auto Input=[&](FKey Key,EInputEvent Event)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,Event,Event==IE_Released?0.f:1.f));
    };
    if(T.Frame==0)
    {
        T.CameraBefore=CameraArm->GetComponentLocation();
        T.ViewBefore=CameraArm->GetComponentRotation();
        if(T.Step==11) T.SavedWind=Environment->GetWindState();
        if(LeftCtrl) Input(EKeys::LeftControl,IE_Pressed);
        if(RightCtrl) Input(EKeys::RightControl,IE_Pressed);
        if(T.Step==26) Input(EKeys::LeftShift,IE_Pressed);
        Input(Keys[T.Step],IE_Pressed);
    }
    if(T.Step==27 && T.Frame==1)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseX,IE_Axis,30.f));
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::MouseY,IE_Axis,10.f));
    }
    if(T.Frame==3)
    {
        bool Pass=true;
        switch(T.Step)
        {
        case 0: Pass=bAutoPaused; break;
        case 1: case 3: case 7: case 9: Pass=SimulationSpeed==2; break;
        case 2: case 6: case 8: Pass=SimulationSpeed==1; break;
        case 4: case 10: Pass=!Environment->IsWindEnabled() && SimulationSpeed==2; break;
        case 5: case 12: Pass=Environment->IsWindEnabled() && SimulationSpeed==2; break;
        case 11:
        {
            const auto& W=Environment->GetWindState();
            Pass=!W.bWindEnabled && W.WindSpeedMetersPerSecond==T.SavedWind.WindSpeedMetersPerSecond &&
                W.WindDirectionDegrees==T.SavedWind.WindDirectionDegrees && W.WindDirectionVector==T.SavedWind.WindDirectionVector &&
                W.TargetWindSpeedMetersPerSecond==T.SavedWind.TargetWindSpeedMetersPerSecond &&
                W.TargetWindDirectionDegrees==T.SavedWind.TargetWindDirectionDegrees && W.SecondsUntilTarget==T.SavedWind.SecondsUntilTarget &&
                !bAutoPaused && !bEmergencyStop && bAutoRunning;
            break;
        }
        case 13: Pass=!bHUDVisible && PC->GetHUD()->bShowHUD; break;
        case 14:
        {
            Pass=bHUDVisible && PC->GetHUD()->bShowHUD;
            auto* HUD=Cast<APortSimHUD>(PC->GetHUD());
            HUD->NotifyHitBoxClick(TEXT("TogglePortHUD")); Pass &= !bHUDVisible && HUD->bShowHUD;
            HUD->NotifyHitBoxClick(TEXT("TogglePortHUD")); Pass &= bHUDVisible && HUD->bShowHUD;
            break;
        }
        case 15: Pass=bEmergencyStop; break;
        case 16: Pass=!bEmergencyStop; break;
        case 17: Pass=!bFreeCamera && CameraArm->TargetArmLength==185000; break;
        case 18: Pass=!bFreeCamera && CameraArm->TargetArmLength==65000; break;
        case 19: Pass=SiteCameraIndex>=0 && !bFreeCamera; break;
        case 20: case 21: case 22: case 23: case 24: case 25: case 26:
            Pass=bFreeCamera && !CameraArm->GetComponentLocation().Equals(T.CameraBefore,.001); break;
        case 27: Pass=bMouseLooking && !PC->bShowMouseCursor && !CameraArm->GetComponentRotation().Equals(T.ViewBefore,.001); break;
        case 28: Pass=bAutoPaused; break;
        case 29: Pass=!bAutoPaused && bAutoRunning; break;
        case 30: Pass=!bAutoLoading && bAutoRunning; break; // Unified mode deliberately rejects loading.
        case 31: Pass=bFollowAGV; break;
        }
        if(!Check(Pass,*Keys[T.Step].ToString())) return;
        if(FParse::Param(FCommandLine::Get(),TEXT("PortSimWindHUDCapture")) &&
            (T.Step==4 || T.Step==12 || T.Step==13 || T.Step==14))
            FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/FString::Printf(TEXT("Screenshots/WindHUD_%d.png"),T.Step),true,false);
        Input(Keys[T.Step],IE_Released);
        if(LeftCtrl) Input(EKeys::LeftControl,IE_Released);
        if(RightCtrl) Input(EKeys::RightControl,IE_Released);
        if(T.Step==26) Input(EKeys::LeftShift,IE_Released);
        if(T.Step==12)
        {
            const auto Before=Environment->GetWindState();
            ResetSimulation();
            const auto& After=Environment->GetWindState();
            if(!Check(After.bWindEnabled && After.WindSpeedMetersPerSecond==Before.WindSpeedMetersPerSecond &&
                After.WindDirectionDegrees==Before.WindDirectionDegrees && After.TargetWindSpeedMetersPerSecond==Before.TargetWindSpeedMetersPerSecond &&
                After.TargetWindDirectionDegrees==Before.TargetWindDirectionDegrees,TEXT("ON reset preserves weather"))) return;
        }
    }
    if(++T.Frame>=6) { T.Frame=0; ++T.Step; }
}
#endif
