// Copyright Epic Games, Inc. All Rights Reserved.

#include "SonarScreenWidget.h"
#include "SonarStation.h"
#include "Submarine.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

namespace SonarScreen
{
	static const FLinearColor Background(0.f, 0.03f, 0.01f, 1.f);
	static const FLinearColor Green(0.15f, 1.f, 0.35f, 1.f);
	static const FLinearColor Amber(1.f, 0.6f, 0.05f, 1.f);
	static const FLinearColor Below(0.2f, 0.5f, 1.f, 1.f);
	static const FLinearColor Above(0.9f, 1.f, 0.95f, 1.f);
	static const FLinearColor Warning(1.f, 0.15f, 0.1f, 1.f);
	static constexpr float DotSize = 8.f;
	static constexpr float TargetSize = 16.f;

	/** Contacts this far (cm) above/below the sub get the full above/below tint */
	static constexpr float HeightTintRange = 1000.f;

	static TArray<FVector2f> MakeCircle(const FVector2f& Center, float Radius, int32 Segments = 64)
	{
		TArray<FVector2f> Points;
		Points.Reserve(Segments + 1);
		for (int32 Index = 0; Index <= Segments; ++Index)
		{
			const float Angle = 2.f * UE_PI * Index / Segments;
			Points.Add(Center + FVector2f(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		return Points;
	}

	/** Green at our height, fading to blue below and white above */
	static FLinearColor TintForHeight(float HeightDifference)
	{
		const float T = FMath::Clamp(HeightDifference / HeightTintRange, -1.f, 1.f);
		return T < 0.f ? FLinearColor::LerpUsingHSV(Green, Below, -T) : FLinearColor::LerpUsingHSV(Green, Above, T);
	}

	static FString Meters(float Centimeters)
	{
		return FString::Printf(TEXT("%.0f m"), Centimeters / 100.f);
	}
}

void USonarScreenWidget::SetSonarStation(ASonarStation* InStation)
{
	Station = InStation;
}

int32 USonarScreenWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	LayerId = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);

	using namespace SonarScreen;

	const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush("GenericWhiteBox");
	const FPaintGeometry FullGeometry = AllottedGeometry.ToPaintGeometry();
	const FVector2f Size = FVector2f(AllottedGeometry.GetLocalSize());

	// Layout: square radar on the left, depth panel on the right
	const float RadarSize = Size.Y;
	const FVector2f Center(RadarSize * 0.5f, RadarSize * 0.5f);
	const float Radius = RadarSize * 0.45f;
	const float PanelX = RadarSize + 16.f;

	auto DrawLabel = [&](const FString& Text, const FVector2f& Position, int32 FontSize, const FLinearColor& Color)
	{
		FSlateDrawElement::MakeText(OutDrawElements, LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2f(Size.X - Position.X, FontSize * 1.6f), FSlateLayoutTransform(Position)),
			Text, FCoreStyle::GetDefaultFontStyle("Bold", FontSize), ESlateDrawEffect::None, Color);
	};
	auto DrawLine = [&](const FVector2f& From, const FVector2f& To, const FLinearColor& Color, float Thickness)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, FullGeometry, TArray<FVector2f>{ From, To }, ESlateDrawEffect::None, Color, true, Thickness);
	};

	// Screen, range rings, crosshair, divider
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId++, FullGeometry, WhiteBrush, ESlateDrawEffect::None, Background);
	for (const float Fraction : { 1.f / 3.f, 2.f / 3.f, 1.f })
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, FullGeometry, MakeCircle(Center, Radius * Fraction),
			ESlateDrawEffect::None, Green.CopyWithNewOpacity(0.3f), true, 2.f);
	}
	DrawLine(Center - FVector2f(Radius, 0.f), Center + FVector2f(Radius, 0.f), Green.CopyWithNewOpacity(0.15f), 1.f);
	DrawLine(Center - FVector2f(0.f, Radius), Center + FVector2f(0.f, Radius), Green.CopyWithNewOpacity(0.15f), 1.f);
	DrawLine(FVector2f(RadarSize, 16.f), FVector2f(RadarSize, Size.Y - 16.f), Green.CopyWithNewOpacity(0.3f), 2.f);

	// The sub, pointing up (forward)
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, FullGeometry,
		{ Center + FVector2f(-8.f, 10.f), Center + FVector2f(0.f, -14.f), Center + FVector2f(8.f, 10.f), Center + FVector2f(-8.f, 10.f) },
		ESlateDrawEffect::None, Green, true, 2.f);

	// Title (also makes a mirrored screen obvious)
	DrawLabel(TEXT("SONAR"), FVector2f(16.f, 10.f), 22, Green);
	++LayerId;

	const ASonarStation* Sonar = Station.Get();
	const ASubmarine* Submarine = Sonar != nullptr ? Sonar->GetSubmarine() : nullptr;
	if (Submarine == nullptr)
	{
		return LayerId;
	}

	// Depth gauge is live, it doesn't need a ping
	DrawLabel(TEXT("DEPTH"), FVector2f(PanelX, 16.f), 16, Green.CopyWithNewOpacity(0.7f));
	DrawLabel(Meters(Submarine->GetDepth()), FVector2f(PanelX, 40.f), 30, Green);

	const FSonarPing& Ping = Sonar->GetLastPing();
	const bool bHasPing = Ping.ServerTime >= 0.0;
	const float Age = bHasPing ? Sonar->GetTimeSinceLastPing() : UE_BIG_NUMBER;
	const float Opacity = 1.f - Age / Sonar->GetContactFadeTime();

	// Vertical clearance gauge: the sub in the middle, ground below and ceiling above at scale
	const float GaugeTop = 120.f;
	const float GaugeBottom = Size.Y - 40.f;
	const float GaugeMid = (GaugeTop + GaugeBottom) * 0.5f;
	const float GaugeX = PanelX + 30.f;
	const float GaugePixelsPerCm = (GaugeBottom - GaugeMid) / Sonar->GetRange();
	DrawLine(FVector2f(GaugeX, GaugeTop), FVector2f(GaugeX, GaugeBottom), Green.CopyWithNewOpacity(0.25f), 2.f);
	FSlateDrawElement::MakeLines(OutDrawElements, LayerId, FullGeometry,
		{ FVector2f(GaugeX - 14.f, GaugeMid - 7.f), FVector2f(GaugeX + 14.f, GaugeMid), FVector2f(GaugeX - 14.f, GaugeMid + 7.f), FVector2f(GaugeX - 14.f, GaugeMid - 7.f) },
		ESlateDrawEffect::None, Green, true, 2.f);

	if (Opacity <= 0.f)
	{
		DrawLabel(TEXT("PING FOR DEPTH"), FVector2f(GaugeX + 24.f, GaugeMid + 30.f), 12, Green.CopyWithNewOpacity(0.5f));
	}
	else
	{
		// Ground: red when it's close
		if (Ping.BelowDistance >= 0.f)
		{
			const float Y = FMath::Min(GaugeMid + 20.f + Ping.BelowDistance * GaugePixelsPerCm, GaugeBottom);
			const FLinearColor Color = (Ping.BelowDistance < Sonar->GetGroundWarningDistance() ? Warning : Below).CopyWithNewOpacity(Opacity);
			DrawLine(FVector2f(GaugeX - 20.f, Y), FVector2f(GaugeX + 20.f, Y), Color, 4.f);
			DrawLabel(FString::Printf(TEXT("GROUND %s"), *Meters(Ping.BelowDistance)), FVector2f(GaugeX + 26.f, Y - 12.f), 14, Color);
		}
		else
		{
			DrawLabel(FString::Printf(TEXT("GROUND >%s"), *Meters(Sonar->GetRange())), FVector2f(GaugeX + 26.f, GaugeBottom - 12.f), 14, Below.CopyWithNewOpacity(Opacity));
		}

		// Ceiling
		if (Ping.AboveDistance >= 0.f)
		{
			const float Y = FMath::Max(GaugeMid - 20.f - Ping.AboveDistance * GaugePixelsPerCm, GaugeTop);
			const FLinearColor Color = Above.CopyWithNewOpacity(Opacity);
			DrawLine(FVector2f(GaugeX - 20.f, Y), FVector2f(GaugeX + 20.f, Y), Color, 4.f);
			DrawLabel(FString::Printf(TEXT("ABOVE %s"), *Meters(Ping.AboveDistance)), FVector2f(GaugeX + 26.f, Y - 12.f), 14, Color);
		}
		else
		{
			DrawLabel(TEXT("ABOVE CLEAR"), FVector2f(GaugeX + 26.f, GaugeTop - 12.f), 14, Above.CopyWithNewOpacity(Opacity));
		}
	}
	++LayerId;

	if (!bHasPing)
	{
		return LayerId;
	}

	// Ping ring sweeping out
	const float Sweep = Age / Sonar->GetPingTravelTime();
	if (Sweep < 1.f)
	{
		FSlateDrawElement::MakeLines(OutDrawElements, LayerId, FullGeometry, MakeCircle(Center, Radius * Sweep),
			ESlateDrawEffect::None, Green.CopyWithNewOpacity(1.f - Sweep), true, 3.f);
	}

	if (Opacity <= 0.f)
	{
		return LayerId + 1;
	}

	// Contacts are stored in world space; draw them relative to where the sub is now, forward up
	const FVector SubLocation = Submarine->GetActorLocation();
	const FRotator SubYaw(0.f, Submarine->GetActorRotation().Yaw, 0.f);
	const float PixelsPerCm = Radius / Sonar->GetRange();
	auto DrawBlips = [&](const TArray<FVector_NetQuantize>& Points, float BlipSize, TFunctionRef<FLinearColor(const FVector&)> ColorFor)
	{
		for (const FVector& Point : Points)
		{
			const FVector Local = SubYaw.UnrotateVector(Point - SubLocation);
			const FVector2f Offset(Local.Y * PixelsPerCm, -Local.X * PixelsPerCm);
			const float Distance = Offset.Size();

			// Only show blips on the screen that the ring has already swept past
			if (Distance > Radius || Distance > Radius * Sweep)
			{
				continue;
			}

			const FVector2f BlipPosition = Center + Offset - FVector2f(BlipSize * 0.5f);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId,
				AllottedGeometry.ToPaintGeometry(FVector2f(BlipSize), FSlateLayoutTransform(BlipPosition)),
				WhiteBrush, ESlateDrawEffect::None, ColorFor(Local).CopyWithNewOpacity(Opacity));
		}
	};

	DrawBlips(Ping.Contacts, DotSize, [](const FVector& Local) { return TintForHeight(Local.Z); });
	++LayerId;
	DrawBlips(Ping.Targets, TargetSize, [](const FVector&) { return Amber; });

	return LayerId + 1;
}
