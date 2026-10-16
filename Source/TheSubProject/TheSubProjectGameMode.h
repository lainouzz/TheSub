// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TheSubGameState.h"
#include "TheSubProjectGameMode.generated.h"

/** Testing shortcuts, typed in the console (see ATheSubProjectCharacter's Sub* commands) */
UENUM()
enum class ESubDebugCommand : uint8
{
	GoToBeacon,
	GoToSurface,
	Win,
	Flood,
	Leak,
};

UCLASS(minimalapi)
class ATheSubProjectGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATheSubProjectGameMode();

	/** Spawns the player inside the level's submarine if there is one, otherwise at a PlayerStart */
	virtual void RestartPlayer(AController* NewPlayer) override;

	virtual void Tick(float DeltaSeconds) override;

	/** Starts a new round in place (no map reload): everything back to the start, players respawned. Only once the round is over. */
	void RestartRound();

	/** Server: run a testing shortcut. Does nothing in Shipping builds. */
	void RunDebugCommand(ESubDebugCommand Command);

protected:
	/** Back at the surface = within this many cm of it */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Round", meta=(ClampMin="0"))
	float SurfaceDepthTolerance = 300.f;

private:
	/** Decide win/lose for the current round */
	void CheckRoundOutcome();

	/** Finds the sub and beacon if we haven't yet */
	void FindRoundActors();

	void EndRound(ERoundState Result);

	void DoRestartRound();

	/** Which spawn slot inside the submarine the next player gets */
	int32 NextSpawnSlot = 0;

	bool bRestartPending = false;

	TWeakObjectPtr<class ASubmarine> Submarine;
	TWeakObjectPtr<class ABeacon> Beacon;
};



