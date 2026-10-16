// Copyright Epic Games, Inc. All Rights Reserved.

#include "TheSubGameState.h"
#include "Net/UnrealNetwork.h"

void ATheSubGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ATheSubGameState, RoundState);
}
