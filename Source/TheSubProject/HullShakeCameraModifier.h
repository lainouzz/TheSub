// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "HullShakeCameraModifier.generated.h"

class APlayerController;

/**
 * Shakes the camera when the hull takes a hit. Cosmetic and local only: each client adds it to its own camera.
 * Stronger shakes override weaker ones and everything decays back to still.
 */
UCLASS()
class THESUBPROJECT_API UHullShakeCameraModifier : public UCameraModifier
{
	GENERATED_BODY()

public:
	/** Shake the local player's camera; Strength is roughly the peak angle in degrees */
	static void Shake(APlayerController* PlayerController, float Strength);

	virtual bool ModifyCamera(float DeltaTime, struct FMinimalViewInfo& InOutPOV) override;

private:
	void AddShake(float Strength) { ShakeAmount = FMath::Max(ShakeAmount, Strength); }

	/** Current peak angle in degrees; decays towards 0 */
	float ShakeAmount = 0.f;

	float Time = 0.f;
};
