#pragma once
#include "CoreMinimal.h"

namespace AGVDispatchDependencies
{
    struct FPriority { int32 PathLength=0, Unlocks=0; };

    // Yard support dependencies are already ordered by the stowage planner.
    // Count the complete chain THROUGH a job, so its final upper box retains
    // the urgency of its foundations instead of losing priority at the end.
    inline bool Build(const TArray<int32>& Rank,const TArray<FIntPoint>& Dependencies,
        int32 Limit,TArray<FPriority>& Result)
    {
        Result.Reset(); Result.SetNum(Rank.Num());
        TArray<TArray<int32>> Children; Children.SetNum(Rank.Num());
        for(FIntPoint Edge:Dependencies)
        {
            if(!Rank.IsValidIndex(Edge.X) || !Rank.IsValidIndex(Edge.Y)) return false;
            if(Rank[Edge.Y]<0 || Rank[Edge.Y]>=Limit) continue;
            if(Rank[Edge.X]<0 || Rank[Edge.X]>=Rank[Edge.Y]) return false;
            Children[Edge.X].AddUnique(Edge.Y);
        }
        TArray<int32> Order;
        for(int32 I=0;I<Rank.Num();++I) if(Rank[I]>=0 && Rank[I]<Limit) Order.Add(I);
        Order.Sort([&](int32 A,int32 B){return Rank[A]<Rank[B];});
        TArray<int32> Before,After; Before.Init(0,Rank.Num()); After.Init(0,Rank.Num());
        for(int32 Parent:Order) for(int32 Child:Children[Parent])
            Before[Child]=FMath::Max(Before[Child],Before[Parent]+1);
        TArray<TSet<int32>> Descendants; Descendants.SetNum(Rank.Num());
        for(int32 O=Order.Num()-1;O>=0;--O)
        {
            const int32 Parent=Order[O];
            for(int32 Child:Children[Parent])
            {
                After[Parent]=FMath::Max(After[Parent],After[Child]+1);
                Descendants[Parent].Add(Child); Descendants[Parent].Append(Descendants[Child]);
            }
            Result[Parent]={Before[Parent]+After[Parent],Descendants[Parent].Num()};
        }
        return true;
    }
}
