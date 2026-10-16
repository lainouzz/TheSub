// Copyright Epic Games, Inc. All Rights Reserved.

#include "TheSubProjectGameMode.h"
#include "TheSubProjectCharacter.h"
#include "Submarine.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ATheSubProjectGameMode::ATheSubProjectGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

}

void ATheSubProjectGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer != nullptr && !NewPlayer->IsPendingKillPending())
	{
		if (ASubmarine* Submarine = Cast<ASubmarine>(UGameplayStatics::GetActorOfClass(this, ASubmarine::StaticClass())))
		{
			// Spread players around the spawn point so co-op players don't spawn inside each other
			static const FVector SlotOffsets[] =
			{
				FVector(0.f, -80.f, 0.f), FVector(0.f, 80.f, 0.f), FVector(-150.f, -80.f, 0.f), FVector(-150.f, 80.f, 0.f),
			};
			FTransform SpawnTransform = Submarine->GetPlayerSpawnTransform();
			SpawnTransform.SetLocation(SpawnTransform.TransformPosition(SlotOffsets[NextSpawnSlot++ % UE_ARRAY_COUNT(SlotOffsets)]));
			SpawnTransform.SetScale3D(FVector::OneVector);

			RestartPlayerAtTransform(NewPlayer, SpawnTransform);
			return;
		}
	}

	Super::RestartPlayer(NewPlayer);
}
