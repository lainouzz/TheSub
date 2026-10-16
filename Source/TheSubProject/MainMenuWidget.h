// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;

DECLARE_DELEGATE_OneParam(FOnJoinAddress, const FString& /*Address*/);

/**
 * Greybox main menu: Host, Join by IP, Quit. Plain Slate so it needs no widget assets.
 * The HUD shows it when the game runs on its own (not hosting, not connected).
 */
class SMainMenuWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMainMenuWidget) {}
		SLATE_ARGUMENT(FString, DefaultAddress)
		SLATE_EVENT(FSimpleDelegate, OnHost)
		SLATE_EVENT(FOnJoinAddress, OnJoin)
		SLATE_EVENT(FSimpleDelegate, OnQuit)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** The text box to give keyboard focus to */
	TSharedPtr<SWidget> GetFocusWidget() const;

private:
	FReply HandleHost();
	FReply HandleJoin();
	FReply HandleQuit();

	FSimpleDelegate OnHost;
	FOnJoinAddress OnJoin;
	FSimpleDelegate OnQuit;

	TSharedPtr<SEditableTextBox> AddressBox;
};
