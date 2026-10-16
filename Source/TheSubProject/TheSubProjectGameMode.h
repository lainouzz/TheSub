// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TheSubProjectGameMode.generated.h"

UCLASS(minimalapi)
class ATheSubProjectGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATheSubProjectGameMode();

	/** Spawns the player inside the level's submarine if there is one, otherwise at a PlayerStart */
	virtual void RestartPlayer(AController* NewPlayer) override;

private:
	/** Which spawn slot inside the submarine the next player gets */
	int32 NextSpawnSlot = 0;
};



