#include "QuayCrane.h"
#include "PortEnvironmentComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void APortSimHUD::DrawWindCompass(const UPortEnvironmentComponent& Environment,float Scale)
{
    const float Size=150.f*Scale, Margin=24.f*Scale;
    const float X=Canvas->SizeX-Margin-Size, Y=Canvas->SizeY-Margin-Size;
    const bool Enabled=Environment.IsWindEnabled();
    const FLinearColor Ink=Enabled?FLinearColor(.35f,.95f,.9f):FLinearColor(.48f,.53f,.57f);
    DrawRect(FLinearColor(.015f,.035f,.055f,.92f),X,Y,Size,Size);
    auto Text=[&](const FString& Value,float CX,float CY,FLinearColor Color,float FontScale=1.f)
    {
        float W=0,H=0; GetTextSize(Value,W,H,GEngine->GetSmallFont(),Scale*FontScale);
        DrawText(Value,Color,X+CX*Scale-W*.5f,Y+CY*Scale,GEngine->GetSmallFont(),Scale*FontScale);
    };
    Text(Enabled?TEXT("WIND ON"):TEXT("WIND OFF"),75,6,Ink);
    const FVector2D Center(X+75*Scale,Y+61*Scale);
    const float Radius=27*Scale;
    for(int32 I=0;I<32;++I)
    {
        const float A=I*2*PI/32, B=(I+1)*2*PI/32;
        DrawLine(Center.X+Radius*FMath::Cos(A),Center.Y+Radius*FMath::Sin(A),
            Center.X+Radius*FMath::Cos(B),Center.Y+Radius*FMath::Sin(B),FLinearColor(.2f,.32f,.4f),Scale);
    }
    Text(TEXT("N"),75,23,Ink); Text(TEXT("E"),114,55,Ink);
    Text(TEXT("S"),75,88,Ink); Text(TEXT("W"),36,55,Ink);
    // Compass arrow points toward the wind SOURCE (meteorological FROM),
    // not GetWindDirectionVector(), which points in the opposite travel direction.
    const float Angle=FMath::DegreesToRadians(Environment.GetWindDirectionDegrees());
    const FVector2D Direction(FMath::Sin(Angle),-FMath::Cos(Angle));
    const FVector2D Side(-Direction.Y,Direction.X);
    const FVector2D Tip=Center+Direction*23*Scale;
    const FVector2D Tail=Center-Direction*16*Scale;
    const FVector2D Left=Tip-Direction*9*Scale+Side*5*Scale;
    const FVector2D Right=Tip-Direction*9*Scale-Side*5*Scale;
    DrawLine(Tail.X,Tail.Y,Tip.X,Tip.Y,Ink,2*Scale);
    DrawLine(Tip.X,Tip.Y,Left.X,Left.Y,Ink,2*Scale);
    DrawLine(Tip.X,Tip.Y,Right.X,Right.Y,Ink,2*Scale);
    Text(FString::Printf(TEXT("FROM %s  %.1f m/s"),*Environment.GetWindDirectionName(),
        Enabled?Environment.GetWindSpeedMetersPerSecond():0.f),75,108,Ink);
    Text(Enabled?TEXT("[Ctrl+0] OFF"):TEXT("[Ctrl+0] ON"),75,130,FLinearColor(.7f,.77f,.82f));
}
