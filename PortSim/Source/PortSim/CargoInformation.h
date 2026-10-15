#pragma once
#include "CoreMinimal.h"
#include "CargoInformation.generated.h"

/** Synthetic booking data for planning; never a real dangerous-goods declaration. */
USTRUCT(BlueprintType)
struct FPortCargoInformation
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Goods=TEXT("Machinery parts");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString HandlingType=TEXT("dry");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString HazardGroup=TEXT("none");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 OutboundOrder=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString DestinationPort=TEXT("SGSIN");
    UPROPERTY(EditAnywhere,BlueprintReadWrite) int32 PortCallOrder=2;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float TemperatureC=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float PermittedSuperimposedKg=180000;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) FString DataBasis=TEXT("synthetic_simulation_booking");
    bool Valid() const
    {
        return !Goods.IsEmpty() && OutboundOrder>=1 && OutboundOrder<=4 && PortCallOrder>=1 && PortCallOrder<=3 &&
            (HandlingType==TEXT("dry") || HandlingType==TEXT("reefer")) &&
            (HazardGroup==TEXT("none") || HazardGroup==TEXT("flammable") || HazardGroup==TEXT("corrosive")) &&
            (HandlingType!=TEXT("reefer") || (HazardGroup==TEXT("none") && FMath::IsFinite(TemperatureC) && TemperatureC>=-60 && TemperatureC<=40)) &&
            !DestinationPort.IsEmpty() && FMath::IsFinite(PermittedSuperimposedKg) && PermittedSuperimposedKg>0;
    }
    static FPortCargoInformation ForSequence(int32 Sequence,int32 LengthFt)
    {
        FPortCargoInformation C;
        // Small dry boxes are a machinery booking group, making useful paired
        // AGV trips possible without inventing a twin-lift crane/spreader.
        if(LengthFt==20) return C;
        const int32 N=FMath::Abs(Sequence);
        C.OutboundOrder=1+(N/3)%4; C.PortCallOrder=1+(N/5)%3;
        C.DestinationPort=C.PortCallOrder==1?TEXT("JPTYO"):C.PortCallOrder==2?TEXT("SGSIN"):TEXT("NLRTM");
        switch(N%12)
        {
        case 0: C.Goods=TEXT("Frozen food"); C.HandlingType=TEXT("reefer"); C.TemperatureC=-18; break;
        case 1: C.Goods=TEXT("Chilled produce"); C.HandlingType=TEXT("reefer"); C.TemperatureC=4; break;
        case 2: C.Goods=TEXT("Simulated flammable coating"); C.HazardGroup=TEXT("flammable"); break;
        case 3: C.Goods=TEXT("Simulated corrosive cleaner"); C.HazardGroup=TEXT("corrosive"); break;
        case 4: case 5: C.Goods=TEXT("Steel parts"); break;
        case 6: case 7: C.Goods=TEXT("Electronics"); break;
        case 8: case 9: C.Goods=TEXT("Textiles"); break;
        default: C.Goods=TEXT("Furniture"); break;
        }
        return C;
    }
};
