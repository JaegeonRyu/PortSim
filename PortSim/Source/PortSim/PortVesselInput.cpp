#include "QuayCrane.h"
#include "TerminalLayout.h"
#include "Dom/JsonObject.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
    FString InputRoot() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()/TEXT("../VesselData")); }
    FString SerializeInput(const FPortVesselInput& Input,int32 Kind)
    {
        FString Content=Kind?TEXT("container_id,bay,row,tier\n"):TEXT("container_id,operation,bay,row,tier\n");
        for(const auto& R:Input.Rows)
            Content+=R.ContainerId+TEXT(",")+(Kind?FString():R.Operation+TEXT(","))+
                FString::Printf(TEXT("%d,%d,%d\n"),R.Bay,R.Row,R.Tier);
        return Content;
    }
    bool ParseContent(const FString& Content,const FString& Name,int32 Kind,FPortVesselInput& Result,FString& Error)
    {
        TArray<FString> Lines; Content.ParseIntoArrayLines(Lines,false);
        const FString Header=Kind?TEXT("container_id,bay,row,tier"):TEXT("container_id,operation,bay,row,tier");
        if(Lines.IsEmpty() || Lines[0].TrimStartAndEnd()!=Header)
        { Error=TEXT("CSV 헤더 오류: 프로젝트 예시 파일 형식을 사용하세요."); return false; }
        TSet<FString> Ids,Slots;
        for(int32 I=1;I<Lines.Num();++I)
        {
            if(Lines[I].TrimStartAndEnd().IsEmpty()) continue;
            TArray<FString> Cells; Lines[I].ParseIntoArray(Cells,TEXT(","),false);
            auto Fail=[&]() { Error=FString::Printf(TEXT("%d행 오류: ID·작업·위치 또는 중복을 확인하세요."),I+1); return false; };
            if(Cells.Num()!=(Kind?4:5) || Result.Rows.Num()>=TerminalLayout::ContainersPerVessel) return Fail();
            for(auto& Cell:Cells) Cell=Cell.TrimStartAndEnd();
            FPortVesselInputRow Entry; Entry.ContainerId=Cells[0].ToUpper();
            if(Entry.ContainerId.IsEmpty() || Entry.ContainerId.Len()>32 || Ids.Contains(Entry.ContainerId)) return Fail();
            for(TCHAR C:Entry.ContainerId) if(!((C>='A' && C<='Z') || (C>='0' && C<='9') || C=='-' || C=='_')) return Fail();
            Entry.Operation=Kind?TEXT("ABOARD"):Cells[1].ToUpper();
            if(!Kind && Entry.Operation!=TEXT("DISCHARGE") && Entry.Operation!=TEXT("LOAD")) return Fail();
            const int32 Offset=Kind?1:2;
            int32* Values[]={&Entry.Bay,&Entry.Row,&Entry.Tier};
            for(int32 N=0;N<3;++N)
            {
                const FString& Value=Cells[Offset+N];
                if(Value.IsEmpty() || Value.Len()>3) return Fail();
                for(TCHAR C:Value) if(C<'0' || C>'9') return Fail();
                *Values[N]=FCString::Atoi(*Value);
                if((N!=1 && *Values[N]<1) || *Values[N]>(N==0?999:99)) return Fail();
            }
            const FString Slot=Entry.Operation+FString::Printf(TEXT("/%d/%d/%d"),Entry.Bay,Entry.Row,Entry.Tier);
            if(Slots.Contains(Slot)) return Fail();
            Ids.Add(Entry.ContainerId); Slots.Add(Slot); Result.Rows.Add(MoveTemp(Entry));
        }
        // Two explicit slot conventions: simulator 1-based slots, or sample
        // odd bays / deck tiers 82,84. Never compact gaps or wrap excess cargo.
        const bool DeckCodes=Result.Rows.ContainsByPredicate([](const FPortVesselInputRow& R){return R.Tier>=82;});
        TSet<FIntVector> GridSlots;
        for(auto& R:Result.Rows)
        {
            if(DeckCodes && (R.Bay%2!=1 || R.Tier<82 || (R.Tier-82)%2!=0))
            { Error=TEXT("위치 형식 오류: 갑판 표기는 홀수 Bay 및 Tier 82/84를 사용하세요."); return false; }
            R.GridSlot=FIntVector(DeckCodes?(R.Bay-1)/2:R.Bay-1,R.Row-1,DeckCodes?(R.Tier-82)/2:R.Tier-1);
            const FIntVector G=R.GridSlot;
            if(G.X<0 || G.X>=TerminalLayout::VesselBays || G.Y<0 || G.Y>=TerminalLayout::VesselRows ||
                G.Z<0 || G.Z>=TerminalLayout::VesselTiers)
            { Error=TEXT("선박 배치 범위 초과 또는 위치 중복: 10 Bay × 10 Row × 2 Tier"); return false; }
            GridSlots.Add(G);
        }
        TSet<FIntVector> AboardSlots;
        for(const auto& R:Result.Rows) if(R.Operation!=TEXT("LOAD")) AboardSlots.Add(R.GridSlot);
        for(const auto& G:AboardSlots) if(Kind && G.Z>0 && !AboardSlots.Contains(G-FIntVector(0,0,1)))
        { Error=TEXT("상단 컨테이너 아래에 지지하는 하단 컨테이너가 없습니다."); return false; }
        Result.FileName=FPaths::GetCleanFilename(Name);
        return true;
    }
    bool ParseInput(const FString& Path,int32 Kind,FPortVesselInput& Result,FString& Error)
    {
        const int64 Size=IFileManager::Get().FileSize(*Path);
        FString Content;
        if(Size<0 || Size>2*1024*1024 || !FFileHelper::LoadFileToString(Content,*Path))
        { Error=TEXT("파일 읽기 실패 또는 2MB 초과"); return false; }
        return ParseContent(Content,Path,Kind,Result,Error);
    }
}

const FPortVesselInput* AQuayCrane::GetVesselInput(int32 Vessel) const
{
    return Vessel>=0 && Vessel<3 && ConfiguredFileMode[Vessel]?&ConfiguredVesselInputs[Vessel]:nullptr;
}

const FPortVesselInput* AQuayCrane::GetPendingVesselInput(int32 Vessel) const
{
    return Vessel>=0 && Vessel<3 && PendingFileMode[Vessel] && !VesselInputs[Vessel][1].FileName.IsEmpty()?
        &VesselInputs[Vessel][1]:nullptr;
}

void AQuayCrane::SetVesselInputMode(int32 Vessel,bool FileMode)
{
    if(Vessel<0 || Vessel>=PendingShips || PendingFileMode[Vessel]==FileMode) return;
    if(!PendingFileMode[Vessel]) ManualCargo[Vessel]=PendingCargo[Vessel];
    PendingFileMode[Vessel]=FileMode;
    const auto* Input=GetPendingVesselInput(Vessel);
    PendingCargo[Vessel]=Input?Input->Rows.Num():FMath::Clamp(ManualCargo[Vessel],1,PendingCargoLimit(Vessel));
    VesselInputError.Empty();
    SaveRunSettings();
}

bool AQuayCrane::ApplyPendingVesselInputs()
{
    int32 TotalCargo=PendingCargoTotal();
    for(int32 V=0;V<PendingShips;++V)
    {
        const auto* Input=GetPendingVesselInput(V);
        if(PendingFileMode[V] && !Input)
        { VesselInputError=FString::Printf(TEXT("선박 %d: 선내 적재도 CSV를 선택하세요."),V+1); return false; }
        if(Input && !ValidateVesselInput(V,*Input,VesselInputError)) return false;
        if(Input && !ValidateVesselOperations(*Input,VesselInputs[V][0],VesselInputError)) return false;
        if(Input) for(const auto& Row:VesselInputs[V][0].Rows) TotalCargo+=Row.Operation==TEXT("LOAD");
    }
    if(TotalCargo>TerminalLayout::ReceivingSlotCount())
    { VesselInputError=TEXT("선박 화물과 적하 대기 화물의 합계가 야드 수용량을 초과합니다."); return false; }
    // Snapshot all pending data before rebuilding; file selection never changes live cargo.
    for(int32 V=0;V<3;++V)
    {
        const auto* Input=GetPendingVesselInput(V);
        ConfiguredFileMode[V]=Input!=nullptr;
        ConfiguredVesselInputs[V]=Input?*Input:FPortVesselInput();
        ConfiguredVesselOperations[V]=Input?VesselInputs[V][0]:FPortVesselInput();
        if(Input) PendingCargo[V]=Input->Rows.Num();
    }
    VesselInputError.Empty();
    return true;
}

bool AQuayCrane::ValidateVesselInput(int32 Vessel,const FPortVesselInput& Input,FString& Error) const
{
    const int32 Total=PendingCargoTotal()-PendingCargo[Vessel]+Input.Rows.Num();
    if(Vessel<PendingShips && Total>5594)
    { Error=TEXT("전체 화물이 야드 수용량 5594개를 초과합니다."); return false; }
    TSet<FName> Names;
    int32 Offset=0;
    for(int32 V=0;V<PendingShips;++V)
    {
        Names.Reset(); // IDs belong to each vessel; the same sample may be used on another vessel.
        const auto* Plan=V==Vessel?&Input:GetPendingVesselInput(V);
        const int32 Count=Plan?Plan->Rows.Num():PendingCargo[V];
        for(int32 I=0;I<Count;++I)
        {
            const FName Name=Plan?FName(*Plan->Rows[I].ContainerId):FName(*FString::Printf(TEXT("C%02d"),2000+Offset+I));
            if(Names.Contains(Name)) { Error=TEXT("선박 내 컨테이너 ID가 중복됩니다."); return false; }
            Names.Add(Name);
        }
        Offset+=Count;
    }
    return true;
}

void AQuayCrane::SaveVesselInputs(FJsonObject& Settings) const
{
    for(int32 V=0;V<3;++V)
    {
        Settings.SetNumberField(FString::Printf(TEXT("selected_input_%d"),V+1),SelectedVesselInput[V]);
        for(int32 K=0;K<2;++K)
        {
            const auto& Input=VesselInputs[V][K];
            if(Input.FileName.IsEmpty()) continue;
            Settings.SetStringField(FString::Printf(TEXT("input_%d_%d_csv"),V+1,K),SerializeInput(Input,K));
            Settings.SetStringField(FString::Printf(TEXT("input_%d_%d_name"),V+1,K),Input.FileName);
        }
    }
}

void AQuayCrane::RestoreVesselInputs(const FJsonObject& Settings)
{
    for(int32 V=0;V<3;++V)
    {
        for(int32 K=0;K<2;++K)
        {
            FString Content,Name,Error;
            if(!Settings.TryGetStringField(FString::Printf(TEXT("input_%d_%d_csv"),V+1,K),Content)) continue;
            Settings.TryGetStringField(FString::Printf(TEXT("input_%d_%d_name"),V+1,K),Name);
            FPortVesselInput Parsed;
            if(Content.Len()<=2*1024*1024 && ParseContent(Content,Name,K,Parsed,Error)) VesselInputs[V][K]=MoveTemp(Parsed);
            else VesselInputError=FString::Printf(TEXT("선박 %d 저장 자료 오류: %s"),V+1,*Error);
        }
        double Selected=-1;
        Settings.TryGetNumberField(FString::Printf(TEXT("selected_input_%d"),V+1),Selected);
        SelectedVesselInput[V]=(Selected==0 || Selected==1)?int32(Selected):-1;
        if(SelectedVesselInput[V]>=0 && VesselInputs[V][SelectedVesselInput[V]].FileName.IsEmpty()) SelectedVesselInput[V]=-1;
        if(const auto* Input=GetPendingVesselInput(V)) PendingCargo[V]=Input->Rows.Num();
        else if(PendingFileMode[V])
        {
            PendingFileMode[V]=false;
            PendingCargo[V]=FMath::Clamp(ManualCargo[V],1,PendingCargoLimit(V));
            VesselInputError=FString::Printf(TEXT("선박 %d: 저장된 파일 배치 없음 · 직접 설정으로 복원"),V+1);
        }
    }
}

void APortSimHUD::LoadVesselInput(int32 Vessel,int32 Kind)
{
    if(Vessel<0 || Vessel>=3 || Kind<0 || Kind>=2) return;
    auto* PC=GetOwningPlayerController();
    auto* C=PC?Cast<AQuayCrane>(PC->GetPawn()):nullptr;
    if(!C || !C->bUnifiedTerminal) return;
    auto* Desktop=FDesktopPlatformModule::Get();
    if(!Desktop) { VesselImportStatus=TEXT("파일 선택 창을 사용할 수 없습니다."); return; }
    TArray<FString> Files;
    const void* Parent=FSlateApplication::IsInitialized()?FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr):nullptr;
    const FString Folder=InputRoot()/(Kind?TEXT("Stowage"):TEXT("Operations"));
    if(!Desktop->OpenFileDialog(Parent,Kind?TEXT("선내 적재도 불러오기"):TEXT("양하/선적 목록 불러오기"),
        Folder,TEXT(""),TEXT("CSV (*.csv)|*.csv"),0,Files) || Files.IsEmpty()) return;
    FPortVesselInput Parsed; FString Error;
    if(!ParseInput(Files[0],Kind,Parsed,Error) || !C->ValidateVesselInput(Vessel,Parsed,Error))
    { VesselImportStatus=Error; return; }
    C->VesselInputs[Vessel][Kind]=MoveTemp(Parsed);
    C->SelectedVesselInput[Vessel]=Kind;
    C->PendingFileMode[Vessel]=true;
    C->PendingCargo[Vessel]=C->GetPendingVesselInput(Vessel)?C->GetPendingVesselInput(Vessel)->Rows.Num():0;
    C->VesselInputError.Empty();
    C->SaveRunSettings();
    VesselImportStatus=FString::Printf(TEXT("선박 %d · %d개 선택됨 · 리셋을 누르면 적용%s"),Vessel+1,
        C->PendingCargo[Vessel],C->bRunSettingsSaved?TEXT(""):TEXT(" · 설정 저장 실패"));
}
