// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SubStation.h"
#include "EngineStation.generated.h"

class UInputAction;
class UWidgetComponent;


UCLASS()
class THESUBPROJECT_API AEngineStation : public ASubStation
{
	GENERATED_BODY()

public:
	AEngineStation();

	virtual void BeginPlay() override;

protected:
	

private:

};
