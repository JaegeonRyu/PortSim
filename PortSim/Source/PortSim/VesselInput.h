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

// Operations describe changes to the separately supplied arrival inventory.
inline bool ValidateVesselOperations(const FPortVesselInput& Stowage,const FPortVesselInput& Operations,FString& Error)
{
    TMap<FString,FIntVector> Aboard;
    TSet<FIntVector> Remaining;
    for(const auto& Row:Stowage.Rows) { Aboard.Add(Row.ContainerId,Row.GridSlot); Remaining.Add(Row.GridSlot); }
    for(const auto& Row:Operations.Rows) if(Row.Operation==TEXT("DISCHARGE"))
    {
        const auto* Slot=Aboard.Find(Row.ContainerId);
        if(!Slot || *Slot!=Row.GridSlot)
        { Error=TEXT("양하 ID 또는 위치가 선내 적재도와 일치하지 않습니다: ")+Row.ContainerId; return false; }
        Remaining.Remove(*Slot);
    }
    for(const auto& Slot:Remaining) if(Slot.Z>0 && !Remaining.Contains(Slot-FIntVector(0,0,1)))
    { Error=TEXT("양하 대상 위에 잔류 컨테이너가 있습니다. 상단 컨테이너도 양하 목록에 추가하세요."); return false; }
    for(const auto& Row:Operations.Rows) if(Row.Operation==TEXT("LOAD"))
    {
        if(Aboard.Contains(Row.ContainerId) || Remaining.Contains(Row.GridSlot))
        { Error=TEXT("적하 ID 또는 목적 위치가 기존 선박 재고와 중복됩니다: ")+Row.ContainerId; return false; }
        Remaining.Add(Row.GridSlot);
    }
    for(const auto& Slot:Remaining) if(Slot.Z>0 && !Remaining.Contains(Slot-FIntVector(0,0,1)))
    { Error=TEXT("적하 목적 위치 아래에 지지 컨테이너가 없습니다."); return false; }
    return true;
}
