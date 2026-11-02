#include "BuildFFRRandomizerUICommandlet.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputKey.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetTextLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "WidgetBlueprint.h"

DEFINE_LOG_CATEGORY_STATIC(LogFFRRandomizerUI, Log, All);

namespace
{
	const TCHAR* const ModFolder = TEXT("/Game/Mods/FFRRandomizer/");
	constexpr float FontSize = 14.f;

	// Stops at the first problem and names it, so a failed run says which connection broke.
	struct FBuildError
	{
		FString Message;
	};

	class FGraphBuilder
	{
	public:
		explicit FGraphBuilder(UEdGraph* InGraph)
			: Graph(InGraph)
		{
		}

		UK2Node_CallFunction* Call(UClass* Owner, FName Function)
		{
			UFunction* Found = Owner->FindFunctionByName(Function);
			if (!Found)
			{
				throw FBuildError{FString::Printf(TEXT("%s has no function %s"), *Owner->GetName(), *Function.ToString())};
			}
			FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
			UK2Node_CallFunction* Node = Creator.CreateNode();
			Node->SetFromFunction(Found);
			Creator.Finalize();
			return Node;
		}

		UK2Node_VariableGet* Get(FName Variable)
		{
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
			UK2Node_VariableGet* Node = Creator.CreateNode();
			Node->VariableReference.SetSelfMember(Variable);
			Creator.Finalize();
			return Node;
		}

		UK2Node_VariableSet* Set(FName Variable)
		{
			FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
			UK2Node_VariableSet* Node = Creator.CreateNode();
			Node->VariableReference.SetSelfMember(Variable);
			Creator.Finalize();
			return Node;
		}

		// Sets a property of another class's object, whose Target pin is the returned node's self.
		UK2Node_VariableSet* SetOn(UClass* Owner, FName Variable)
		{
			FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
			UK2Node_VariableSet* Node = Creator.CreateNode();
			Node->VariableReference.SetExternalMember(Variable, Owner);
			Creator.Finalize();
			return Node;
		}

		UK2Node_IfThenElse* Branch()
		{
			FGraphNodeCreator<UK2Node_IfThenElse> Creator(*Graph);
			UK2Node_IfThenElse* Node = Creator.CreateNode();
			Creator.Finalize();
			return Node;
		}

		static UEdGraphPin* Pin(UEdGraphNode* Node, FName Name)
		{
			UEdGraphPin* Found = Node->FindPin(Name);
			if (!Found)
			{
				throw FBuildError{FString::Printf(TEXT("%s has no pin %s"), *Node->GetName(), *Name.ToString())};
			}
			return Found;
		}

		void Link(UEdGraphNode* From, FName FromPin, UEdGraphNode* To, FName ToPin)
		{
			if (!Graph->GetSchema()->TryCreateConnection(Pin(From, FromPin), Pin(To, ToPin)))
			{
				throw FBuildError{FString::Printf(TEXT("cannot connect %s.%s to %s.%s"),
					*From->GetName(), *FromPin.ToString(), *To->GetName(), *ToPin.ToString())};
			}
		}

		// Execution: From's output into To's input.
		void Then(UEdGraphNode* From, FName FromPin, UEdGraphNode* To)
		{
			Link(From, FromPin, To, UEdGraphSchema_K2::PN_Execute);
		}

		void Then(UEdGraphNode* From, UEdGraphNode* To)
		{
			Then(From, UEdGraphSchema_K2::PN_Then, To);
		}

		// A widget or other object variable as the target of a call.
		void Target(FName Variable, UEdGraphNode* Node)
		{
			Link(Get(Variable), Variable, Node, UEdGraphSchema_K2::PN_Self);
		}

		void Literal(UEdGraphNode* Node, FName PinName, const FString& Value)
		{
			Pin(Node, PinName)->DefaultValue = Value;
		}

	private:
		UEdGraph* Graph;
	};

	UK2Node_Event* EnabledBeginPlay(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		TArray<UK2Node_Event*> Events;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, Events);
		for (UK2Node_Event* Event : Events)
		{
			if (Event->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
			{
				Event->SetEnabledState(ENodeEnabledState::Enabled, false);
				return Event;
			}
		}

		FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
		UK2Node_Event* Event = Creator.CreateNode();
		Event->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
		Event->bOverrideFunction = true;
		Creator.Finalize();
		return Event;
	}

	UPackage* NewAssetPackage(const FString& AssetName)
	{
		const FString Path = FString(ModFolder) + AssetName;
		if (FPackageName::DoesPackageExist(Path))
		{
			throw FBuildError{Path + TEXT(" already exists; delete the FFRRandomizer folder to generate it again")};
		}
		return CreatePackage(*Path);
	}

	void CompileAndSave(UBlueprint* Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			throw FBuildError{Blueprint->GetName() + TEXT(" did not compile")};
		}

		UPackage* Package = Blueprint->GetOutermost();
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Package, Blueprint, *File, Args))
		{
			throw FBuildError{TEXT("could not save ") + File};
		}
		UE_LOG(LogFFRRandomizerUI, Display, TEXT("Saved %s"), *File);
	}

	UTextBlock* AddLabel(UWidgetTree* Tree, UVerticalBox* Box, FName Name, const FString& Text)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(FText::FromString(Text));
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = FontSize;
		Label->SetFont(Font);
		Box->AddChildToVerticalBox(Label)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
		return Label;
	}

	UWidgetBlueprint* BuildPanel()
	{
		UWidgetBlueprint* Blueprint = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), NewAssetPackage(TEXT("WBP_SeedPanel")), TEXT("WBP_SeedPanel"), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));

		UWidgetTree* Tree = Blueprint->WidgetTree;
		UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		Tree->RootWidget = Root;

		UBorder* Frame = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Frame"));
		Frame->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.03f, 0.9f));
		Frame->SetPadding(FMargin(14.f));
		UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame);
		// Top centre: the game's quest text and mini-map hold the top corners.
		FrameSlot->SetAnchors(FAnchors(0.5f, 0.f));
		FrameSlot->SetAlignment(FVector2D(0.5f, 0.f));
		FrameSlot->SetPosition(FVector2D(0.f, 24.f));
		FrameSlot->SetAutoSize(true);

		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box"));
		Frame->SetContent(Box);

		AddLabel(Tree, Box, TEXT("Title"), TEXT("FFR Randomizer seed  (F9 to close)"));
		UTextBlock* SeedLabel = AddLabel(Tree, Box, TEXT("SeedLabel"), TEXT("Seed: waiting for the mod's DLL"));
		SeedLabel->bIsVariable = true;

		UEditableTextBox* SeedInput = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SeedInput"));
		SeedInput->bIsVariable = true;
		SeedInput->SetHintText(FText::FromString(TEXT("Ctrl+C to copy, Ctrl+V to paste")));
		SeedInput->WidgetStyle.TextStyle.Font.Size = FontSize;
		SeedInput->WidgetStyle.TextStyle.ColorAndOpacity = FSlateColor(FLinearColor::Black);
		SeedInput->WidgetStyle.SetForegroundColor(FSlateColor(FLinearColor::Black));
		SeedInput->SetSelectAllTextWhenFocused(true);
		Box->AddChildToVerticalBox(SeedInput)->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));

		UButton* ApplyButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ApplyButton"));
		ApplyButton->bIsVariable = true;
		UTextBlock* ApplyText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ApplyText"));
		ApplyText->SetText(FText::FromString(TEXT("Apply seed")));
		ApplyText->SetColorAndOpacity(FSlateColor(FLinearColor::Black));
		FSlateFontInfo ApplyFont = ApplyText->GetFont();
		ApplyFont.Size = FontSize;
		ApplyText->SetFont(ApplyFont);
		ApplyButton->SetContent(ApplyText);
		Box->AddChildToVerticalBox(ApplyButton)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));

		UTextBlock* GroupsLabel = AddLabel(Tree, Box, TEXT("GroupsLabel"), TEXT(""));
		GroupsLabel->bIsVariable = true;

		const FName Seed(TEXT("SeedLabel")), Input(TEXT("SeedInput")), Button(TEXT("ApplyButton")), Groups(TEXT("GroupsLabel"));
		Tree->ForEachWidget([Blueprint](UWidget* Widget)
		{
			Blueprint->OnVariableAdded(Widget->GetFName());
		});

		const FName RequestedSeed(TEXT("RequestedSeed"));
		FEdGraphPinType StringType;
		StringType.PinCategory = UEdGraphSchema_K2::PC_String;
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, RequestedSeed, StringType))
		{
			throw FBuildError{TEXT("could not add RequestedSeed")};
		}

		// The widget variables and RequestedSeed exist on the skeleton class only after this.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (!Graph)
		{
			throw FBuildError{TEXT("WBP_SeedPanel has no event graph")};
		}
		FGraphBuilder Build(Graph);

		// Apply button: RequestedSeed = SeedInput's text. The DLL reads and clears it.
		{
			FObjectProperty* ButtonProperty = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, Button);
			FMulticastDelegateProperty* Clicked = FindFProperty<FMulticastDelegateProperty>(UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnClicked));
			if (!ButtonProperty || !Clicked)
			{
				throw FBuildError{TEXT("ApplyButton or OnClicked was not found")};
			}
			FGraphNodeCreator<UK2Node_ComponentBoundEvent> Creator(*Graph);
			UK2Node_ComponentBoundEvent* OnClicked = Creator.CreateNode();
			OnClicked->InitializeComponentBoundEventParams(ButtonProperty, Clicked);
			Creator.Finalize();

			UK2Node_CallFunction* GetText = Build.Call(UEditableTextBox::StaticClass(), TEXT("GetText"));
			Build.Target(Input, GetText);
			UK2Node_CallFunction* ToString = Build.Call(UKismetTextLibrary::StaticClass(), TEXT("Conv_TextToString"));
			Build.Link(GetText, UEdGraphSchema_K2::PN_ReturnValue, ToString, TEXT("InText"));
			UK2Node_VariableSet* SetRequest = Build.Set(RequestedSeed);
			Build.Link(ToString, UEdGraphSchema_K2::PN_ReturnValue, SetRequest, RequestedSeed);
			Build.Then(OnClicked, SetRequest);
		}

		// UpdateDisplay(SeedText, GroupsText): called by the DLL with what to show.
		{
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* Update = Creator.CreateNode();
			Update->CustomFunctionName = TEXT("UpdateDisplay");
			Creator.Finalize();
			Update->CreateUserDefinedPin(TEXT("SeedText"), StringType, EGPD_Output);
			Update->CreateUserDefinedPin(TEXT("GroupsText"), StringType, EGPD_Output);

			const auto SetTextFrom = [&Build](FName Widget, UClass* WidgetClass, UEdGraphNode* Source, FName SourcePin)
			{
				UK2Node_CallFunction* Convert = Build.Call(UKismetTextLibrary::StaticClass(), TEXT("Conv_StringToText"));
				Build.Link(Source, SourcePin, Convert, TEXT("InString"));
				UK2Node_CallFunction* SetText = Build.Call(WidgetClass, TEXT("SetText"));
				Build.Target(Widget, SetText);
				Build.Link(Convert, UEdGraphSchema_K2::PN_ReturnValue, SetText, TEXT("InText"));
				return SetText;
			};
			UK2Node_CallFunction* SetSeed = SetTextFrom(Seed, UTextBlock::StaticClass(), Update, TEXT("SeedText"));
			UK2Node_CallFunction* SetInput = SetTextFrom(Input, UEditableTextBox::StaticClass(), Update, TEXT("SeedText"));
			UK2Node_CallFunction* SetGroups = SetTextFrom(Groups, UTextBlock::StaticClass(), Update, TEXT("GroupsText"));
			Build.Then(Update, SetSeed);
			Build.Then(SetSeed, SetInput);
			Build.Then(SetInput, SetGroups);
		}

		CompileAndSave(Blueprint);
		return Blueprint;
	}

	void BuildModActor(UWidgetBlueprint* Panel)
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
			AActor::StaticClass(), NewAssetPackage(TEXT("ModActor")), TEXT("ModActor"), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());

		const FName PanelVariable(TEXT("Panel"));
		FEdGraphPinType PanelType;
		PanelType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PanelType.PinSubCategoryObject = UUserWidget::StaticClass();
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, PanelVariable, PanelType))
		{
			throw FBuildError{TEXT("could not add Panel")};
		}
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (!Graph)
		{
			throw FBuildError{TEXT("ModActor has no event graph")};
		}
		FGraphBuilder Build(Graph);

		const auto PlayerController = [&Build]()
		{
			UK2Node_CallFunction* Node = Build.Call(UGameplayStatics::StaticClass(), TEXT("GetPlayerController"));
			Build.Literal(Node, TEXT("PlayerIndex"), TEXT("0"));
			return Node;
		};
		const FName Return = UEdGraphSchema_K2::PN_ReturnValue;

		// BeginPlay: create the panel, hidden, and start listening for F9.
		{
			UK2Node_Event* BeginPlay = EnabledBeginPlay(Blueprint, Graph);
			UK2Node_CallFunction* Create = Build.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("Create"));
			UEdGraphPin* WidgetType = FGraphBuilder::Pin(Create, TEXT("WidgetType"));
			WidgetType->DefaultObject = Panel->GeneratedClass;
			Build.Link(PlayerController(), Return, Create, TEXT("OwningPlayer"));

			UK2Node_VariableSet* SetPanel = Build.Set(PanelVariable);
			Build.Link(Create, Return, SetPanel, PanelVariable);

			UK2Node_CallFunction* AddToViewport = Build.Call(UUserWidget::StaticClass(), TEXT("AddToViewport"));
			Build.Target(PanelVariable, AddToViewport);
			Build.Literal(AddToViewport, TEXT("ZOrder"), TEXT("100"));

			UK2Node_CallFunction* Hide = Build.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
			Build.Target(PanelVariable, Hide);
			Build.Literal(Hide, TEXT("InVisibility"), TEXT("Collapsed"));

			UK2Node_CallFunction* EnableInput = Build.Call(AActor::StaticClass(), TEXT("EnableInput"));
			Build.Link(PlayerController(), Return, EnableInput, TEXT("PlayerController"));

			Build.Then(BeginPlay, Create);
			Build.Then(Create, SetPanel);
			Build.Then(SetPanel, AddToViewport);
			Build.Then(AddToViewport, Hide);
			Build.Then(Hide, EnableInput);
		}

		// F9: show the panel and free the mouse, or hide it and hand control back.
		{
			FGraphNodeCreator<UK2Node_InputKey> Creator(*Graph);
			UK2Node_InputKey* Key = Creator.CreateNode();
			Key->InputKey = EKeys::F9;
			Key->bConsumeInput = false;
			Creator.Finalize();

			UK2Node_CallFunction* IsVisible = Build.Call(UWidget::StaticClass(), TEXT("IsVisible"));
			Build.Target(PanelVariable, IsVisible);
			UK2Node_IfThenElse* Branch = Build.Branch();
			Build.Link(IsVisible, Return, Branch, UEdGraphSchema_K2::PN_Condition);
			Build.Then(Key, TEXT("Pressed"), Branch);

			UK2Node_CallFunction* Hide = Build.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
			Build.Target(PanelVariable, Hide);
			Build.Literal(Hide, TEXT("InVisibility"), TEXT("Collapsed"));
			UK2Node_CallFunction* GameOnly = Build.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("SetInputMode_GameOnly"));
			Build.Link(PlayerController(), Return, GameOnly, TEXT("PlayerController"));
			UK2Node_VariableSet* HideCursor = Build.SetOn(APlayerController::StaticClass(), TEXT("bShowMouseCursor"));
			Build.Link(PlayerController(), Return, HideCursor, UEdGraphSchema_K2::PN_Self);
			Build.Literal(HideCursor, TEXT("bShowMouseCursor"), TEXT("false"));
			Build.Then(Branch, UEdGraphSchema_K2::PN_Then, Hide);
			Build.Then(Hide, GameOnly);
			Build.Then(GameOnly, HideCursor);

			UK2Node_CallFunction* Show = Build.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
			Build.Target(PanelVariable, Show);
			Build.Literal(Show, TEXT("InVisibility"), TEXT("Visible"));
			UK2Node_CallFunction* GameAndUI = Build.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("SetInputMode_GameAndUIEx"));
			Build.Link(PlayerController(), Return, GameAndUI, TEXT("PlayerController"));
			UK2Node_VariableSet* ShowCursor = Build.SetOn(APlayerController::StaticClass(), TEXT("bShowMouseCursor"));
			Build.Link(PlayerController(), Return, ShowCursor, UEdGraphSchema_K2::PN_Self);
			Build.Literal(ShowCursor, TEXT("bShowMouseCursor"), TEXT("true"));
			Build.Then(Branch, UEdGraphSchema_K2::PN_Else, Show);
			Build.Then(Show, GameAndUI);
			Build.Then(GameAndUI, ShowCursor);
		}

		CompileAndSave(Blueprint);
	}
}

int32 UBuildFFRRandomizerUICommandlet::Main(const FString& Params)
{
	try
	{
		BuildModActor(BuildPanel());
	}
	catch (const FBuildError& Error)
	{
		UE_LOG(LogFFRRandomizerUI, Error, TEXT("%s"), *Error.Message);
		return 1;
	}
	return 0;
}
