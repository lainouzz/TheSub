// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interactable.h"
#include "Leak.generated.h"

class UParticleSystemComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * A hole in the hull letting water in. Spawned by the server on a hard impact and attached to the
 * inside of the submarine; the sub adds up FlowRate of all its leaks to raise the water level.
 * Players hold interact (E) on it to patch it; several players patch faster. Progress is kept if
 * they let go, and the leak is destroyed once fully patched.
 */
UCLASS()
class THESUBPROJECT_API ALeak : public AActor, public IInteractable
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Leak, meta=(AllowPrivateAccess = "true"))
	USceneComponent* LeakRoot;

	/** Greybox marker; blocks Visibility so players can aim at it to patch it */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Leak, meta=(AllowPrivateAccess = "true"))
	UStaticMeshComponent* Marker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Leak, meta=(AllowPrivateAccess = "true"))
	UParticleSystemComponent* Spray;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category=Leak, meta=(AllowPrivateAccess = "true"))
	UTextRenderComponent* Label;

public:
	ALeak();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Server: new round, the hull is whole again */
	virtual void Reset() override { Destroy(); }

	// IInteractable: hold to patch
	virtual FString GetInteractionPrompt(const ATheSubProjectCharacter* Character) const override;
	virtual void BeginInteract(ATheSubProjectCharacter* Character) override;
	virtual void EndInteract(ATheSubProjectCharacter* Character) override;
	virtual bool IsHoldInteraction() const override { return true; }

	/** How fast this leak raises the water, in cm per second */
	float GetFlowRate() const { return FlowRate; }

	/** 0 = fresh hole, 1 = patched */
	float GetPatchProgress() const { return PatchProgress; }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Leak", meta=(ClampMin="0"))
	float FlowRate = 2.f;

	/** Seconds for one player to patch it from scratch */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Leak", meta=(ClampMin="0.1"))
	float PatchTime = 4.f;

	/** Patchers further away than this (cm) stop patching */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Leak", meta=(ClampMin="0"))
	float PatchDistance = 350.f;

private:
	UFUNCTION()
	void OnRep_PatchProgress();

	void UpdateLabel();

	UPROPERTY(ReplicatedUsing=OnRep_PatchProgress)
	float PatchProgress = 0.f;

	/** Server: everyone currently holding interact on this leak */
	TArray<TWeakObjectPtr<ATheSubProjectCharacter>> Patchers;
};
