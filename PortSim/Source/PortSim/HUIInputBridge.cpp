#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "QuayCrane.h"

namespace
{
    /**
     * HUI input policy:
     *   H toggles the in-game HUI on/off.
     *
     * This is kept separate from the crane simulation so the toggle works in
     * both manual and terminal-automation modes.
     */
    struct FHUIInputBridge
    {
        FHUIInputBridge()
        {
            FWorldDelegates::OnWorldTickStart.AddStatic(&FHUIInputBridge::OnWorldTickStart);
        }

        static void OnWorldTickStart(UWorld* World, ELevelTick, float)
        {
            if (!World || !GEngine) return;

            for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
            {
                APlayerController* Controller = It->Get();
                if (!Controller || !Controller->IsLocalController()) continue;

                if (Controller->WasInputKeyJustPressed(EKeys::H))
                {
                    // PortSim's Canvas HUD keeps its visibility button available;
                    // its pawn owns bHUDVisible. Do not also toggle AHUD::bShowHUD.
                    if (AHUD* HUD = Controller->GetHUD(); HUD && !Cast<APortSimHUD>(HUD))
                    {
                        HUD->bShowHUD = !HUD->bShowHUD;
                    }
                }
            }
        }
    };

    static FHUIInputBridge GHUIInputBridge;
}
