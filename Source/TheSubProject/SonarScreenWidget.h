// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SonarScreenWidget.generated.h"

class ASonarStation;

/**
 * Sonar display drawn straight in NativePaint, no widget asset needed.
 * Sub-relative, forward is up: range rings, the sweeping ping ring, and contacts that appear
 * as the ring reaches them and fade out over time.
 */
UCLASS()
class THESUBPROJECT_API USonarScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetSonarStation(ASonarStation* InStation);

protected:
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	TWeakObjectPtr<ASonarStation> Station;
};
