#pragma once

#include "CoreMinimal.h"
#include "CPP_EU_WBP_Activatable.h"
#include "GameplayTagContainer.h"
#include "CPP_WBP_MuchaActivatable.generated.h"

// The class the game's screens derive from. Its ActionGuideTag picks the prompts the game shows
// at the bottom right while the screen is on top. The properties keep the game's names, types and
// order (see CPP_EU_WBP_Activatable.h).

UCLASS(Blueprintable, EditInlineNew)
class MUCHA_API UCPP_WBP_MuchaActivatable : public UCPP_EU_WBP_Activatable
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag ActionGuideTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bClearInputTargetAtClose = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag OpenSoundTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag CloseSoundTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag CancelSoundTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TWeakObjectPtr<UCommonActivatableWidget> ParentWidget;
};
