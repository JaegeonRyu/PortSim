#include "ShipStowagePlanning.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool FPortShipStowageAssumptions::Load(const FString& Path,FString& Error)
{
    FString Text; TSharedPtr<FJsonObject> Root;
    if(!FFileHelper::LoadFileToString(Text,*Path) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root) || !Root.IsValid())
    { Error=TEXT("Missing/invalid Stowage_Planning.json"); return false; }
    if(!Root->TryGetStringField(TEXT("basis"),Basis) || Basis!=TEXT("simulation_assumptions_not_vessel_approved"))
    { Error=TEXT("Ship planning assumptions must declare their simulation basis"); return false; }
    auto Read=[&](const TCHAR* Key,double& Value)
    {
        return Root->TryGetNumberField(Key,Value) && FMath::IsFinite(Value) && Value>=0;
    };
    if(!Read(TEXT("lightship_kg"),LightshipKg) || !Read(TEXT("lightship_kg_m"),LightshipKGM) ||
        !Read(TEXT("km_m"),KMM) || !Read(TEXT("free_surface_moment_kg_m"),FreeSurfaceMomentKgM) ||
        !Read(TEXT("minimum_gm_m"),MinimumGMM) || !Read(TEXT("maximum_gm_m"),MaximumGMM) ||
        !Read(TEXT("maximum_heel_deg"),MaximumHeelDegrees) || !Read(TEXT("maximum_stack_kg"),MaximumStackKg) ||
        !Read(TEXT("maximum_lashing_n"),MaximumLashingN) || !Read(TEXT("lateral_acceleration_mps2"),LateralAccelerationMps2) ||
        !Read(TEXT("deck_height_m"),DeckHeightM) || LightshipKg<=0 || KMM<=LightshipKGM || MinimumGMM<=0 ||
        MaximumGMM<=MinimumGMM || MaximumStackKg<=0 || MaximumLashingN<=0 || MaximumHeelDegrees<=0 ||
        LateralAccelerationMps2<=0)
    { Error=TEXT("Invalid ship planning numeric assumptions"); return false; }
    SnapshotJson=Text;
    return true;
}

FPortShipStowageResult PlanShipStowage(const TArray<FPortShipPlanningCargo>& Cargo,const FPortShipStowageAssumptions& A)
{
    FPortShipStowageResult Result;
    for(double V:{A.MaximumStackKg,A.MaximumLashingN,A.MaximumHeelDegrees,A.DeckHeightM})
        if(!FMath::IsFinite(V) || V<=0) { Result.Reason=TEXT("Invalid ship limit"); return Result; }
    if(!FMath::IsFinite(A.LightshipKg) || A.LightshipKg<=0 || A.Bays<=0 || A.Rows<=0 || A.Tiers<=0 ||
        A.Bays>100 || A.Rows>30 || A.Tiers>20 || !FMath::IsFinite(A.KMM) || !FMath::IsFinite(A.LightshipKGM) ||
        !FMath::IsFinite(A.FreeSurfaceMomentKgM) || A.FreeSurfaceMomentKgM<0 ||
        !FMath::IsFinite(A.MinimumGMM) || !FMath::IsFinite(A.MaximumGMM) || A.MinimumGMM<=0 ||
        A.MaximumGMM<=A.MinimumGMM || A.MaximumStackKg<=0 || A.MaximumLashingN<=0 ||
        !FMath::IsFinite(A.LateralAccelerationMps2) || A.LateralAccelerationMps2<=0)
    { Result.Reason=TEXT("Invalid ship stability/stack/lashing inputs"); return Result; }
    struct FColumn { TArray<int32> CargoIndices; double Mass=0, Moment=0; };
    TArray<FColumn> Columns; Columns.SetNum(A.Bays*A.Rows);
    TArray<int32> Order; TSet<int32> IDs;
    for(int32 I=0;I<Cargo.Num();++I)
    {
        if(IDs.Contains(Cargo[I].ID) || !Cargo[I].Information.Valid() || Cargo[I].CoGOffsetM.ContainsNaN() ||
            FMath::Abs(Cargo[I].CoGOffsetM.Y)>1.219 || FMath::Abs(Cargo[I].CoGOffsetM.Z)>1.2955 || !FMath::IsFinite(Cargo[I].GrossKg) ||
            Cargo[I].GrossKg<=0 || (Cargo[I].LengthFt!=20 && Cargo[I].LengthFt!=40 && Cargo[I].LengthFt!=45))
        { Result.Reason=TEXT("Invalid/duplicate ship booking"); return Result; }
        IDs.Add(Cargo[I].ID); Order.Add(I);
    }
    // Later port calls load below earlier calls. Within that group, heavy first.
    Order.StableSort([&](int32 X,int32 Y)
    {
        if(Cargo[X].Information.PortCallOrder!=Cargo[Y].Information.PortCallOrder)
            return Cargo[X].Information.PortCallOrder>Cargo[Y].Information.PortCallOrder;
        if(Cargo[X].GrossKg!=Cargo[Y].GrossKg) return Cargo[X].GrossKg>Cargo[Y].GrossKg;
        return Cargo[X].ID<Cargo[Y].ID;
    });
    double Displacement=A.LightshipKg, VerticalMoment=A.LightshipKg*A.LightshipKGM, TransverseMoment=0;
    Result.CorrectedGMM=A.KMM-A.LightshipKGM-A.FreeSurfaceMomentKgM/A.LightshipKg;
    for(int32 Index:Order)
    {
        const auto& C=Cargo[Index];
        int32 Best=INDEX_NONE; double BestScore=TNumericLimits<double>::Max(),BestGM=0,BestHeel=0,BestLash=0;
        for(int32 Col=0;Col<Columns.Num();++Col)
        {
            const auto& Stack=Columns[Col]; const int32 Tier=Stack.CargoIndices.Num();
            if(Tier>=A.Tiers || Stack.Mass+C.GrossKg>A.MaximumStackKg) continue;
            if(Tier>0)
            {
                const auto& Lower=Cargo[Stack.CargoIndices.Last()];
                if(Lower.LengthFt!=C.LengthFt || Lower.Information.PortCallOrder<C.Information.PortCallOrder ||
                    Lower.Information.HazardGroup!=C.Information.HazardGroup ||
                    Lower.Information.HandlingType!=C.Information.HandlingType || Lower.GrossKg<C.GrossKg) continue;
            }
            bool Supported=true; double Above=C.GrossKg;
            for(int32 N=Tier-1;N>=0;--N)
            {
                const auto& Lower=Cargo[Stack.CargoIndices[N]];
                if(Above>Lower.Information.PermittedSuperimposedKg) { Supported=false; break; }
                Above+=Lower.GrossKg;
            }
            if(!Supported) continue;
            const double Y=(Col%A.Rows-(A.Rows-1)*.5)*2.6+C.CoGOffsetM.Y, Z=A.DeckHeightM+(Tier+.5)*2.591+C.CoGOffsetM.Z;
            const double Delta=Displacement+C.GrossKg;
            const double GM=A.KMM-(VerticalMoment+C.GrossKg*Z)/Delta-A.FreeSurfaceMomentKgM/Delta;
            if(!FMath::IsFinite(GM) || GM<A.MinimumGMM || GM>A.MaximumGMM) continue;
            const double Heel=FMath::RadiansToDegrees(FMath::Atan2(FMath::Abs(TransverseMoment+C.GrossKg*Y)/Delta,GM));
            if(Heel>A.MaximumHeelDegrees) continue;
            // Conservative static overturning-moment estimate shared by four
            // lashings. It is a simulation proxy, not vessel-approved lashing software.
            const double Moment=Stack.Moment+C.GrossKg*A.LateralAccelerationMps2*(Z-A.DeckHeightM);
            const double Lashing=Moment/(4*2.438);
            if(Lashing>A.MaximumLashingN) continue;
            // Preserve unused columns for later port groups whose heavier boxes
            // cannot sit above today's lighter stack. Reuse compatible stacks
            // after all safety limits pass, rather than exhausting the floor first.
            const double Score=Heel*1000.+(Tier==0?500.:Tier*10.)+FMath::Abs(Col/A.Rows-(A.Bays-1)*.5)*.01;
            if(Score<BestScore) { Best=Col; BestScore=Score; BestGM=GM; BestHeel=Heel; BestLash=Lashing; }
        }
        if(Best==INDEX_NONE) { Result.Reason=TEXT("No placement satisfies port access, GM, heel, stack and lashing limits"); return Result; }
        auto& Stack=Columns[Best]; const int32 Tier=Stack.CargoIndices.Num();
        const double Y=(Best%A.Rows-(A.Rows-1)*.5)*2.6+C.CoGOffsetM.Y, Z=A.DeckHeightM+(Tier+.5)*2.591+C.CoGOffsetM.Z;
        Displacement+=C.GrossKg; VerticalMoment+=C.GrossKg*Z; TransverseMoment+=C.GrossKg*Y;
        Stack.Mass+=C.GrossKg; Stack.Moment+=C.GrossKg*A.LateralAccelerationMps2*(Z-A.DeckHeightM);
        Stack.CargoIndices.Add(Index);
        Result.Placements.Add({C.ID,Best/A.Rows+1,Best%A.Rows+1,Tier+1,BestGM,BestHeel,Stack.Mass,BestLash});
        Result.CorrectedGMM=BestGM; Result.HeelDegrees=BestHeel;
    }
    Result.Feasible=true; Result.Reason=TEXT("Feasible under explicit simulation assumptions");
    return Result;
}
