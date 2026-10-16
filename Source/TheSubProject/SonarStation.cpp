// Copyright Epic Games, Inc. All Rights Reserved.

#include "SonarStation.h"
#include "Beacon.h"
#include "SonarScreenWidget.h"
#include "Submarine.h"
#include "TheSubProjectCharacter.h"
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Net/UnrealNetwork.h"

ASonarStation::ASonarStation()
{
	StationName = NSLOCTEXT("SonarStation", "Name", "Sonar");

	// Screen stands on the far edge of the console, facing the operator (who stands at -X)
	Screen = CreateDefaultSubobject<UWidgetComponent>(TEXT("Screen"));
	Screen->SetupAttachment(RootComponent);
	Screen->SetWidgetSpace(EWidgetSpace::World);
	Screen->SetWidgetClass(USonarScreenWidget::StaticClass());
	// Radar square on the left, depth panel on the right
	Screen->SetDrawSize(FVector2D(768.f, 512.f));
	Screen->SetRelativeLocation(FVector(25.f, 0.f, 170.f));
	// Widgets face +X like the name label; turn it around so the operator sees the front
	Screen->SetRelativeRotation(FRotator(0.f, 180.f, 0.f));
	Screen->SetRelativeScale3D(FVector(0.18f));
	Screen->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Make room for the screen
	GetNameLabel()->SetRelativeLocation(FVector(0.f, 0.f, 240.f));
}

void ASonarStation::BeginPlay()
{
	Super::BeginPlay();

	if (USonarScreenWidget* ScreenWidget = Cast<USonarScreenWidget>(Screen->GetUserWidgetObject()))
	{
		ScreenWidget->SetSonarStation(this);
	}
}

void ASonarStation::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASonarStation, LastPing);
}

void ASonarStation::Reset()
{
	Super::Reset();

	LastPing = FSonarPing();
}

double ASonarStation::GetServerTime() const
{
	if (const ASubmarine* Submarine = GetSubmarine())
	{
		return Submarine->GetServerTime();
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	return GameState != nullptr ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}

float ASonarStation::GetTimeSinceLastPing() const
{
	return LastPing.ServerTime < 0.0 ? UE_BIG_NUMBER : static_cast<float>(GetServerTime() - LastPing.ServerTime);
}

float ASonarStation::GetCooldownRemaining() const
{
	return FMath::Max(0.f, Cooldown - GetTimeSinceLastPing());
}

FString ASonarStation::GetStatusText() const
{
	const float Remaining = GetCooldownRemaining();
	const FString PingState = Remaining > 0.f
		? FString::Printf(TEXT("recharging %.1fs"), Remaining)
		: FString(TEXT("ready"));

	const ASubmarine* Submarine = GetSubmarine();
	auto Meters = [](float Centimeters) { return Centimeters >= 0.f ? FString::Printf(TEXT("%.0f m"), Centimeters / 100.f) : FString(TEXT("--")); };

	return FString::Printf(
		TEXT("SONAR\n")
		TEXT("Ping: %s   [Space]\n")
		TEXT("Depth: %s   Ground: %s   Above: %s\n")
		TEXT("Contacts: %d   Targets: %d\n")
		TEXT("[E] Leave sonar"),
		*PingState,
		Submarine != nullptr ? *Meters(Submarine->GetDepth()) : TEXT("--"),
		*Meters(LastPing.BelowDistance),
		*Meters(LastPing.AboveDistance),
		LastPing.Contacts.Num(),
		LastPing.Targets.Num());
}

void ASonarStation::BuildDefaultInput()
{
	if (StationMappingContext != nullptr)
	{
		return;
	}

	if (PingAction == nullptr)
	{
		PingAction = NewObject<UInputAction>(this, TEXT("IA_Sonar_Ping"));
		PingAction->ValueType = EInputActionValueType::Boolean;
	}

	StationMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Sonar"));
	StationMappingContext->MapKey(PingAction, EKeys::SpaceBar);
	StationMappingContext->MapKey(PingAction, EKeys::LeftMouseButton);
}

void ASonarStation::BindStationInput(UEnhancedInputComponent& EnhancedInputComponent)
{
	StationBindingHandles.Add(EnhancedInputComponent.BindAction(PingAction, ETriggerEvent::Started, this, &ASonarStation::Ping).GetHandle());
}

void ASonarStation::Ping()
{
	// Cheap local check so we don't spam the server; the server checks again
	if (GetCooldownRemaining() <= 0.f)
	{
		ServerPing();
	}
}

void ASonarStation::ServerPing_Implementation()
{
	ASubmarine* Submarine = GetSubmarine();
	if (GetOccupant() == nullptr || Submarine == nullptr || GetCooldownRemaining() > 0.f)
	{
		return;
	}

	// Rays start inside the hull, so ignore the sub, everything attached to it, and the players
	FCollisionQueryParams Params(SCENE_QUERY_STAT(SonarPing), false, Submarine);
	TArray<AActor*> AttachedActors;
	Submarine->GetAttachedActors(AttachedActors, true, true);
	Params.AddIgnoredActors(AttachedActors);
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		Params.AddIgnoredActor(*It);
	}

	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);

	FSonarPing NewPing;
	NewPing.ServerTime = GetServerTime();

	const FVector Origin = Submarine->GetActorLocation();
	auto Trace = [&](const FVector& Direction, FHitResult& OutHit)
	{
		return GetWorld()->LineTraceSingleByObjectType(OutHit, Origin, Origin + Direction * Range, ObjectParams, Params);
	};

	// A ring of rays per pitch: level shows walls around us, tilted down shows the ground ahead
	for (const float Pitch : RayPitches)
	{
		for (int32 Ray = 0; Ray < NumRays; ++Ray)
		{
			FHitResult Hit;
			if (Trace(FRotator(Pitch, 360.f * Ray / NumRays, 0.f).Vector(), Hit))
			{
				NewPing.Contacts.Add(Hit.ImpactPoint);
			}
		}
	}

	// Straight down and up, measured from the hull's bottom/top
	const float HullHalfHeight = Submarine->GetHullHalfHeight();
	FHitResult VerticalHit;
	if (Trace(FVector::DownVector, VerticalHit))
	{
		NewPing.BelowDistance = FMath::Max(0.f, VerticalHit.Distance - HullHalfHeight);
	}
	if (Trace(FVector::UpVector, VerticalHit))
	{
		NewPing.AboveDistance = FMath::Max(0.f, VerticalHit.Distance - HullHalfHeight);
	}

	// Beacons and test targets show up whenever they're in range on the screen (distance across the water, ignoring depth)
	auto AddTarget = [&NewPing, &Origin, this](const AActor* Target)
	{
		if (Target != nullptr && FVector::Dist2D(Target->GetActorLocation(), Origin) <= Range)
		{
			NewPing.Targets.Add(Target->GetActorLocation());
		}
	};
	for (TActorIterator<ABeacon> It(GetWorld()); It; ++It)
	{
		AddTarget(*It);
	}
	for (const AActor* Target : Submarine->GetSonarTestTargets())
	{
		AddTarget(Target);
	}

	LastPing = MoveTemp(NewPing);

	UE_LOG(LogSubmarine, Log, TEXT("Sonar (%s): ping, %d contacts, %d targets, below %.0f, above %.0f"),
		*GetNameSafe(GetOccupant()), LastPing.Contacts.Num(), LastPing.Targets.Num(), LastPing.BelowDistance, LastPing.AboveDistance);
}
