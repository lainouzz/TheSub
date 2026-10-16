// Copyright Epic Games, Inc. All Rights Reserved.

#include "TheSubProjectHUD.h"
#include "TheSubProjectCharacter.h"
#include "SubStation.h"
#include "Submarine.h"
#include "Beacon.h"
#include "MainMenuWidget.h"
#include "TheSubGameState.h"
#include "EngineUtils.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"

namespace MainMenuConfig
{
	static const TCHAR* Section = TEXT("TheSub.MainMenu");
	static const TCHAR* LastAddressKey = TEXT("LastAddress");
}

void ATheSubProjectHUD::BeginPlay()
{
	Super::BeginPlay();

	// Standalone = not hosting and not connected: that's the "main menu" state.
	// Hosting (listen server) or joining reloads the map in another net mode, so the menu doesn't come back.
	if (GetNetMode() == NM_Standalone && PlayerOwner != nullptr && PlayerOwner->IsLocalController())
	{
		ShowMainMenu();
	}
}

void ATheSubProjectHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Viewport widgets outlive the world, so take the menu down before travelling
	HideMainMenu();

	Super::EndPlay(EndPlayReason);
}

void ATheSubProjectHUD::ShowMainMenu()
{
	if (MainMenu.IsValid() || GEngine == nullptr || GEngine->GameViewport == nullptr)
	{
		return;
	}

	FString LastAddress = TEXT("127.0.0.1");
	GConfig->GetString(MainMenuConfig::Section, MainMenuConfig::LastAddressKey, LastAddress, GGameUserSettingsIni);

	MainMenu = SNew(SMainMenuWidget)
		.DefaultAddress(LastAddress)
		.OnHost(FSimpleDelegate::CreateUObject(this, &ATheSubProjectHUD::HostGame))
		.OnJoin(FOnJoinAddress::CreateUObject(this, &ATheSubProjectHUD::JoinGame))
		.OnQuit(FSimpleDelegate::CreateUObject(this, &ATheSubProjectHUD::QuitGame));
	GEngine->GameViewport->AddViewportWidgetContent(MainMenu.ToSharedRef(), 100);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(MainMenu->GetFocusWidget());
	PlayerOwner->SetInputMode(InputMode);
	PlayerOwner->SetShowMouseCursor(true);
}

void ATheSubProjectHUD::HideMainMenu()
{
	if (!MainMenu.IsValid())
	{
		return;
	}

	if (GEngine != nullptr && GEngine->GameViewport != nullptr)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MainMenu.ToSharedRef());
	}
	MainMenu.Reset();

	if (PlayerOwner != nullptr)
	{
		PlayerOwner->SetInputMode(FInputModeGameOnly());
		PlayerOwner->SetShowMouseCursor(false);
	}
}

void ATheSubProjectHUD::HostGame()
{
	HideMainMenu();

	// Reload this map as a listen server; others can now join on port 7777
	UE_LOG(LogSubmarine, Log, TEXT("Hosting %s"), *UGameplayStatics::GetCurrentLevelName(this));
	UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)), true, TEXT("listen"));
}

void ATheSubProjectHUD::JoinGame(const FString& Address)
{
	GConfig->SetString(MainMenuConfig::Section, MainMenuConfig::LastAddressKey, *Address, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);

	HideMainMenu();

	// If the connection fails the engine falls back to the default map in standalone, which shows the menu again
	UE_LOG(LogSubmarine, Log, TEXT("Joining %s"), *Address);
	PlayerOwner->ClientTravel(Address, TRAVEL_Absolute);
}

void ATheSubProjectHUD::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, PlayerOwner, EQuitPreference::Quit, false);
}

void ATheSubProjectHUD::DrawHUD()
{
	Super::DrawHUD();

	// The menu covers the screen; nothing else to draw
	if (MainMenu.IsValid())
	{
		return;
	}

	const ATheSubProjectCharacter* Character = Cast<ATheSubProjectCharacter>(GetOwningPawn());
	if (Character == nullptr || Canvas == nullptr)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;

	// Everyone sees the flooding state, top right
	if (!Submarine.IsValid())
	{
		TActorIterator<ASubmarine> It(GetWorld());
		Submarine = It ? *It : nullptr;
	}
	if (const ASubmarine* Sub = Submarine.Get())
	{
		if (Sub->GetNumLeaks() > 0 || Sub->GetWaterLevel() > 1.f)
		{
			const FString Status = FString::Printf(TEXT("LEAKS: %d\nWATER: %.0f / %.0f cm"),
				Sub->GetNumLeaks(), Sub->GetWaterLevel(), Sub->GetMaxWaterLevel());
			DrawTextLines(Status, Canvas->ClipX - 260.f, 40.f, false);
		}

		// Objective, top center (only once a trench has spawned its beacon)
		if (!Beacon.IsValid())
		{
			TActorIterator<ABeacon> It(GetWorld());
			Beacon = It ? *It : nullptr;
		}
		if (const ABeacon* Objective = Beacon.Get())
		{
			DrawTextLines(Objective->IsReached()
				? TEXT("BEACON REACHED - return to the surface")
				: TEXT("OBJECTIVE: reach the beacon at the bottom of the trench"),
				CenterX, 20.f, true, 1.f, Objective->IsReached() ? FLinearColor::Green : FLinearColor(1.f, 0.6f, 0.05f));
		}

	}

	// Round result, same for everyone (decided by the server)
	if (const ATheSubGameState* RoundState = GetWorld()->GetGameState<ATheSubGameState>())
	{
		if (RoundState->GetRoundState() == ERoundState::Won)
		{
			DrawTextLines(TEXT("SURFACED\nYOU WIN"), CenterX, CenterY - 160.f, true, 3.f, FLinearColor::Green);
		}
		else if (RoundState->GetRoundState() == ERoundState::Lost)
		{
			DrawTextLines(TEXT("FLOODED\nGAME OVER"), CenterX, CenterY - 160.f, true, 3.f, FLinearColor::Red);
		}

		if (RoundState->IsRoundOver())
		{
			DrawTextLines(TEXT("Press R to play again"), CenterX, CenterY + 80.f, true, 1.5f, FLinearColor::White);
		}
	}

	if (const ASubStation* Station = Character->GetCurrentStation())
	{
		DrawTextLines(Station->GetStatusText(), 40.f, 40.f, false);
		return;
	}

	// Crosshair dot so you can tell what you're aiming at
	DrawRect(FLinearColor::White, CenterX - 2.f, CenterY - 2.f, 4.f, 4.f);

	if (!Character->GetInteractionPrompt().IsEmpty())
	{
		DrawTextLines(Character->GetInteractionPrompt(), CenterX, CenterY + 30.f, true);
	}
}

void ATheSubProjectHUD::DrawTextLines(const FString& Text, float X, float Y, bool bCentered, float Scale, const FLinearColor& Color)
{
	UFont* Font = GEngine->GetMediumFont();

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	for (const FString& Line : Lines)
	{
		float Width = 0.f;
		float Height = 0.f;
		GetTextSize(Line, Width, Height, Font, Scale);

		DrawText(Line, Color, bCentered ? X - Width * 0.5f : X, Y, Font, Scale);
		Y += Height + 4.f * Scale;
	}
}
