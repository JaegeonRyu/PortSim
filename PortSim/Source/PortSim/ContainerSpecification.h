#pragma once

#include "CoreMinimal.h"
#include "ContainerSpecification.generated.h"

UENUM(BlueprintType)
enum class EPortContainerSize : uint8
{
    Ft20 UMETA(DisplayName="20 ft"),
    Ft40 UMETA(DisplayName="40 ft"),
    Ft45 UMETA(DisplayName="45 ft")
};

struct FPortContainerSpecification
{
    EPortContainerSize Size=EPortContainerSize::Ft40;
    int32 LengthFt=40;
    float LengthCm=1219.2f;
    float WidthCm=243.8f;
    float HeightCm=259.1f;
    float TareMassKg=3800.f;
    float CargoMassKg=12000.f;

    float GrossMassKg() const { return TareMassKg+CargoMassKg; }
};

namespace PortContainerSpecification
{
    // Largest-remainder allocation keeps the requested 28/70/2 mix for any
    // manifest total; totals divisible by 100 have exact percentages.
    inline FIntVector CountsForManifest(int32 Total)
    {
        Total=FMath::Max(0,Total);
        int32 Counts[3],Remainders[3]; const int32 Percent[3]={28,70,2};
        for(int32 I=0;I<3;++I)
        { Counts[I]=int64(Total)*Percent[I]/100; Remainders[I]=int64(Total)*Percent[I]%100; }
        for(int32 Left=Total-Counts[0]-Counts[1]-Counts[2];Left>0;--Left)
        {
            int32 Best=1;
            for(int32 I:{0,2}) if(Remainders[I]>Remainders[Best]) Best=I;
            ++Counts[Best]; Remainders[Best]=-1;
        }
        return FIntVector(Counts[0],Counts[1],Counts[2]);
    }
    // Masses are representative simulation inputs, not a claim that size fixes weight.
    inline FPortContainerSpecification ForSequence(int32 Sequence,int32 ManifestIndex=INDEX_NONE,int32 ManifestTotal=100)
    {
        const int32 Total=FMath::Max(1,ManifestTotal);
        auto GCD=[](int32 A,int32 B) { while(B) { const int32 R=A%B; A=B; B=R; } return A; };
        int32 Stride=37;
        while(GCD(Stride,Total)!=1) ++Stride;
        const int32 Index=ManifestIndex==INDEX_NONE?FMath::Abs(Sequence)%Total:ManifestIndex;
        const int32 Bucket=(int64(Index)*Stride)%Total;
        const FIntVector Counts=CountsForManifest(Total);
        FPortContainerSpecification Result;
        if(Bucket<Counts.X)
        {
            Result.Size=EPortContainerSize::Ft20; Result.LengthFt=20; Result.LengthCm=605.8f;
            Result.TareMassKg=2300.f; Result.CargoMassKg=7000.f+1000.f*(FMath::Abs(Sequence*7)%9);
        }
        else if(Bucket>=Counts.X+Counts.Y)
        {
            Result.Size=EPortContainerSize::Ft45; Result.LengthFt=45; Result.LengthCm=1371.6f;
            Result.TareMassKg=4800.f; Result.CargoMassKg=14000.f+1500.f*(FMath::Abs(Sequence*5)%8);
        }
        else
        {
            Result.Size=EPortContainerSize::Ft40; Result.LengthFt=40; Result.LengthCm=1219.2f;
            Result.TareMassKg=3800.f; Result.CargoMassKg=12000.f+1500.f*(FMath::Abs(Sequence*3)%9);
        }
        return Result;
    }
}
