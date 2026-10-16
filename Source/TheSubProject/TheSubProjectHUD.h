// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TheSubProjectHUD.generated.h"

class ASubmarine;
class ABeacon;

/**
 * Greybox HUD: a crosshair dot, the interaction prompt, and the status of the station you're operating.
 * Drawn per player, so it stays correct with several PIE players in one process.
 */
UCLASS()
class THESUBPROJECT_API ATheSubProjectHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Main menu, shown when the game runs on its own (not hosting, not connected to a host) */
	void ShowMainMenu();
	void HideMainMenu();

	void HostGame();
	void JoinGame(const FString& Address);
	void QuitGame();

	TSharedPtr<class SMainMenuWidget> MainMenu;

	void DrawTextLines(const FString& Text, float X, float Y, bool bCentered, float Scale = 1.f, const FLinearColor& Color = FLinearColor::White);

	/** The sub in the level, found on first draw */
	TWeakObjectPtr<ASubmarine> Submarine;

	/** The objective beacon, once the trench has spawned it */
	TWeakObjectPtr<ABeacon> Beacon;
};
