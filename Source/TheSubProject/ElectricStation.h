// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SubStation.h"
#include "ElectricStation.generated.h"

class UInputAction;
class UWidgetComponent;


UCLASS()
class THESUBPROJECT_API AElectricStation : public ASubStation
{
	GENERATED_BODY()

public:
	AElectricStation();

	virtual void BeginPlay() override;

protected:
	

private:

};
