#include "BuildFFRRandomizerUICommandlet.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "CommonTextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/NamedSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "CPP_SoundSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetInputLibrary.h"
#include "Kismet/KismetMathLibrary.h"
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

	// The game's UI assets. The project holds unpacked stand-ins at these paths; in game the
	// references resolve to the game's own.
	const TCHAR* const CursorTexture = TEXT("/Game/UI/Textures/Menu/MenuIcon_FingerCursor_R.MenuIcon_FingerCursor_R");
	const TCHAR* const ButtonTexture = TEXT("/Game/UI/Textures/Menu/T_Dialog_Default.T_Dialog_Default");
	const TCHAR* const ButtonSelectTexture = TEXT("/Game/UI/Textures/Menu/T_Dialog_Select.T_Dialog_Select");
	const TCHAR* const SystemFont = TEXT("/Game/UI/Font/SystemText.SystemText");
	const TCHAR* const SystemTextStyle = TEXT("/Game/UI/Common/Text/TextStyle/SystemText/BP_SystemTextStyle.BP_SystemTextStyle_C");

	// Game widgets the panel creates at run time: saved as instances, they would load with the
	// stand-in's property layout rather than the game's.
	const TCHAR* const TitleWindowPackage = TEXT("/Game/UI/Common/Layout/Panel/WBP_TitlePanelLayout");
	const TCHAR* const RowLabelPackage = TEXT("/Game/UI/Common/Button/LeftLine/Widget/WBP_LeftLineButton");

	// The space the window's frame and title leave for content.
	const FVector2D WindowSize(1800.f, 760.f);
	const FMargin WindowInside(211.f, 125.f, 211.f, 110.f);

	const FLinearColor ValueFill(0.f, 0.f, 0.f, 0.7f);
	const FLinearColor HintColor(0.55f, 0.72f, 0.85f, 0.9f);
	constexpr float RowHeight = 50.f;
	constexpr float LabelWidth = 300.f;
	constexpr float SeedColumnWidth = 680.f;
	constexpr float CursorSize = 34.f;

	// The zone lists' entries are made in advance; the DLL names the ones it needs and hides the
	// rest. Kind is what ChooseZone passes on.
	struct FZoneListKind
	{
		const TCHAR* Name;
		int32 Kind;
		int32 Entries;
	};
	const FZoneListKind ZoneLists[] = {
		{TEXT("Dungeon"), 1, 64},
		{TEXT("Field"), 2, 96},
	};

	const TCHAR* const SoundOpen = TEXT("Sound.UI.Common.Open");
	const TCHAR* const SoundClose = TEXT("Sound.UI.Cancel");
	const TCHAR* const SoundCursor = TEXT("Sound.UI.Cursor");
	const TCHAR* const SoundDecide = TEXT("Sound.UI.Decide");

	struct FBuildError
	{
		FString Message;
	};

	template<typename AssetType>
	AssetType* LoadAsset(const TCHAR* Path)
	{
		AssetType* Asset = LoadObject<AssetType>(nullptr, Path);
		if (!Asset)
		{
			throw FBuildError{FString(Path) + TEXT(" is missing: copy Content/UI from the toolkit's bp-template")};
		}
		return Asset;
	}

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

		UK2Node_VariableGet* GetOn(UClass* Owner, FName Variable)
		{
			FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
			UK2Node_VariableGet* Node = Creator.CreateNode();
			Node->VariableReference.SetExternalMember(Variable, Owner);
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

		UK2Node_VariableSet* SetOn(UClass* Owner, FName Variable)
		{
			FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
			UK2Node_VariableSet* Node = Creator.CreateNode();
			Node->VariableReference.SetExternalMember(Variable, Owner);
			Creator.Finalize();
			return Node;
		}

		UK2Node_Self* Self()
		{
			FGraphNodeCreator<UK2Node_Self> Creator(*Graph);
			UK2Node_Self* Node = Creator.CreateNode();
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

		UK2Node_SwitchEnum* Switch(UEnum* Enum)
		{
			FGraphNodeCreator<UK2Node_SwitchEnum> Creator(*Graph);
			UK2Node_SwitchEnum* Node = Creator.CreateNode();
			Node->SetEnum(Enum);
			Creator.Finalize();
			return Node;
		}

		UK2Node_DynamicCast* CastTo(UClass* Class)
		{
			FGraphNodeCreator<UK2Node_DynamicCast> Creator(*Graph);
			UK2Node_DynamicCast* Node = Creator.CreateNode();
			Node->TargetType = Class;
			Node->SetPurity(true);
			Creator.Finalize();
			return Node;
		}

		UK2Node_CallFunction* PlayerController()
		{
			UK2Node_CallFunction* Node = Call(UGameplayStatics::StaticClass(), TEXT("GetPlayerController"));
			Literal(Node, TEXT("PlayerIndex"), TEXT("0"));
			return Node;
		}

		// The game's UI sound for Tag, as its menus play it.
		UK2Node_CallFunction* PlaySound(const TCHAR* Tag)
		{
			FGraphNodeCreator<UK2Node_GetSubsystem> Creator(*Graph);
			UK2Node_GetSubsystem* Subsystem = Creator.CreateNode();
			Subsystem->Initialize(UCPP_SoundSubsystem::StaticClass());
			Creator.Finalize();

			UK2Node_CallFunction* Play = Call(UCPP_SoundSubsystem::StaticClass(), GET_FUNCTION_NAME_CHECKED(UCPP_SoundSubsystem, PlaySound2D));
			if (!Graph->GetSchema()->TryCreateConnection(Subsystem->GetResultPin(), Pin(Play, UEdGraphSchema_K2::PN_Self)))
			{
				throw FBuildError{TEXT("cannot connect the sound subsystem to PlaySound2D")};
			}
			Literal(Play, TEXT("P1"), FString::Printf(TEXT("(TagName=\"%s\")"), Tag));
			Link(Self(), UEdGraphSchema_K2::PN_Self, Play, TEXT("P3"));
			return Play;
		}

		template<typename NodeType>
		NodeType* Find() const
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (NodeType* Found = Cast<NodeType>(Node)) return Found;
			}
			throw FBuildError{FString::Printf(TEXT("%s has no %s"), *Graph->GetName(), *NodeType::StaticClass()->GetName())};
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

		void Then(UEdGraphNode* From, FName FromPin, UEdGraphNode* To)
		{
			Link(From, FromPin, To, UEdGraphSchema_K2::PN_Execute);
		}

		void Then(UEdGraphNode* From, UEdGraphNode* To)
		{
			Then(From, UEdGraphSchema_K2::PN_Then, To);
		}

		void Target(FName Variable, UEdGraphNode* Node)
		{
			Link(Get(Variable), Variable, Node, UEdGraphSchema_K2::PN_Self);
		}

		void TargetPlayer(UEdGraphNode* Node, FName PinName = UEdGraphSchema_K2::PN_Self)
		{
			Link(PlayerController(), UEdGraphSchema_K2::PN_ReturnValue, Node, PinName);
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

	UK2Node_Event* OverrideEvent(UEdGraph* Graph, UClass* Owner, FName Function)
	{
		FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
		UK2Node_Event* Event = Creator.CreateNode();
		Event->EventReference.SetExternalMember(Function, Owner);
		Event->bOverrideFunction = true;
		Creator.Finalize();
		return Event;
	}

	UK2Node_ComponentBoundEvent* BoundEvent(UBlueprint* Blueprint, UEdGraph* Graph, FName Widget, UClass* WidgetClass, FName Delegate)
	{
		FObjectProperty* WidgetProperty = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, Widget);
		FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(WidgetClass, Delegate);
		if (!WidgetProperty || !DelegateProperty)
		{
			throw FBuildError{FString::Printf(TEXT("%s or its %s was not found"), *Widget.ToString(), *Delegate.ToString())};
		}
		FGraphNodeCreator<UK2Node_ComponentBoundEvent> Creator(*Graph);
		UK2Node_ComponentBoundEvent* Event = Creator.CreateNode();
		Event->InitializeComponentBoundEventParams(WidgetProperty, DelegateProperty);
		Creator.Finalize();
		return Event;
	}

	UEdGraph* OverrideFunction(UBlueprint* Blueprint, UClass* Owner, FName Function)
	{
		if (!Owner->FindFunctionByName(Function))
		{
			throw FBuildError{FString::Printf(TEXT("%s has no function %s to override"), *Owner->GetName(), *Function.ToString())};
		}
		UEdGraph* Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, Function, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddFunctionGraph<UClass>(Blueprint, Graph, false, Owner);
		return Graph;
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

	// An editor stand-in for a game widget Blueprint at its shipped path, made once and never packed.
	UClass* StandInWidget(const TCHAR* PackagePath, TFunctionRef<void(UWidgetBlueprint*)> AddMembers)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (FPackageName::DoesPackageExist(PackagePath))
		{
			UWidgetBlueprint* Existing = LoadObject<UWidgetBlueprint>(nullptr, *(FString(PackagePath) + TEXT(".") + AssetName));
			if (!Existing || !Existing->GeneratedClass)
			{
				throw FBuildError{FString(PackagePath) + TEXT(" exists but is not a widget Blueprint")};
			}
			return Existing->GeneratedClass;
		}

		UWidgetBlueprint* Blueprint = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), CreatePackage(PackagePath), *AssetName, BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));
		Blueprint->WidgetTree->RootWidget = Blueprint->WidgetTree->ConstructWidget<UNamedSlot>(UNamedSlot::StaticClass(), TEXT("Content"));
		AddMembers(Blueprint);
		CompileAndSave(Blueprint);
		return Blueprint->GeneratedClass;
	}

	// The size is set by hand: in a commandlet an uncompiled texture reports 0 x 0.
	FSlateBrush TextureBrush(UTexture2D* Texture, FVector2D Size, ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::Image, float Margin = 0.f)
	{
		FSlateBrush Brush;
		Brush.SetResourceObject(Texture);
		Brush.ImageSize = Size;
		Brush.DrawAs = DrawAs;
		Brush.Margin = FMargin(Margin);
		return Brush;
	}

	FEdGraphPinType IntPinType()
	{
		FEdGraphPinType Type;
		Type.PinCategory = UEdGraphSchema_K2::PC_Int;
		return Type;
	}

	UCommonTextBlock* GameText(UWidgetTree* Tree, UClass* Style, FName Name, const FString& Text)
	{
		UCommonTextBlock* Block = Tree->ConstructWidget<UCommonTextBlock>(UCommonTextBlock::StaticClass(), Name);
		Block->SetText(FText::FromString(Text));
		Block->SetStyle(Style);
		return Block;
	}

	UWidgetBlueprint* BuildPanel()
	{
		UWidgetBlueprint* Blueprint = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UUserWidget::StaticClass(), NewAssetPackage(TEXT("WBP_SeedPanel")), TEXT("WBP_SeedPanel"), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));

		UFont* Font = LoadAsset<UFont>(SystemFont);
		UClass* Style = LoadAsset<UClass>(SystemTextStyle);
		UTexture2D* Finger = LoadAsset<UTexture2D>(CursorTexture);
		UTexture2D* ButtonImage = LoadAsset<UTexture2D>(ButtonTexture);
		UTexture2D* ButtonSelectImage = LoadAsset<UTexture2D>(ButtonSelectTexture);

		const FName TitleText(TEXT("TitleText"));
		UClass* TitleWindow = StandInWidget(TitleWindowPackage, [TitleText](UWidgetBlueprint* StandIn)
		{
			FEdGraphPinType TextType;
			TextType.PinCategory = UEdGraphSchema_K2::PC_Text;
			FBlueprintEditorUtils::AddMemberVariable(StandIn, TitleText, TextType);
		});
		UClass* RowLabel = StandInWidget(RowLabelPackage, [](UWidgetBlueprint*) {});

		UWidgetTree* Tree = Blueprint->WidgetTree;
		UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		Tree->RootWidget = Root;

		// The game's titled window fills WindowHost at run time; the content lies over it.
		UOverlay* Window = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Window"));
		UCanvasPanelSlot* WindowSlot = Root->AddChildToCanvas(Window);
		WindowSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		WindowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		WindowSlot->SetSize(WindowSize);

		USizeBox* WindowHost = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("WindowHost"));
		WindowHost->bIsVariable = true;
		UOverlaySlot* HostSlot = Window->AddChildToOverlay(WindowHost);
		HostSlot->SetHorizontalAlignment(HAlign_Fill);
		HostSlot->SetVerticalAlignment(VAlign_Fill);

		UHorizontalBox* Columns = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Columns"));
		UOverlaySlot* ColumnsSlot = Window->AddChildToOverlay(Columns);
		ColumnsSlot->SetPadding(WindowInside);
		ColumnsSlot->SetHorizontalAlignment(HAlign_Fill);
		ColumnsSlot->SetVerticalAlignment(VAlign_Fill);

		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box"));
		USizeBox* SeedColumn = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SeedColumn"));
		SeedColumn->SetWidthOverride(SeedColumnWidth);
		SeedColumn->SetContent(Box);
		Columns->AddChildToHorizontalBox(SeedColumn)->SetPadding(FMargin(0.f, 0.f, 36.f, 0.f));

		UVerticalBox* ZonesColumn = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ZonesColumn"));
		Columns->AddChildToHorizontalBox(ZonesColumn)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

		const auto AddHeader = [&](UVerticalBox* Column, const FString& Name, const FString& Text)
		{
			UOverlay* Header = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *(Name + TEXT("Header")));
			UImage* Bar = Tree->ConstructWidget<UImage>(UImage::StaticClass(), *(Name + TEXT("HeaderBar")));
			Bar->SetBrush(TextureBrush(ButtonSelectImage, FVector2D(256.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
			UOverlaySlot* BarSlot = Header->AddChildToOverlay(Bar);
			BarSlot->SetHorizontalAlignment(HAlign_Fill);
			BarSlot->SetVerticalAlignment(VAlign_Fill);
			UOverlaySlot* TextSlot = Header->AddChildToOverlay(GameText(Tree, Style, *(Name + TEXT("HeaderText")), Text));
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
			USizeBox* HeaderBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Name + TEXT("HeaderBox")));
			HeaderBox->SetHeightOverride(RowHeight);
			HeaderBox->SetContent(Header);
			Column->AddChildToVerticalBox(HeaderBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
		};
		AddHeader(Box, TEXT("Seed"), TEXT("- Battle encounters"));
		AddHeader(ZonesColumn, TEXT("Zones"), TEXT("- Zones"));

		const auto StyleScroll = [](UScrollBox* Scroll)
		{
			FScrollBarStyle Bar = Scroll->GetWidgetBarStyle();
			FSlateBrush Track;
			Track.TintColor = FSlateColor(FLinearColor(0.f, 0.02f, 0.04f, 0.8f));
			Bar.SetVerticalBackgroundImage(Track);
			FSlateBrush Thumb;
			Thumb.TintColor = FSlateColor(FLinearColor(0.004f, 0.571f, 1.f, 0.9f));
			Bar.SetNormalThumbImage(Thumb);
			Thumb.TintColor = FSlateColor(FLinearColor(0.2f, 0.7f, 1.f, 1.f));
			Bar.SetHoveredThumbImage(Thumb);
			Bar.SetDraggedThumbImage(Thumb);
			Scroll->SetWidgetBarStyle(Bar);
			Scroll->SetScrollbarThickness(FVector2D(8.f));
			Scroll->SetAlwaysShowScrollbar(true);
		};

		// The zone choosers open lists drawn inside the panel rather than popups, so the finger cursor
		// stays over them and each keeps its scroll.
		for (const FZoneListKind& List : ZoneLists)
		{
			const FString Name(List.Name);
			UButton* Select = Tree->ConstructWidget<UButton>(UButton::StaticClass(), *(Name + TEXT("Select")));
			Select->bIsVariable = true;
			FButtonStyle Bar = Select->GetStyle();
			Bar.SetNormal(TextureBrush(ButtonImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
			Bar.SetHovered(TextureBrush(ButtonSelectImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
			Bar.SetPressed(TextureBrush(ButtonSelectImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
			Bar.SetNormalPadding(FMargin(16.f, 0.f));
			Bar.SetPressedPadding(FMargin(16.f, 0.f));
			Select->SetStyle(Bar);
			UHorizontalBox* SelectRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *(Name + TEXT("SelectRow")));
			UCommonTextBlock* SelectText = GameText(Tree, Style, *(Name + TEXT("SelectText")), TEXT(""));
			SelectText->bIsVariable = true;
			UHorizontalBoxSlot* NameSlot = SelectRow->AddChildToHorizontalBox(SelectText);
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NameSlot->SetVerticalAlignment(VAlign_Center);
			SelectRow->AddChildToHorizontalBox(GameText(Tree, Style, *(Name + TEXT("SelectArrow")), TEXT("\u25BC")))->SetVerticalAlignment(VAlign_Center);
			Select->SetContent(SelectRow);
			CastChecked<UButtonSlot>(SelectRow->Slot)->SetHorizontalAlignment(HAlign_Fill);
			USizeBox* SelectBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Name + TEXT("SelectBox")));
			SelectBox->SetHeightOverride(RowHeight);
			SelectBox->SetContent(Select);
			ZonesColumn->AddChildToVerticalBox(SelectBox)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		}

		// The shown zone's battles, with the open list, if any, over them.
		{
			UOverlay* ZoneArea = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("ZoneArea"));
			ZonesColumn->AddChildToVerticalBox(ZoneArea)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

			UScrollBox* Scroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ZoneScroll"));
			Scroll->bIsVariable = true;
			StyleScroll(Scroll);
			UCommonTextBlock* ZoneList = GameText(Tree, Style, TEXT("ZoneList"), TEXT("Shown once a save is loaded."));
			ZoneList->bIsVariable = true;
			ZoneList->SetAutoWrapText(true);
			Scroll->AddChild(ZoneList);
			UBorder* Strip = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ZoneStrip"));
			Strip->SetBrushColor(ValueFill);
			Strip->SetPadding(FMargin(24.f, 12.f, 8.f, 12.f));
			Strip->SetContent(Scroll);
			UOverlaySlot* StripSlot = ZoneArea->AddChildToOverlay(Strip);
			StripSlot->SetHorizontalAlignment(HAlign_Fill);
			StripSlot->SetVerticalAlignment(VAlign_Fill);

			FButtonStyle Item = GetDefault<UButton>()->GetStyle();
			FSlateBrush Clear;
			Clear.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
			FSlateBrush Lit;
			Lit.TintColor = FSlateColor(FLinearColor(0.004f, 0.571f, 1.f, 0.35f));
			FSlateBrush Down;
			Down.TintColor = FSlateColor(FLinearColor(0.004f, 0.571f, 1.f, 0.6f));
			Item.SetNormal(Clear).SetHovered(Lit).SetPressed(Down);
			Item.SetNormalPadding(FMargin(16.f, 4.f)).SetPressedPadding(FMargin(16.f, 4.f));
			for (const FZoneListKind& List : ZoneLists)
			{
				const FString Name(List.Name);
				UScrollBox* MenuScroll = Tree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), *(Name + TEXT("MenuScroll")));
				MenuScroll->bIsVariable = true;
				StyleScroll(MenuScroll);
				for (int32 Index = 0; Index < List.Entries; ++Index)
				{
					UButton* Entry = Tree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("%sItem%d"), List.Name, Index));
					Entry->bIsVariable = true;
					Entry->SetStyle(Item);
					// Not focusable: a focused entry scrolls its list to itself.
					FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable"))->SetPropertyValue_InContainer(Entry, false);
					UCommonTextBlock* EntryText = GameText(Tree, Style, *FString::Printf(TEXT("%sItemText%d"), List.Name, Index), TEXT(""));
					EntryText->bIsVariable = true;
					Entry->SetContent(EntryText);
					MenuScroll->AddChild(Entry);
					if (UButtonSlot* InButton = Cast<UButtonSlot>(EntryText->Slot)) InButton->SetHorizontalAlignment(HAlign_Left);
				}
				UBorder* Menu = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), *(Name + TEXT("Menu")));
				Menu->bIsVariable = true;
				Menu->SetBrushColor(FLinearColor(0.f, 0.02f, 0.04f, 1.f));
				Menu->SetPadding(FMargin(4.f, 6.f, 8.f, 6.f));
				Menu->SetContent(MenuScroll);
				Menu->SetVisibility(ESlateVisibility::Collapsed);
				UOverlaySlot* MenuSlot = ZoneArea->AddChildToOverlay(Menu);
				MenuSlot->SetHorizontalAlignment(HAlign_Fill);
				MenuSlot->SetVerticalAlignment(VAlign_Fill);
			}
		}

		// An option row: the game's row label (created at run time into LabelHost) and a value strip.
		TArray<FName> LabelHosts;
		const auto AddRow = [&](FName Name, const FString& Caption, UWidget* Value)
		{
			UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), Name);

			UOverlay* Label = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *(Name.ToString() + TEXT("Label")));
			const FName HostName(*(Name.ToString() + TEXT("LabelHost")));
			USizeBox* Host = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), HostName);
			Host->bIsVariable = true;
			UOverlaySlot* HostInLabel = Label->AddChildToOverlay(Host);
			HostInLabel->SetHorizontalAlignment(HAlign_Fill);
			HostInLabel->SetVerticalAlignment(VAlign_Fill);
			UCommonTextBlock* CaptionText = GameText(Tree, Style, *(Name.ToString() + TEXT("Caption")), Caption);
			CaptionText->SetVisibility(ESlateVisibility::HitTestInvisible);
			UOverlaySlot* CaptionSlot = Label->AddChildToOverlay(CaptionText);
			CaptionSlot->SetPadding(FMargin(22.f, 0.f, 8.f, 0.f));
			CaptionSlot->SetVerticalAlignment(VAlign_Center);
			USizeBox* LabelBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Name.ToString() + TEXT("LabelBox")));
			LabelBox->SetWidthOverride(LabelWidth);
			LabelBox->SetMinDesiredHeight(RowHeight);
			LabelBox->SetContent(Label);
			Row->AddChildToHorizontalBox(LabelBox)->SetPadding(FMargin(0.f, 0.f, 2.f, 0.f));
			LabelHosts.Add(HostName);

			UBorder* Strip = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), *(Name.ToString() + TEXT("Value")));
			Strip->SetBrushColor(ValueFill);
			Strip->SetPadding(FMargin(24.f, 6.f));
			Strip->SetVerticalAlignment(VAlign_Center);
			Strip->SetContent(Value);
			UHorizontalBoxSlot* StripSlot = Row->AddChildToHorizontalBox(Strip);
			StripSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

			Box->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
		};

		UCommonTextBlock* SeedLabel = GameText(Tree, Style, TEXT("SeedLabel"), TEXT("waiting for the mod's DLL"));
		SeedLabel->bIsVariable = true;
		AddRow(TEXT("SeedRow"), TEXT("Current seed"), SeedLabel);

		UEditableTextBox* SeedInput = Tree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("SeedInput"));
		SeedInput->bIsVariable = true;
		SeedInput->SetHintText(FText::FromString(TEXT("1 to 8 hexadecimal digits  (Ctrl+V to paste)")));
		FSlateBrush Clear;
		Clear.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
		FSlateBrush Lit;
		Lit.TintColor = FSlateColor(FLinearColor(0.004f, 0.571f, 1.f, 0.18f));
		SeedInput->WidgetStyle.SetBackgroundImageNormal(Clear);
		SeedInput->WidgetStyle.SetBackgroundImageHovered(Lit);
		SeedInput->WidgetStyle.SetBackgroundImageFocused(Lit);
		SeedInput->WidgetStyle.SetBackgroundImageReadOnly(Clear);
		SeedInput->WidgetStyle.SetPadding(FMargin(8.f, 4.f));
		SeedInput->WidgetStyle.TextStyle.SetFont(FSlateFontInfo(Font, 20));
		SeedInput->WidgetStyle.TextStyle.SetColorAndOpacity(FSlateColor(FLinearColor::White));
		SeedInput->WidgetStyle.SetForegroundColor(FSlateColor(FLinearColor::White));
		SeedInput->WidgetStyle.SetFocusedForegroundColor(FSlateColor(FLinearColor::White));
		SeedInput->SetSelectAllTextWhenFocused(true);
		AddRow(TEXT("InputRow"), TEXT("New seed"), SeedInput);

		{
			UButton* ApplyButton = Tree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ApplyButton"));
			ApplyButton->bIsVariable = true;
			FButtonStyle ButtonStyle = ApplyButton->GetStyle();
			ButtonStyle.SetNormal(TextureBrush(ButtonImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
			ButtonStyle.SetHovered(TextureBrush(ButtonSelectImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
			FSlateBrush Pressed = TextureBrush(ButtonSelectImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f);
			Pressed.TintColor = FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, 1.f));
			ButtonStyle.SetPressed(Pressed);
			ButtonStyle.SetNormalPadding(FMargin(0.f));
			ButtonStyle.SetPressedPadding(FMargin(0.f));
			ApplyButton->SetStyle(ButtonStyle);
			ApplyButton->SetContent(GameText(Tree, Style, TEXT("ApplyText"), TEXT("Apply seed")));
			USizeBox* ApplyBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("ApplyBox"));
			ApplyBox->SetHeightOverride(RowHeight);
			ApplyBox->SetContent(ApplyButton);
			Box->AddChildToVerticalBox(ApplyBox)->SetPadding(FMargin(0.f, 2.f, 0.f, 10.f));
		}

		UTextBlock* Prompts = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Prompts"));
		Prompts->SetText(FText::FromString(TEXT("Esc   Close        F2   Open and close")));
		Prompts->SetFont(FSlateFontInfo(Font, 16));
		Prompts->SetColorAndOpacity(FSlateColor(HintColor));
		Prompts->SetShadowOffset(FVector2D(2.f, 1.f));
		Prompts->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.5f));
		UOverlaySlot* PromptSlot = Window->AddChildToOverlay(Prompts);
		PromptSlot->SetPadding(FMargin(0.f, 0.f, WindowInside.Right, 52.f));
		PromptSlot->SetHorizontalAlignment(HAlign_Right);
		PromptSlot->SetVerticalAlignment(VAlign_Bottom);

		// The game's finger cursor, mirrored and turned so its tip is on the mouse. Not named Cursor:
		// UWidget has a Cursor property.
		UImage* Cursor = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FingerCursor"));
		Cursor->bIsVariable = true;
		Cursor->SetBrush(TextureBrush(Finger, FVector2D(CursorSize)));
		Cursor->SetVisibility(ESlateVisibility::HitTestInvisible);
		Cursor->SetRenderTransform(FWidgetTransform(FVector2D(-7.2f, -7.2f), FVector2D(-1.f, 1.f), FVector2D::ZeroVector, 45.f));
		UCanvasPanelSlot* CursorSlot = Root->AddChildToCanvas(Cursor);
		CursorSlot->SetAutoSize(false);
		CursorSlot->SetSize(FVector2D(CursorSize));
		CursorSlot->SetZOrder(100);

		const FName Seed(TEXT("SeedLabel")), Input(TEXT("SeedInput")), Button(TEXT("ApplyButton")), Zones(TEXT("ZoneList"));
		Tree->ForEachWidget([Blueprint](UWidget* Widget)
		{
			Blueprint->OnVariableAdded(Widget->GetFName());
		});

		const FName RequestedSeed(TEXT("RequestedSeed")), RequestedZone(TEXT("RequestedZone")), PrevCursor(TEXT("PrevCursor"));
		FEdGraphPinType StringType;
		StringType.PinCategory = UEdGraphSchema_K2::PC_String;
		FEdGraphPinType BoolType;
		BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, RequestedSeed, StringType)
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, RequestedZone, IntPinType())
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, PrevCursor, BoolType)
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("DungeonOpened"), BoolType)
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, TEXT("FieldOpened"), BoolType))
		{
			throw FBuildError{TEXT("could not add the panel's variables")};
		}

		// The widget variables and members exist on the skeleton class only after this.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		UEdGraph* Graph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (!Graph)
		{
			throw FBuildError{TEXT("WBP_SeedPanel has no event graph")};
		}
		FGraphBuilder Build(Graph);
		const FName Return = UEdGraphSchema_K2::PN_ReturnValue;
		const FName ShowCursor = GET_MEMBER_NAME_CHECKED(APlayerController, bShowMouseCursor);

		// Construct: the game's titled window and row labels, into their hosts.
		{
			UK2Node_Event* Construct = OverrideEvent(Graph, UUserWidget::StaticClass(), TEXT("Construct"));
			const auto CreateInto = [&Build, Return](UClass* Class, FName Host)
			{
				UK2Node_CallFunction* Create = Build.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("Create"));
				UEdGraphPin* WidgetType = FGraphBuilder::Pin(Create, TEXT("WidgetType"));
				WidgetType->DefaultObject = Class;
				Create->PinDefaultValueChanged(WidgetType);
				Build.TargetPlayer(Create, TEXT("OwningPlayer"));
				UK2Node_CallFunction* Place = Build.Call(UContentWidget::StaticClass(), TEXT("SetContent"));
				Build.Target(Host, Place);
				Build.Link(Create, Return, Place, TEXT("Content"));
				return TPair<UK2Node_CallFunction*, UK2Node_CallFunction*>(Create, Place);
			};

			// The window reads its title when first built, so it is set before placing it.
			const auto [CreateWindow, PlaceWindow] = CreateInto(TitleWindow, TEXT("WindowHost"));
			UK2Node_VariableSet* Title = Build.SetOn(TitleWindow, TitleText);
			UK2Node_DynamicCast* AsWindow = Build.CastTo(TitleWindow);
			Build.Link(CreateWindow, Return, AsWindow, UEdGraphSchema_K2::PN_ObjectToCast);
			if (!Graph->GetSchema()->TryCreateConnection(AsWindow->GetCastResultPin(), FGraphBuilder::Pin(Title, UEdGraphSchema_K2::PN_Self)))
			{
				throw FBuildError{TEXT("cannot connect the window cast to its TitleText")};
			}
			FGraphBuilder::Pin(Title, TitleText)->DefaultTextValue = FText::AsCultureInvariant(TEXT("Randomizer"));
			Build.Then(Construct, CreateWindow);
			Build.Then(CreateWindow, Title);
			Build.Then(Title, PlaceWindow);

			UEdGraphNode* Last = PlaceWindow;
			for (const FName Host : LabelHosts)
			{
				const auto [CreateLabel, PlaceLabel] = CreateInto(RowLabel, Host);
				Build.Then(Last, CreateLabel);
				Build.Then(CreateLabel, PlaceLabel);
				Last = PlaceLabel;
			}
		}

		// Apply: RequestedSeed = the seed box's text, which the DLL reads and clears.
		{
			UK2Node_ComponentBoundEvent* OnClicked = BoundEvent(Blueprint, Graph, Button, UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnClicked));
			UK2Node_CallFunction* GetText = Build.Call(UEditableTextBox::StaticClass(), TEXT("GetText"));
			Build.Target(Input, GetText);
			UK2Node_CallFunction* ToString = Build.Call(UKismetTextLibrary::StaticClass(), TEXT("Conv_TextToString"));
			Build.Link(GetText, Return, ToString, TEXT("InText"));
			UK2Node_VariableSet* SetRequest = Build.Set(RequestedSeed);
			Build.Link(ToString, Return, SetRequest, RequestedSeed);
			UK2Node_CallFunction* Decide = Build.PlaySound(SoundDecide);
			Build.Then(OnClicked, SetRequest);
			Build.Then(SetRequest, Decide);

			UK2Node_ComponentBoundEvent* OnHovered = BoundEvent(Blueprint, Graph, Button, UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnHovered));
			Build.Then(OnHovered, Build.PlaySound(SoundCursor));
		}

		const auto Visibility = [&Build](FName Widget, const TCHAR* State)
		{
			UK2Node_CallFunction* Node = Build.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
			Build.Target(Widget, Node);
			Build.Literal(Node, TEXT("InVisibility"), State);
			return Node;
		};

		// A chooser toggles its list and closes the other; the battles under an open list are hidden.
		for (const FZoneListKind& List : ZoneLists)
		{
			const FString Name(List.Name);
			const FName Menu(*(Name + TEXT("Menu")));
			const FName Select(*(Name + TEXT("Select")));
			UK2Node_ComponentBoundEvent* OnClicked = BoundEvent(Blueprint, Graph, Select, UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnClicked));
			UK2Node_CallFunction* Sound = Build.PlaySound(SoundCursor);
			Build.Then(OnClicked, Sound);
			UEdGraphNode* Last = Sound;
			for (const FZoneListKind& Other : ZoneLists)
			{
				if (&Other == &List) continue;
				UK2Node_CallFunction* CloseOther = Visibility(*(FString(Other.Name) + TEXT("Menu")), TEXT("Collapsed"));
				Build.Then(Last, CloseOther);
				Last = CloseOther;
			}
			UK2Node_CallFunction* IsOpen = Build.Call(UWidget::StaticClass(), TEXT("IsVisible"));
			Build.Target(Menu, IsOpen);
			UK2Node_IfThenElse* Branch = Build.Branch();
			Build.Link(IsOpen, Return, Branch, UEdGraphSchema_K2::PN_Condition);
			Build.Then(Last, Branch);
			UK2Node_CallFunction* Close = Visibility(Menu, TEXT("Collapsed"));
			UK2Node_CallFunction* ShowBattles = Visibility(TEXT("ZoneScroll"), TEXT("Visible"));
			Build.Then(Branch, UEdGraphSchema_K2::PN_Then, Close);
			Build.Then(Close, ShowBattles);
			UK2Node_CallFunction* Open = Visibility(Menu, TEXT("Visible"));
			UK2Node_CallFunction* HideBattles = Visibility(TEXT("ZoneScroll"), TEXT("Hidden"));
			Build.Then(Branch, UEdGraphSchema_K2::PN_Else, Open);
			Build.Then(Open, HideBattles);
			// The first opening starts at the top; later ones keep the scroll.
			const FName Opened(*(Name + TEXT("Opened")));
			UK2Node_IfThenElse* First = Build.Branch();
			Build.Link(Build.Get(Opened), Opened, First, UEdGraphSchema_K2::PN_Condition);
			UK2Node_CallFunction* ToTop = Build.Call(UScrollBox::StaticClass(), TEXT("ScrollToStart"));
			Build.Target(*(Name + TEXT("MenuScroll")), ToTop);
			UK2Node_VariableSet* SetOpened = Build.Set(Opened);
			Build.Literal(SetOpened, Opened, TEXT("true"));
			Build.Then(HideBattles, First);
			Build.Then(First, UEdGraphSchema_K2::PN_Else, ToTop);
			Build.Then(ToTop, SetOpened);

			UK2Node_ComponentBoundEvent* OnHovered = BoundEvent(Blueprint, Graph, Select, UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnHovered));
			Build.Then(OnHovered, Build.PlaySound(SoundCursor));
		}

		// ChooseZone(Kind, Index): RequestedZone = Kind * 1000 + Index + 1, which the DLL reads and clears.
		{
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* Choose = Creator.CreateNode();
			Choose->CustomFunctionName = TEXT("ChooseZone");
			Creator.Finalize();
			Choose->CreateUserDefinedPin(TEXT("Kind"), IntPinType(), EGPD_Output);
			Choose->CreateUserDefinedPin(TEXT("Index"), IntPinType(), EGPD_Output);

			UK2Node_CallFunction* Times = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Multiply_IntInt"));
			Build.Link(Choose, TEXT("Kind"), Times, TEXT("A"));
			Build.Literal(Times, TEXT("B"), TEXT("1000"));
			UK2Node_CallFunction* Plus = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Add_IntInt"));
			Build.Link(Times, Return, Plus, TEXT("A"));
			Build.Link(Choose, TEXT("Index"), Plus, TEXT("B"));
			UK2Node_CallFunction* PlusOne = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Add_IntInt"));
			Build.Link(Plus, Return, PlusOne, TEXT("A"));
			Build.Literal(PlusOne, TEXT("B"), TEXT("1"));
			UK2Node_VariableSet* SetRequest = Build.Set(RequestedZone);
			Build.Link(PlusOne, Return, SetRequest, RequestedZone);
			Build.Then(Choose, SetRequest);
			UEdGraphNode* Last = SetRequest;
			for (const FZoneListKind& List : ZoneLists)
			{
				UK2Node_CallFunction* Close = Visibility(*(FString(List.Name) + TEXT("Menu")), TEXT("Collapsed"));
				Build.Then(Last, Close);
				Last = Close;
			}
			UK2Node_CallFunction* ShowBattles = Visibility(TEXT("ZoneScroll"), TEXT("Visible"));
			UK2Node_CallFunction* Sound = Build.PlaySound(SoundDecide);
			Build.Then(Last, ShowBattles);
			Build.Then(ShowBattles, Sound);
		}

		// UpdateDisplay(SeedText, DungeonName, FieldName, ZoneText): called by the DLL. The battle list
		// returns to its top, so a short zone is never scrolled out of sight.
		{
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* Update = Creator.CreateNode();
			Update->CustomFunctionName = TEXT("UpdateDisplay");
			Creator.Finalize();
			Update->CreateUserDefinedPin(TEXT("SeedText"), StringType, EGPD_Output);
			Update->CreateUserDefinedPin(TEXT("DungeonName"), StringType, EGPD_Output);
			Update->CreateUserDefinedPin(TEXT("FieldName"), StringType, EGPD_Output);
			Update->CreateUserDefinedPin(TEXT("ZoneText"), StringType, EGPD_Output);

			const auto SetTextFrom = [&Build, Return](FName Widget, UClass* WidgetClass, UEdGraphNode* Source, FName SourcePin)
			{
				UK2Node_CallFunction* Convert = Build.Call(UKismetTextLibrary::StaticClass(), TEXT("Conv_StringToText"));
				Build.Link(Source, SourcePin, Convert, TEXT("InString"));
				UK2Node_CallFunction* SetText = Build.Call(WidgetClass, TEXT("SetText"));
				Build.Target(Widget, SetText);
				Build.Link(Convert, Return, SetText, TEXT("InText"));
				return SetText;
			};
			UK2Node_CallFunction* SetSeed = SetTextFrom(Seed, UTextBlock::StaticClass(), Update, TEXT("SeedText"));
			UK2Node_CallFunction* SetInput = SetTextFrom(Input, UEditableTextBox::StaticClass(), Update, TEXT("SeedText"));
			UK2Node_CallFunction* SetDungeon = SetTextFrom(TEXT("DungeonSelectText"), UTextBlock::StaticClass(), Update, TEXT("DungeonName"));
			UK2Node_CallFunction* SetField = SetTextFrom(TEXT("FieldSelectText"), UTextBlock::StaticClass(), Update, TEXT("FieldName"));
			UK2Node_CallFunction* SetZone = SetTextFrom(Zones, UTextBlock::StaticClass(), Update, TEXT("ZoneText"));
			UK2Node_CallFunction* ToTop = Build.Call(UScrollBox::StaticClass(), TEXT("ScrollToStart"));
			Build.Target(TEXT("ZoneScroll"), ToTop);
			Build.Then(Update, SetSeed);
			Build.Then(SetSeed, SetInput);
			Build.Then(SetInput, SetDungeon);
			Build.Then(SetDungeon, SetField);
			Build.Then(SetField, SetZone);
			Build.Then(SetZone, ToTop);
		}

		// TogglePanel: called by the DLL on F2 and by the panel on Back. Opening shows the panel and
		// the mouse cursor and gives the UI all input; closing gives everything back.
		{
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* Toggle = Creator.CreateNode();
			Toggle->CustomFunctionName = TEXT("TogglePanel");
			Creator.Finalize();

			UK2Node_CallFunction* IsOpen = Build.Call(UWidget::StaticClass(), TEXT("IsVisible"));
			UK2Node_IfThenElse* Branch = Build.Branch();
			Build.Link(IsOpen, Return, Branch, UEdGraphSchema_K2::PN_Condition);
			Build.Then(Toggle, Branch);

			// Open.
			UK2Node_VariableGet* WasShown = Build.GetOn(APlayerController::StaticClass(), ShowCursor);
			Build.TargetPlayer(WasShown);
			UK2Node_VariableSet* Remember = Build.Set(PrevCursor);
			Build.Link(WasShown, ShowCursor, Remember, PrevCursor);
			UK2Node_CallFunction* Show = Build.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
			Build.Literal(Show, TEXT("InVisibility"), TEXT("Visible"));
			UK2Node_VariableSet* CursorOn = Build.SetOn(APlayerController::StaticClass(), ShowCursor);
			Build.TargetPlayer(CursorOn);
			Build.Literal(CursorOn, ShowCursor, TEXT("true"));
			UK2Node_CallFunction* UIOnly = Build.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("SetInputMode_UIOnlyEx"));
			Build.TargetPlayer(UIOnly, TEXT("PlayerController"));
			Build.Link(Build.Self(), UEdGraphSchema_K2::PN_Self, UIOnly, TEXT("InWidgetToFocus"));
			Build.Literal(UIOnly, TEXT("InMouseLockMode"), TEXT("DoNotLock"));
			UK2Node_CallFunction* OpenSound = Build.PlaySound(SoundOpen);
			Build.Then(Branch, UEdGraphSchema_K2::PN_Else, Remember);
			Build.Then(Remember, Show);
			Build.Then(Show, CursorOn);
			Build.Then(CursorOn, UIOnly);
			Build.Then(UIOnly, OpenSound);

			// Close.
			UK2Node_CallFunction* CloseSound = Build.PlaySound(SoundClose);
			UK2Node_CallFunction* Hide = Build.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
			Build.Literal(Hide, TEXT("InVisibility"), TEXT("Collapsed"));
			UK2Node_CallFunction* GameOnly = Build.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("SetInputMode_GameOnly"));
			Build.TargetPlayer(GameOnly, TEXT("PlayerController"));
			UK2Node_VariableSet* CursorBack = Build.SetOn(APlayerController::StaticClass(), ShowCursor);
			Build.TargetPlayer(CursorBack);
			Build.Link(Build.Get(PrevCursor), PrevCursor, CursorBack, ShowCursor);
			Build.Then(Branch, UEdGraphSchema_K2::PN_Then, CloseSound);
			Build.Then(CloseSound, Hide);
			Build.Then(Hide, GameOnly);
			Build.Then(GameOnly, CursorBack);
		}

		// Tick, only while shown: the game hides the mouse cursor on its own, so keep it on, and move
		// the finger to the mouse.
		{
			UK2Node_Event* Tick = OverrideEvent(Graph, UUserWidget::StaticClass(), TEXT("Tick"));
			UK2Node_VariableSet* CursorOn = Build.SetOn(APlayerController::StaticClass(), ShowCursor);
			Build.TargetPlayer(CursorOn);
			Build.Literal(CursorOn, ShowCursor, TEXT("true"));

			UK2Node_CallFunction* Mouse = Build.Call(UWidgetLayoutLibrary::StaticClass(), TEXT("GetMousePositionOnViewport"));
			UK2Node_CallFunction* AsCanvasSlot = Build.Call(UWidgetLayoutLibrary::StaticClass(), TEXT("SlotAsCanvasSlot"));
			Build.Link(Build.Get(TEXT("FingerCursor")), TEXT("FingerCursor"), AsCanvasSlot, TEXT("Widget"));
			UK2Node_CallFunction* Move = Build.Call(UCanvasPanelSlot::StaticClass(), TEXT("SetPosition"));
			Build.Link(AsCanvasSlot, Return, Move, UEdGraphSchema_K2::PN_Self);
			Build.Link(Mouse, Return, Move, TEXT("InPosition"));
			// Not pure: it has to be on the execution chain.
			Build.Then(Tick, CursorOn);
			Build.Then(CursorOn, Mouse);
			Build.Then(Mouse, Move);
		}

		// TogglePanel and ChooseZone exist on the class only after this.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		for (const FZoneListKind& List : ZoneLists)
		{
			for (int32 Index = 0; Index < List.Entries; ++Index)
			{
				UK2Node_ComponentBoundEvent* OnClicked = BoundEvent(Blueprint, Graph, *FString::Printf(TEXT("%sItem%d"), List.Name, Index), UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnClicked));
				UK2Node_CallFunction* Choose = Build.Call(Blueprint->SkeletonGeneratedClass, TEXT("ChooseZone"));
				Build.Literal(Choose, TEXT("Kind"), FString::FromInt(List.Kind));
				Build.Literal(Choose, TEXT("Index"), FString::FromInt(Index));
				Build.Then(OnClicked, Choose);
			}
		}

		// On Key Down: Back (Escape) closes the panel; any other key is left to the widgets.
		{
			UEdGraph* Function = OverrideFunction(Blueprint, UUserWidget::StaticClass(), TEXT("OnKeyDown"));
			FGraphBuilder Override(Function);
			UK2Node_FunctionEntry* Entry = Override.Find<UK2Node_FunctionEntry>();
			UK2Node_FunctionResult* Result = Override.Find<UK2Node_FunctionResult>();

			UK2Node_CallFunction* Action = Override.Call(UKismetInputLibrary::StaticClass(), TEXT("Key_GetNavigationActionFromKey"));
			Override.Link(Entry, TEXT("InKeyEvent"), Action, TEXT("InKeyEvent"));
			UK2Node_SwitchEnum* Switch = Override.Switch(StaticEnum<EUINavigationAction>());
			Override.Link(Action, Return, Switch, TEXT("Selection"));
			UK2Node_CallFunction* Close = Override.Call(Blueprint->SkeletonGeneratedClass, TEXT("TogglePanel"));
			UK2Node_CallFunction* Handled = Override.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("Handled"));
			Override.Link(Handled, Return, Result, Return);
			Override.Then(Entry, Switch);
			Override.Then(Switch, TEXT("Back"), Close);
			Override.Then(Close, Result);
		}

		// Focusable, so it gets the keyboard and sees Back.
		CastChecked<UUserWidget>(Blueprint->GeneratedClass->GetDefaultObject())->SetIsFocusable(true);

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
		const FName Return = UEdGraphSchema_K2::PN_ReturnValue;

		// BeginPlay: the panel, in the viewport and collapsed.
		{
			UK2Node_Event* BeginPlay = EnabledBeginPlay(Blueprint, Graph);
			UK2Node_CallFunction* Create = Build.Call(UWidgetBlueprintLibrary::StaticClass(), TEXT("Create"));
			UEdGraphPin* WidgetType = FGraphBuilder::Pin(Create, TEXT("WidgetType"));
			WidgetType->DefaultObject = Panel->GeneratedClass;
			Build.TargetPlayer(Create, TEXT("OwningPlayer"));

			UK2Node_VariableSet* SetPanel = Build.Set(PanelVariable);
			Build.Link(Create, Return, SetPanel, PanelVariable);

			UK2Node_CallFunction* AddToViewport = Build.Call(UUserWidget::StaticClass(), TEXT("AddToViewport"));
			Build.Target(PanelVariable, AddToViewport);
			Build.Literal(AddToViewport, TEXT("ZOrder"), TEXT("100"));

			UK2Node_CallFunction* Hide = Build.Call(UWidget::StaticClass(), TEXT("SetVisibility"));
			Build.Target(PanelVariable, Hide);
			Build.Literal(Hide, TEXT("InVisibility"), TEXT("Collapsed"));

			Build.Then(BeginPlay, Create);
			Build.Then(Create, SetPanel);
			Build.Then(SetPanel, AddToViewport);
			Build.Then(AddToViewport, Hide);
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
