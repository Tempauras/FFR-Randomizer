#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CPP_SoundSubsystem.generated.h"

// The game's sound subsystem, mirrored from the .usmap by FFRSExtract's bp-stubs. Member order
// and types must match the game's: Blueprints pass the parameter struct as a literal.

USTRUCT(BlueprintType)
struct MUCHA_API FAISAC_CONTROL_PARAM
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 0.0f;
};

USTRUCT(BlueprintType)
struct MUCHA_API FPLAY_SOUND_PARAMETER
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float startTime = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool isPersistAcrossLevelTransition = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FAISAC_CONTROL_PARAM> aisacControlList;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString selectorName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString selectorLabel;
};

UCLASS(Blueprintable, BlueprintType)
class MUCHA_API UCPP_SoundSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsPlayableSound = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsLogVisible = false;

	// Parameter names are not in the game's bytecode, so they are numbered.
	UFUNCTION(BlueprintCallable)
	int32 PlaySound2D(FGameplayTag P1, FPLAY_SOUND_PARAMETER P2, UObject* P3) { return 0; }
};
