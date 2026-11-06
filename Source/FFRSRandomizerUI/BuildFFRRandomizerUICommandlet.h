#pragma once

#include "Commandlets/Commandlet.h"
#include "BuildFFRRandomizerUICommandlet.generated.h"

// Writes /Game/Mods/FFRRandomizer/WBP_SeedPanel. Editor only: none of this ships.
//   UnrealEditor-Cmd.exe FFRS.uproject -run=BuildFFRRandomizerUI
UCLASS()
class UBuildFFRRandomizerUICommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
