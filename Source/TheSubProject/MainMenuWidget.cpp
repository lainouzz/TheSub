// Copyright Epic Games, Inc. All Rights Reserved.

#include "MainMenuWidget.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MainMenu"

namespace MainMenuStyle
{
	static const FLinearColor Green(0.15f, 1.f, 0.35f, 1.f);
	static const FLinearColor Dim(0.6f, 0.7f, 0.65f, 1.f);
}

void SMainMenuWidget::Construct(const FArguments& InArgs)
{
	OnHost = InArgs._OnHost;
	OnJoin = InArgs._OnJoin;
	OnQuit = InArgs._OnQuit;

	auto MakeButton = [](const FText& Label, FOnClicked OnClicked)
	{
		return SNew(SBox)
			.HeightOverride(56.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.OnClicked(OnClicked)
				[
					SNew(STextBlock)
					.Text(Label)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
				]
			];
	};

	ChildSlot
	[
		// Dark backdrop over the whole screen
		SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.02f, 0.01f, 0.92f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(460.f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 40.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("Title", "THE SUB"))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 48))
					.ColorAndOpacity(MainMenuStyle::Green)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					MakeButton(LOCTEXT("Host", "HOST GAME"), FOnClicked::CreateSP(this, &SMainMenuWidget::HandleHost))
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 30.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("HostHelp", "Your friend joins with your IP (UDP port 7777)."))
					.ColorAndOpacity(MainMenuStyle::Dim)
				]

				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AddressLabel", "Host's IP address"))
					.ColorAndOpacity(MainMenuStyle::Dim)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SAssignNew(AddressBox, SEditableTextBox)
					.Text(FText::FromString(InArgs._DefaultAddress))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
					.SelectAllTextWhenFocused(true)
					.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type CommitType)
					{
						if (CommitType == ETextCommit::OnEnter)
						{
							HandleJoin();
						}
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 30.f)
				[
					MakeButton(LOCTEXT("Join", "JOIN GAME"), FOnClicked::CreateSP(this, &SMainMenuWidget::HandleJoin))
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					MakeButton(LOCTEXT("Quit", "QUIT"), FOnClicked::CreateSP(this, &SMainMenuWidget::HandleQuit))
				]
			]
		]
	];
}

TSharedPtr<SWidget> SMainMenuWidget::GetFocusWidget() const
{
	return AddressBox;
}

FReply SMainMenuWidget::HandleHost()
{
	OnHost.ExecuteIfBound();
	return FReply::Handled();
}

FReply SMainMenuWidget::HandleJoin()
{
	const FString Address = AddressBox.IsValid() ? AddressBox->GetText().ToString().TrimStartAndEnd() : FString();
	if (!Address.IsEmpty())
	{
		OnJoin.ExecuteIfBound(Address);
	}
	return FReply::Handled();
}

FReply SMainMenuWidget::HandleQuit()
{
	OnQuit.ExecuteIfBound();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
