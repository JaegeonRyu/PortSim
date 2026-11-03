#pragma once
#include "CoreMinimal.h"
#include "PortEquipmentActor.h"
#include "PortSupportVehicle.generated.h"

UENUM(BlueprintType)
enum class EPortSupportType : uint8 { ReachStacker, YardTractor, EmptyHandler, Forklift, YardChassis };

/** Individually identifiable support equipment; dispatch is handled separately. */
UCLASS()
class PORTSIM_API APortSupportVehicle : public APortEquipmentActor
{
    GENERATED_BODY()
public:
    void Configure(EPortSupportType Type,int32 Number);
    void PositionRecoverySpreader(FVector Position,FRotator Rotation,float LengthCm);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) EPortSupportType EquipmentType=EPortSupportType::YardChassis;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly) int32 EquipmentNumber=0;
};
