// Copyright Epic Games, Inc. All Rights Reserved.

#include "HullShakeCameraModifier.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

namespace HullShake
{
	/** How quickly the shake dies down (higher = shorter) */
	static constexpr float DecayRate = 3.f;
	static constexpr float MaxStrength = 6.f;
}

void UHullShakeCameraModifier::Shake(APlayerController* PlayerController, float Strength)
{
	if (PlayerController == nullptr || PlayerController->PlayerCameraManager == nullptr || Strength <= 0.f)
	{
		return;
	}

	APlayerCameraManager* CameraManager = PlayerController->PlayerCameraManager;
	UHullShakeCameraModifier* Modifier = Cast<UHullShakeCameraModifier>(CameraManager->FindCameraModifierByClass(StaticClass()));
	if (Modifier == nullptr)
	{
		Modifier = Cast<UHullShakeCameraModifier>(CameraManager->AddNewCameraModifier(StaticClass()));
	}
	if (Modifier != nullptr)
	{
		Modifier->AddShake(FMath::Min(Strength, HullShake::MaxStrength));
	}
}

bool UHullShakeCameraModifier::ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	Super::ModifyCamera(DeltaTime, InOutPOV);

	if (ShakeAmount <= 0.01f)
	{
		ShakeAmount = 0.f;
		return false;
	}

	// A few out-of-phase sines read as a rough, metallic jolt rather than a smooth wobble
	Time += DeltaTime;
	InOutPOV.Rotation.Pitch += ShakeAmount * FMath::Sin(Time * 37.f);
	InOutPOV.Rotation.Yaw += ShakeAmount * 0.7f * FMath::Sin(Time * 29.f + 1.f);
	InOutPOV.Rotation.Roll += ShakeAmount * 1.4f * FMath::Sin(Time * 23.f + 2.f);

	ShakeAmount *= FMath::Exp(-HullShake::DecayRate * DeltaTime);
	return false;
}
