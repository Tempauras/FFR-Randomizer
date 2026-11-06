#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "Input/UIActionBindingHandle.h"
#include "Types/SlateEnums.h"
#include "UObject/Interface.h"
#include "CPP_EU_WBP_Activatable.generated.h"

// The game's screen base class and the input interface it implements. The properties are
// mirrored from the .usmap by FFRSExtract's bp-stubs and must keep the game's names, types and
// order: the mod is cooked unversioned, so a Blueprint's saved defaults are matched to properties
// by their place in the class chain. Function signatures come from the UE4SS header dump; only
// the ones the randomizer's Blueprints use are here.

UENUM(BlueprintType)
enum class EWidgetInputEventType : uint8
{
	Triggered,
	Pressed,
	Released,
	Max,
	None,
};

USTRUCT(BlueprintType)
struct ENHANCEDUI_API FEU_OpenCloseProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsEnableInProcessingStateChange = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsEnableSameStateCall = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsEnableDefaultFinisheCall = false;
};

// The game's menu input, which its input mapping sends to the screen on top: the d-pad, stick or
// arrow keys as InputDirection, confirm as InputDecide, back as InputCancel.
UINTERFACE(Blueprintable, MinimalAPI)
class UCPP_BI_WidgetBasicInput : public UInterface
{
	GENERATED_BODY()
};

class ENHANCEDUI_API ICPP_BI_WidgetBasicInput
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void InputEscape(const EWidgetInputEventType InEventType);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void InputDirection(const EUINavigation InNavigation, const EWidgetInputEventType InEventType);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void InputDecide(const EWidgetInputEventType InEventType);

	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void InputCancel(const EWidgetInputEventType InEventType);
};

UCLASS(Blueprintable, EditInlineNew)
class ENHANCEDUI_API UCPP_EU_WBP_Activatable : public UCommonActivatableWidget, public ICPP_BI_WidgetBasicInput
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsOverrideInputMode = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FUIInputConfig InputConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FEU_OpenCloseProperty OpenStateProperty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FEU_OpenCloseProperty CloseStateProperty;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsInputTarget = false;

	UPROPERTY()
	TWeakObjectPtr<UWidget> InputTargetWidget;

	UPROPERTY()
	TArray<TWeakObjectPtr<UWidget>> InputTargetWidgetStack;

	UPROPERTY()
	TArray<TWeakObjectPtr<UWidget>> InputMonitorWidgetList;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIsCanceling = false;

	// The game's three open and close delegates: placeholders that keep the order.
	UPROPERTY()
	uint8 CloseBeginDel = 0;

	UPROPERTY()
	uint8 OpenFinishedDel = 0;

	UPROPERTY()
	uint8 CloseFinishedDel = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bShouldHandleEscapeAsCancel = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsCloseCancelInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bCanReceiveInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsDefaultPopInputTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ClosableInputTargetStackNum = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bRecursivelyCalculateInputTarget = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsCanInput = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsOpeningProcess = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool IsClosingProcess = false;

	// The game's input target delegate: a placeholder that keeps the order.
	UPROPERTY()
	uint8 RequestToRemoveInputTargetDelegate = 0;

	// Shipped screens override it, call the parent first, then FinishedOpen once they are shown.
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
	void OnRequestOpen();
	virtual void OnRequestOpen_Implementation() {}

	UFUNCTION(BlueprintCallable)
	void FinishedOpen() {}

	virtual void InputEscape_Implementation(const EWidgetInputEventType InEventType) override {}
	virtual void InputDirection_Implementation(const EUINavigation InNavigation, const EWidgetInputEventType InEventType) override {}
	virtual void InputDecide_Implementation(const EWidgetInputEventType InEventType) override {}
	virtual void InputCancel_Implementation(const EWidgetInputEventType InEventType) override {}
};
