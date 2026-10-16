// Copyright Epic Games, Inc. All Rights Reserved.

#include "HelmStation.h"
#include "Submarine.h"
#include "TheSubProjectCharacter.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"

namespace HelmTelegraph
{
	struct FNotch
	{
		const TCHAR* Name;
		float Throttle;
	};

	static const FNotch Notches[] =
	{
		{ TEXT("Full astern"), -1.f },
		{ TEXT("Half astern"), -0.5f },
		{ TEXT("All stop"),     0.f },
		{ TEXT("Slow ahead"),   1.f / 3.f },
		{ TEXT("Half ahead"),   2.f / 3.f },
		{ TEXT("Full ahead"),   1.f },
	};

	static constexpr int32 AllStopIndex = 2;
	static constexpr int32 NumNotches = UE_ARRAY_COUNT(Notches);

	static int32 FindClosestNotch(float Throttle)
	{
		int32 Best = AllStopIndex;
		for (int32 Index = 0; Index < NumNotches; ++Index)
		{
			if (FMath::Abs(Notches[Index].Throttle - Throttle) < FMath::Abs(Notches[Best].Throttle - Throttle))
			{
				Best = Index;
			}
		}
		return Best;
	}
}

AHelmStation::AHelmStation()
{
	StationName = NSLOCTEXT("HelmStation", "Name", "Helm");
	TelegraphIndex = HelmTelegraph::AllStopIndex;
}

void AHelmStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AHelmStation, TelegraphIndex);
}

void AHelmStation::Reset()
{
	Super::Reset();

	TelegraphIndex = HelmTelegraph::AllStopIndex;
}

FString AHelmStation::GetTelegraphName() const
{
	return HelmTelegraph::Notches[FMath::Clamp(TelegraphIndex, 0, HelmTelegraph::NumNotches - 1)].Name;
}

FString AHelmStation::GetStatusText() const
{
	const ASubmarine* Submarine = GetSubmarine();
	if (Submarine == nullptr)
	{
		return TEXT("HELM\nNot attached to a submarine");
	}

	return FString::Printf(
		TEXT("HELM\n")
		TEXT("Telegraph: %s   [W/S]\n")
		TEXT("Rudder: %+.0f%%   [A/D]\n")
		TEXT("Dive: %+.0f%%   [Space up / Ctrl down]\n")
		TEXT("Speed: %.0f cm/s\n")
		TEXT("[E] Leave helm"),
		*GetTelegraphName(),
		Submarine->GetRudder() * 100.f,
		Submarine->GetDive() * 100.f,
		Submarine->GetCurrentSpeed());
}

void AHelmStation::OnEntered(ATheSubProjectCharacter* Character)
{
	if (ASubmarine* Submarine = GetSubmarine())
	{
		// A person at the helm overrides the test autopilot
		Submarine->SetAutopilotSuspended(true);

		// Pick up from whatever the sub is doing now instead of jerking to a new setting
		TelegraphIndex = HelmTelegraph::FindClosestNotch(Submarine->GetThrottle());
		ApplyTelegraph();
		Submarine->SetRudder(0.f);
		Submarine->SetDive(0.f);
	}
}

void AHelmStation::OnLeft(ATheSubProjectCharacter* Character)
{
	if (ASubmarine* Submarine = GetSubmarine())
	{
		// Let go of the wheel and the dive controls; the telegraph stays where it was
		Submarine->SetRudder(0.f);
		Submarine->SetDive(0.f);
		Submarine->SetAutopilotSuspended(false);
	}
}

void AHelmStation::BuildDefaultInput()
{
	if (StationMappingContext != nullptr)
	{
		return;
	}

	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType ValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = ValueType;
		return Action;
	};

	if (TelegraphUpAction == nullptr)
	{
		TelegraphUpAction = MakeAction(TEXT("IA_Helm_TelegraphUp"), EInputActionValueType::Boolean);
	}
	if (TelegraphDownAction == nullptr)
	{
		TelegraphDownAction = MakeAction(TEXT("IA_Helm_TelegraphDown"), EInputActionValueType::Boolean);
	}
	// One action per key, so we can tell which of two opposing keys was pressed last
	if (RudderLeftAction == nullptr)
	{
		RudderLeftAction = MakeAction(TEXT("IA_Helm_RudderLeft"), EInputActionValueType::Boolean);
	}
	if (RudderRightAction == nullptr)
	{
		RudderRightAction = MakeAction(TEXT("IA_Helm_RudderRight"), EInputActionValueType::Boolean);
	}
	if (DiveUpAction == nullptr)
	{
		DiveUpAction = MakeAction(TEXT("IA_Helm_DiveUp"), EInputActionValueType::Boolean);
	}
	if (DiveDownAction == nullptr)
	{
		DiveDownAction = MakeAction(TEXT("IA_Helm_DiveDown"), EInputActionValueType::Boolean);
	}

	StationMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Helm"));
	StationMappingContext->MapKey(TelegraphUpAction, EKeys::W);
	StationMappingContext->MapKey(TelegraphUpAction, EKeys::Up);
	StationMappingContext->MapKey(TelegraphDownAction, EKeys::S);
	StationMappingContext->MapKey(TelegraphDownAction, EKeys::Down);
	StationMappingContext->MapKey(RudderLeftAction, EKeys::A);
	StationMappingContext->MapKey(RudderRightAction, EKeys::D);
	StationMappingContext->MapKey(DiveUpAction, EKeys::SpaceBar);
	StationMappingContext->MapKey(DiveDownAction, EKeys::LeftControl);
}

void AHelmStation::BindStationInput(UEnhancedInputComponent& EnhancedInputComponent)
{
	StationBindingHandles.Add(EnhancedInputComponent.BindAction(TelegraphUpAction, ETriggerEvent::Started, this, &AHelmStation::TelegraphUp).GetHandle());
	StationBindingHandles.Add(EnhancedInputComponent.BindAction(TelegraphDownAction, ETriggerEvent::Started, this, &AHelmStation::TelegraphDown).GetHandle());

	BindHeldKey(EnhancedInputComponent, RudderLeftAction, RudderKeys, -1.f);
	BindHeldKey(EnhancedInputComponent, RudderRightAction, RudderKeys, 1.f);
	BindHeldKey(EnhancedInputComponent, DiveDownAction, DiveKeys, -1.f);
	BindHeldKey(EnhancedInputComponent, DiveUpAction, DiveKeys, 1.f);
}

void AHelmStation::BindHeldKey(UEnhancedInputComponent& EnhancedInputComponent, UInputAction* Action, FHeldAxis& Axis, float Direction)
{
	StationBindingHandles.Add(EnhancedInputComponent.BindActionValueLambda(Action, ETriggerEvent::Started,
		[this, &Axis, Direction](const FInputActionValue&)
		{
			Axis.Press(Direction);
			SendHeldAxes();
		}).GetHandle());

	StationBindingHandles.Add(EnhancedInputComponent.BindActionValueLambda(Action, ETriggerEvent::Completed,
		[this, &Axis, Direction](const FInputActionValue&)
		{
			Axis.Release(Direction);
			SendHeldAxes();
		}).GetHandle());
}

void AHelmStation::SendHeldAxes()
{
	UE_LOG(LogSubmarine, Log, TEXT("Helm input (%s): rudder keys %.0f, dive keys %.0f"),
		*GetNameSafe(GetOccupant()), RudderKeys.GetValue(), DiveKeys.GetValue());

	const float Rudder = RudderKeys.GetValue();
	if (Rudder != SentRudder)
	{
		SentRudder = Rudder;
		ServerSetRudder(Rudder);
	}

	const float Dive = DiveKeys.GetValue();
	if (Dive != SentDive)
	{
		SentDive = Dive;
		ServerSetDive(Dive);
	}
}

void AHelmStation::OnLocalInputRemoved()
{
	RudderKeys = FHeldAxis();
	DiveKeys = FHeldAxis();
	SentRudder = 0.f;
	SentDive = 0.f;
}

void AHelmStation::TelegraphUp()
{
	UE_LOG(LogSubmarine, Log, TEXT("Helm input (%s): telegraph up pressed"), *GetNameSafe(GetOccupant()));
	ServerStepTelegraph(1);
}

void AHelmStation::TelegraphDown()
{
	UE_LOG(LogSubmarine, Log, TEXT("Helm input (%s): telegraph down pressed"), *GetNameSafe(GetOccupant()));
	ServerStepTelegraph(-1);
}

void AHelmStation::ServerStepTelegraph_Implementation(int8 Direction)
{
	if (GetOccupant() == nullptr)
	{
		return;
	}

	TelegraphIndex = FMath::Clamp(TelegraphIndex + FMath::Sign(Direction), 0, HelmTelegraph::NumNotches - 1);
	ApplyTelegraph();

	UE_LOG(LogSubmarine, Log, TEXT("Helm (%s): telegraph %s"), *GetNameSafe(GetOccupant()), *GetTelegraphName());
}

void AHelmStation::ServerSetRudder_Implementation(float Value)
{
	if (GetOccupant() == nullptr)
	{
		return;
	}

	if (ASubmarine* Submarine = GetSubmarine())
	{
		Submarine->SetRudder(Value);
	}

	UE_LOG(LogSubmarine, Log, TEXT("Helm (%s): rudder %.0f"), *GetNameSafe(GetOccupant()), Value);
}

void AHelmStation::ServerSetDive_Implementation(float Value)
{
	if (GetOccupant() == nullptr)
	{
		return;
	}

	if (ASubmarine* Submarine = GetSubmarine())
	{
		Submarine->SetDive(Value);
	}

	UE_LOG(LogSubmarine, Log, TEXT("Helm (%s): dive %.0f"), *GetNameSafe(GetOccupant()), Value);
}

void AHelmStation::ApplyTelegraph()
{
	if (ASubmarine* Submarine = GetSubmarine())
	{
		Submarine->SetThrottle(HelmTelegraph::Notches[TelegraphIndex].Throttle);
	}
}
