// Copyright Epic Games, Inc. All Rights Reserved.

#include "TheSubProjectGameMode.h"
#include "TheSubProjectCharacter.h"
#include "Submarine.h"
#include "TheSubProjectHUD.h"
#include "Beacon.h"
#include "TimerManager.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

ATheSubProjectGameMode::ATheSubProjectGameMode()
	: Super()
{
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnClassFinder(TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter"));
	DefaultPawnClass = PlayerPawnClassFinder.Class;

	HUDClass = ATheSubProjectHUD::StaticClass();
	GameStateClass = ATheSubGameState::StaticClass();

	// Checks the round outcome a few times a second
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
}

void ATheSubProjectGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	CheckRoundOutcome();
}

void ATheSubProjectGameMode::CheckRoundOutcome()
{
	ATheSubGameState* RoundState = GetGameState<ATheSubGameState>();
	if (RoundState == nullptr || RoundState->IsRoundOver())
	{
		return;
	}

	FindRoundActors();
	if (!Submarine.IsValid())
	{
		return;
	}

	if (Submarine->IsFlooded())
	{
		EndRound(ERoundState::Lost);
	}
	else if (Beacon.IsValid() && Beacon->IsReached() && Submarine->GetDepth() <= SurfaceDepthTolerance)
	{
		EndRound(ERoundState::Won);
	}
}

void ATheSubProjectGameMode::FindRoundActors()
{
	if (!Submarine.IsValid())
	{
		Submarine = Cast<ASubmarine>(UGameplayStatics::GetActorOfClass(this, ASubmarine::StaticClass()));
	}
	if (!Beacon.IsValid())
	{
		Beacon = Cast<ABeacon>(UGameplayStatics::GetActorOfClass(this, ABeacon::StaticClass()));
	}
}

void ATheSubProjectGameMode::RunDebugCommand(ESubDebugCommand Command)
{
#if UE_BUILD_SHIPPING
	return;
#else
	FindRoundActors();
	ASubmarine* Sub = Submarine.Get();
	ABeacon* Objective = Beacon.Get();
	if (Sub == nullptr)
	{
		UE_LOG(LogSubmarine, Warning, TEXT("Debug: no submarine in the level"));
		return;
	}

	switch (Command)
	{
	case ESubDebugCommand::GoToBeacon:
		if (Objective != nullptr)
		{
			// Hover just in front of the beacon (it faces back up the trench), close enough to count as reached
			const FVector Location = Objective->GetActorLocation() + Objective->GetActorForwardVector() * 1200.f + FVector(0.f, 0.f, 700.f);
			Sub->DebugTeleport(Location, FRotator(0.f, Objective->GetActorRotation().Yaw + 180.f, 0.f));
		}
		else
		{
			UE_LOG(LogSubmarine, Warning, TEXT("Debug: no beacon (place a Trench in the level)"));
		}
		break;

	case ESubDebugCommand::GoToSurface:
		Sub->DebugTeleport(Sub->GetStartTransform().GetLocation(), Sub->GetStartTransform().Rotator());
		break;

	case ESubDebugCommand::Win:
		if (Objective != nullptr)
		{
			Objective->DebugMarkReached();
		}
		Sub->DebugTeleport(Sub->GetStartTransform().GetLocation(), Sub->GetStartTransform().Rotator());
		break;

	case ESubDebugCommand::Flood:
		Sub->DebugFlood();
		break;

	case ESubDebugCommand::Leak:
		Sub->DebugSpawnLeak();
		break;
	}
#endif
}

void ATheSubProjectGameMode::EndRound(ERoundState Result)
{
	GetGameState<ATheSubGameState>()->SetRoundState(Result);

	// Round's over: the sub stops and stops listening to the helm
	if (Submarine.IsValid())
	{
		Submarine->SetControlsLocked(true);
	}

	UE_LOG(LogSubmarine, Log, TEXT("Round over: %s"), Result == ERoundState::Won ? TEXT("WON (surfaced with the beacon)") : TEXT("LOST (flooded)"));
}

void ATheSubProjectGameMode::RestartRound()
{
	const ATheSubGameState* RoundState = GetGameState<ATheSubGameState>();
	if (bRestartPending || RoundState == nullptr || !RoundState->IsRoundOver())
	{
		return;
	}

	// Usually requested from inside a player's RPC; that player's pawn gets destroyed by the reset, so do it next tick
	bRestartPending = true;
	GetWorldTimerManager().SetTimerForNextTick(this, &ATheSubProjectGameMode::DoRestartRound);
}

void ATheSubProjectGameMode::DoRestartRound()
{
	bRestartPending = false;
	UE_LOG(LogSubmarine, Log, TEXT("Restarting round"));

	// Calls Reset() on every actor: pawns are destroyed, the sub/beacon/stations put themselves back, leaks remove themselves
	ResetLevel();

	GetGameState<ATheSubGameState>()->SetRoundState(ERoundState::InProgress);

	// ResetLevel doesn't respawn anyone
	NextSpawnSlot = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PlayerController = It->Get())
		{
			RestartPlayer(PlayerController);
		}
	}
}

void ATheSubProjectGameMode::RestartPlayer(AController* NewPlayer)
{
	if (NewPlayer != nullptr && !NewPlayer->IsPendingKillPending())
	{
		if (ASubmarine* Sub = Cast<ASubmarine>(UGameplayStatics::GetActorOfClass(this, ASubmarine::StaticClass())))
		{
			// Spread players around the spawn point so co-op players don't spawn inside each other
			static const FVector SlotOffsets[] =
			{
				FVector(0.f, -80.f, 0.f), FVector(0.f, 80.f, 0.f), FVector(-150.f, -80.f, 0.f), FVector(-150.f, 80.f, 0.f),
			};
			FTransform SpawnTransform = Sub->GetPlayerSpawnTransform();
			SpawnTransform.SetLocation(SpawnTransform.TransformPosition(SlotOffsets[NextSpawnSlot++ % UE_ARRAY_COUNT(SlotOffsets)]));
			SpawnTransform.SetScale3D(FVector::OneVector);

			RestartPlayerAtTransform(NewPlayer, SpawnTransform);
			return;
		}
	}

	Super::RestartPlayer(NewPlayer);
}
