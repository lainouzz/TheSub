// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TheSubGameState.generated.h"

UENUM(BlueprintType)
enum class ERoundState : uint8
{
	InProgress,
	/** Reached the beacon and made it back to the surface */
	Won,
	/** The sub flooded */
	Lost,
};

/** Round state, decided by the server (game mode) and replicated so every player sees the result */
UCLASS()
class THESUBPROJECT_API ATheSubGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category="Round")
	ERoundState GetRoundState() const { return RoundState; }

	UFUNCTION(BlueprintPure, Category="Round")
	bool IsRoundOver() const { return RoundState != ERoundState::InProgress; }

	/** Server only */
	void SetRoundState(ERoundState NewState) { RoundState = NewState; }

private:
	UPROPERTY(Replicated)
	ERoundState RoundState = ERoundState::InProgress;
};
