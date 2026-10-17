#pragma once
#include "CoreMinimal.h"

struct FPortVesselInputRow
{
    FString ContainerId, Operation;
    int32 Bay=0, Row=0, Tier=0;
    FIntVector GridSlot=FIntVector::ZeroValue; // zero-based physical Bay/Row/Tier
};

struct FPortVesselInput
{
    FString FileName;
    TArray<FPortVesselInputRow> Rows;
};
