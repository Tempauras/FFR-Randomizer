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
#include "Components/Spacer.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "CPP_SoundSubsystem.h"
#include "CPP_WBP_MuchaActivatable.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "GameFramework/PlayerController.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CallParentFunction.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_GetSubsystem.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Self.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
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
	// The game's settings mark the chosen side of a choice with these (WBP_OP_RadioButtonParts): the light,
	// tinted, in a 40 high box, and a 2 high white line along its foot.
	const TCHAR* const ChoiceLightMaterial = TEXT("/Game/UI/Textures/Menu/M_menu_Tab_Cursor_Light");
	const TCHAR* const WhiteTexture = TEXT("/Game/UI/Textures/Menu/T_Menu_white.T_Menu_white");
	const FLinearColor ChoiceLightTint(0.015996f, 0.14996f, 0.300544f, 1.f);
	constexpr float ChoiceLightHeight = 40.f;
	const TCHAR* const SystemFont = TEXT("/Game/UI/Font/SystemText.SystemText");
	const TCHAR* const SystemTextStyle = TEXT("/Game/UI/Common/Text/TextStyle/SystemText/BP_SystemTextStyle.BP_SystemTextStyle_C");

	// Game widgets the panel creates at run time: saved as instances, they would load with the
	// stand-in's property layout rather than the game's.
	const TCHAR* const TitleWindowPackage = TEXT("/Game/UI/Common/Layout/Panel/WBP_TitlePanelLayout");
	const TCHAR* const RowLabelPackage = TEXT("/Game/UI/Common/Button/LeftLine/Widget/WBP_LeftLineButton");
	// The darkening the game's screens lay over the field behind them.
	const TCHAR* const DimmingPackage = TEXT("/Game/UI/Common/Layout/WBP_BGDimming");

	// The prompts the game shows at the bottom right while the panel is on top.
	const TCHAR* const ActionGuideTag = TEXT("UI.System.ActionGuide.Declare.Common.Close");

	// The space the window's frame and title leave for content, around the seed column.
	const FMargin WindowInside(211.f, 125.f, 211.f, 110.f);

	// The value strips' dark teal, from the game's settings rows.
	const FLinearColor ValueFill(FColor(12, 22, 27, 235));
	constexpr float RowHeight = 50.f;
	constexpr float LabelWidth = 340.f;
	constexpr float SeedColumnWidth = 1100.f;
	// The options that share a line, two by two in their order (battles and bosses, then their scalings): each half
	// has a shorter label, with this gap between the halves.
	constexpr int32 PairedOptions = 4;
	constexpr float HalfLabelWidth = 285.f;
	constexpr float PairGap = 16.f;
	constexpr float HalfWidth = (SeedColumnWidth - PairGap) / 2.f;
	// The other options' rows, centred under the pairs.
	constexpr float CentredRowWidth = 600.f;
	constexpr float CursorSize = 34.f;
	const FVector2D WindowSize(WindowInside.Left + SeedColumnWidth + WindowInside.Right, 1060.f);

	// The DLL's options, in its order. On and Off call Choose with these kinds; the DLL shows the mark
	// under the current choice.
	const TCHAR* const OptionNames[] = {
		TEXT("Normal battles"), TEXT("Boss battles"), TEXT("Normal scaling"), TEXT("Boss scaling"),
		TEXT("Enemy drops and steals"), TEXT("Chests"), TEXT("Shops"), TEXT("Visions"),
	};
	constexpr int32 OptionOnKind = 3;
	constexpr int32 OptionOffKind = 4;
	constexpr float ChoiceWidth = 120.f;
	// Rows that choose from a list instead of On and Off: the option's row, its widgets' prefix, its choices and
	// the Choose kind an entry sends with its index. The DLL writes the bar's text and marks the current entry.
	struct FDropdown
	{
		int32 Option;
		const TCHAR* Prefix;
		TArray<const TCHAR*> Choices;
		int32 Kind;
	};
	const FDropdown Dropdowns[] = {
		{2, TEXT("Scaling"), {TEXT("Off"), TEXT("Replaced stats"), TEXT("Balanced")}, 6},
		{3, TEXT("BossScaling"), {TEXT("Off"), TEXT("Replaced stats"), TEXT("Balanced")}, 7},
		{4, TEXT("Loot"), {TEXT("Off"), TEXT("Drops"), TEXT("Steals"), TEXT("Both"), TEXT("Mixed")}, 5},
	};

	const FDropdown* DropdownFor(int32 Option)
	{
		for (const FDropdown& Dropdown : Dropdowns)
		{
			if (Dropdown.Option == Option) return &Dropdown;
		}
		return nullptr;
	}

	// The game's menu input, queued for the DLL in PendingInput as numbers: 100 + direction * 10 for
	// InputDirection (EUINavigation: Left, Right, Up, Down), 200 for InputDecide, 300 for InputCancel and
	// 400 for InputEscape, each plus the EWidgetInputEventType, and a comma after each.
	constexpr int32 InputDirectionCode = 100;
	constexpr int32 InputDecideCode = 200;
	constexpr int32 InputCancelCode = 300;
	constexpr int32 InputEscapeCode = 400;

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

	UPackage* NewAssetPackage(const FString& AssetName)
	{
		const FString Path = FString(ModFolder) + AssetName;
		if (FPackageName::DoesPackageExist(Path))
		{
			throw FBuildError{Path + TEXT(" already exists; delete the FFRRandomizer folder to generate it again")};
		}
		return CreatePackage(*Path);
	}

	void SaveAsset(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
		if (!UPackage::SavePackage(Package, Asset, *File, Args))
		{
			throw FBuildError{TEXT("could not save ") + File};
		}
		UE_LOG(LogFFRRandomizerUI, Display, TEXT("Saved %s"), *File);
	}

	void CompileAndSave(UBlueprint* Blueprint)
	{
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		if (Blueprint->Status == BS_Error)
		{
			throw FBuildError{Blueprint->GetName() + TEXT(" did not compile")};
		}

		SaveAsset(Blueprint);
	}

	// An editor stand-in for a game material at its shipped path, made once and never packed: a plain UI material.
	UMaterialInterface* StandInMaterial(const TCHAR* PackagePath)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (FPackageName::DoesPackageExist(PackagePath))
		{
			return LoadAsset<UMaterialInterface>(*(FString(PackagePath) + TEXT(".") + AssetName));
		}
		UMaterial* Material = NewObject<UMaterial>(CreatePackage(PackagePath), *AssetName, RF_Public | RF_Standalone);
		Material->MaterialDomain = MD_UI;
		Material->PostEditChange();
		SaveAsset(Material);
		return Material;
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
		// A game screen: pushed on the game's layer stack, it gets the game's menu input and prompts.
		UWidgetBlueprint* Blueprint = CastChecked<UWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
			UCPP_WBP_MuchaActivatable::StaticClass(), NewAssetPackage(TEXT("WBP_SeedPanel")), TEXT("WBP_SeedPanel"), BPTYPE_Normal,
			UWidgetBlueprint::StaticClass(), UWidgetBlueprintGeneratedClass::StaticClass()));

		UFont* Font = LoadAsset<UFont>(SystemFont);
		UClass* Style = LoadAsset<UClass>(SystemTextStyle);
		UTexture2D* FingerImage = LoadAsset<UTexture2D>(CursorTexture);
		UTexture2D* ButtonImage = LoadAsset<UTexture2D>(ButtonTexture);
		UTexture2D* ButtonSelectImage = LoadAsset<UTexture2D>(ButtonSelectTexture);
		UMaterialInterface* ChoiceLight = StandInMaterial(ChoiceLightMaterial);
		UTexture2D* White = LoadAsset<UTexture2D>(WhiteTexture);

		const FName TitleText(TEXT("TitleText"));
		UClass* TitleWindow = StandInWidget(TitleWindowPackage, [TitleText](UWidgetBlueprint* StandIn)
		{
			FEdGraphPinType TextType;
			TextType.PinCategory = UEdGraphSchema_K2::PC_Text;
			FBlueprintEditorUtils::AddMemberVariable(StandIn, TitleText, TextType);
		});
		UClass* RowLabel = StandInWidget(RowLabelPackage, [](UWidgetBlueprint*) {});
		UClass* Dimming = StandInWidget(DimmingPackage, [](UWidgetBlueprint*) {});

		UWidgetTree* Tree = Blueprint->WidgetTree;
		UCanvasPanel* Root = Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
		Tree->RootWidget = Root;

		// The game's dimming fills DimHost at run time, under everything else.
		USizeBox* DimHost = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("DimHost"));
		DimHost->bIsVariable = true;
		UCanvasPanelSlot* DimSlot = Root->AddChildToCanvas(DimHost);
		DimSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
		DimSlot->SetOffsets(FMargin(0.f));

		// The finger that points at what the controller is on, left of it; the DLL shows one at a time.
		const auto AddFinger = [&](UOverlay* Into, const FString& Name)
		{
			UImage* Finger = Tree->ConstructWidget<UImage>(UImage::StaticClass(), *(Name + TEXT("Finger")));
			Finger->bIsVariable = true;
			Finger->SetBrush(TextureBrush(FingerImage, FVector2D(CursorSize)));
			Finger->SetVisibility(ESlateVisibility::Collapsed);
			Finger->SetRenderTransform(FWidgetTransform(FVector2D(-CursorSize - 10.f, 0.f), FVector2D(1.f), FVector2D::ZeroVector, 0.f));
			UOverlaySlot* FingerSlot = Into->AddChildToOverlay(Finger);
			FingerSlot->SetHorizontalAlignment(HAlign_Left);
			FingerSlot->SetVerticalAlignment(VAlign_Center);
		};

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

		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box"));
		USizeBox* SeedColumn = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SeedColumn"));
		SeedColumn->SetWidthOverride(SeedColumnWidth);
		SeedColumn->SetContent(Box);
		UOverlaySlot* ColumnSlot = Window->AddChildToOverlay(SeedColumn);
		ColumnSlot->SetPadding(WindowInside);
		ColumnSlot->SetHorizontalAlignment(HAlign_Fill);
		ColumnSlot->SetVerticalAlignment(VAlign_Fill);

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
		AddHeader(Box, TEXT("Seed"), TEXT("- Seed"));

		// A list entry: a clear button that lights up under the cursor.
		const auto ListItemStyle = []()
		{
			FButtonStyle Item = GetDefault<UButton>()->GetStyle();
			FSlateBrush Clear;
			Clear.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
			FSlateBrush Lit;
			Lit.TintColor = FSlateColor(FLinearColor(0.004f, 0.571f, 1.f, 0.35f));
			FSlateBrush Down;
			Down.TintColor = FSlateColor(FLinearColor(0.004f, 0.571f, 1.f, 0.6f));
			Item.SetNormal(Clear).SetHovered(Lit).SetPressed(Down);
			Item.SetNormalPadding(FMargin(16.f, 4.f)).SetPressedPadding(FMargin(16.f, 4.f));
			return Item;
		};

		// An option row: the game's row label (created at run time into LabelHost) and a value strip.
		TArray<FName> LabelHosts;
		const auto MakeRow = [&](FName Name, const FString& Caption, UWidget* Value, float RowLabelWidth, FMargin Inset)
		{
			UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), Name);
			Row->bIsVariable = true;

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
			AddFinger(Label, Name.ToString());
			USizeBox* LabelBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Name.ToString() + TEXT("LabelBox")));
			LabelBox->SetWidthOverride(RowLabelWidth);
			LabelBox->SetMinDesiredHeight(RowHeight);
			LabelBox->SetContent(Label);
			Row->AddChildToHorizontalBox(LabelBox)->SetPadding(FMargin(0.f, 0.f, 2.f, 0.f));
			LabelHosts.Add(HostName);

			UBorder* Strip = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), *(Name.ToString() + TEXT("Value")));
			Strip->SetBrushColor(ValueFill);
			Strip->SetPadding(Inset);
			Strip->SetVerticalAlignment(VAlign_Center);
			Strip->SetContent(Value);
			UHorizontalBoxSlot* StripSlot = Row->AddChildToHorizontalBox(Strip);
			StripSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			return Row;
		};
		const auto AddRow = [&](FName Name, const FString& Caption, UWidget* Value, UVerticalBox* Into = nullptr, FMargin Inset = FMargin(24.f, 6.f))
		{
			(Into ? Into : Box)->AddChildToVerticalBox(MakeRow(Name, Caption, Value, LabelWidth, Inset))->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
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

		// What the seed randomizes: a row per option, like the game's settings, with an On and an Off choice or a
		// dropdown. The first PairedOptions share lines two by two.
		AddHeader(Box, TEXT("Options"), TEXT("- Randomize"));
		{
			const FButtonStyle Item = ListItemStyle();

			// A dropdown's bar, with the current choice.
			const auto MakeBar = [&](const FDropdown& Dropdown)
			{
				const FString Prefix(Dropdown.Prefix);
				UButton* Select = Tree->ConstructWidget<UButton>(UButton::StaticClass(), *(Prefix + TEXT("Select")));
				Select->bIsVariable = true;
				FButtonStyle Bar = Select->GetStyle();
				FSlateBrush NoBar;
				NoBar.TintColor = FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.f));
				Bar.SetNormal(NoBar);
				Bar.SetHovered(TextureBrush(ButtonSelectImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
				Bar.SetPressed(TextureBrush(ButtonSelectImage, FVector2D(512.f, 32.f), ESlateBrushDrawType::Box, 0.5f));
				Bar.SetNormalPadding(FMargin(24.f, 0.f));
				Bar.SetPressedPadding(FMargin(24.f, 0.f));
				Select->SetStyle(Bar);
				FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable"))->SetPropertyValue_InContainer(Select, false);
				UHorizontalBox* SelectRow = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *(Prefix + TEXT("SelectRow")));
				UCommonTextBlock* SelectText = GameText(Tree, Style, *(Prefix + TEXT("SelectText")), Dropdown.Choices.Last());
				SelectText->bIsVariable = true;
				UHorizontalBoxSlot* NameSlot = SelectRow->AddChildToHorizontalBox(SelectText);
				NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				NameSlot->SetVerticalAlignment(VAlign_Center);
				SelectRow->AddChildToHorizontalBox(GameText(Tree, Style, *(Prefix + TEXT("SelectArrow")), TEXT("\u25BC")))->SetVerticalAlignment(VAlign_Center);
				Select->SetContent(SelectRow);
				CastChecked<UButtonSlot>(SelectRow->Slot)->SetHorizontalAlignment(HAlign_Fill);
				CastChecked<UButtonSlot>(SelectRow->Slot)->SetVerticalAlignment(VAlign_Fill);
				return Select;
			};

			// A dropdown's list, collapsed until its bar opens it.
			const auto MakeMenu = [&](const FDropdown& Dropdown)
			{
				const FString Prefix(Dropdown.Prefix);
				UVerticalBox* List = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *(Prefix + TEXT("List")));
				FButtonStyle Tight = Item;
				Tight.SetNormalPadding(FMargin(16.f, 2.f)).SetPressedPadding(FMargin(16.f, 2.f));
				for (int32 Choice = 0; Choice < Dropdown.Choices.Num(); ++Choice)
				{
					UButton* Entry = Tree->ConstructWidget<UButton>(UButton::StaticClass(), *FString::Printf(TEXT("%sItem%d"), *Prefix, Choice));
					Entry->bIsVariable = true;
					Entry->SetStyle(Tight);
					FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable"))->SetPropertyValue_InContainer(Entry, false);
					UCommonTextBlock* EntryText = GameText(Tree, Style, *FString::Printf(TEXT("%sItemText%d"), *Prefix, Choice), Dropdown.Choices[Choice]);
					EntryText->bIsVariable = true;
					Entry->SetContent(EntryText);
					List->AddChildToVerticalBox(Entry);
					if (UButtonSlot* InButton = Cast<UButtonSlot>(EntryText->Slot)) InButton->SetHorizontalAlignment(HAlign_Left);
				}
				UBorder* Menu = Tree->ConstructWidget<UBorder>(UBorder::StaticClass(), *(Prefix + TEXT("Menu")));
				Menu->bIsVariable = true;
				Menu->SetBrushColor(FLinearColor(0.f, 0.02f, 0.04f, 1.f));
				Menu->SetPadding(FMargin(4.f, 6.f, 8.f, 6.f));
				Menu->SetContent(List);
				Menu->SetVisibility(ESlateVisibility::Collapsed);
				return Menu;
			};

			// An option's On and Off, with the mark on the chosen side.
			const auto MakeChoices = [&](int32 Index)
			{
				UHorizontalBox* Choices = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *FString::Printf(TEXT("Option%dChoices"), Index));
				Choices->AddChildToHorizontalBox(Tree->ConstructWidget<USpacer>(USpacer::StaticClass(), *FString::Printf(TEXT("Option%dSpace"), Index)))
					->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				for (const TCHAR* Choice : {TEXT("On"), TEXT("Off")})
				{
					const FString Name = FString::Printf(TEXT("Option%s%d"), Choice, Index);
					UOverlay* Cell = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *(Name + TEXT("Cell")));
					// The mark on the chosen side, built like the game's settings: its light in a 40 high box with a
					// white line along the foot, centred in the row.
					UOverlay* Mark = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *(Name + TEXT("Mark")));
					Mark->bIsVariable = true;
					Mark->SetVisibility(ESlateVisibility::Collapsed);
					UImage* Light = Tree->ConstructWidget<UImage>(UImage::StaticClass(), *(Name + TEXT("Light")));
					FSlateBrush LightBrush;
					LightBrush.SetResourceObject(ChoiceLight);
					Light->SetBrush(LightBrush);
					Light->SetColorAndOpacity(ChoiceLightTint);
					USizeBox* LightBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Name + TEXT("LightBox")));
					LightBox->SetHeightOverride(ChoiceLightHeight);
					LightBox->SetContent(Light);
					UOverlaySlot* LightSlot = Mark->AddChildToOverlay(LightBox);
					LightSlot->SetHorizontalAlignment(HAlign_Fill);
					LightSlot->SetVerticalAlignment(VAlign_Fill);
					UImage* Line = Tree->ConstructWidget<UImage>(UImage::StaticClass(), *(Name + TEXT("Line")));
					Line->SetBrush(TextureBrush(White, FVector2D(200.f, 2.f)));
					UOverlaySlot* LineSlot = Mark->AddChildToOverlay(Line);
					LineSlot->SetHorizontalAlignment(HAlign_Fill);
					LineSlot->SetVerticalAlignment(VAlign_Bottom);
					UOverlaySlot* MarkSlot = Cell->AddChildToOverlay(Mark);
					MarkSlot->SetHorizontalAlignment(HAlign_Fill);
					MarkSlot->SetVerticalAlignment(VAlign_Center);

					UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), *(Name + TEXT("Button")));
					Button->bIsVariable = true;
					Button->SetStyle(Item);
					FindFProperty<FBoolProperty>(UButton::StaticClass(), TEXT("IsFocusable"))->SetPropertyValue_InContainer(Button, false);
					Button->SetContent(GameText(Tree, Style, *(Name + TEXT("Text")), Choice));
					UOverlaySlot* ButtonSlot = Cell->AddChildToOverlay(Button);
					ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
					ButtonSlot->SetVerticalAlignment(VAlign_Fill);

					USizeBox* CellBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(Name + TEXT("Box")));
					CellBox->SetWidthOverride(ChoiceWidth);
					CellBox->SetContent(Cell);
					Choices->AddChildToHorizontalBox(CellBox)->SetVerticalAlignment(VAlign_Fill);
				}
				return Choices;
			};

			const auto ValueOf = [&](int32 Index) -> UWidget*
			{
				if (const FDropdown* Dropdown = DropdownFor(Index)) return MakeBar(*Dropdown);
				return MakeChoices(Index);
			};

			// The rows after a line with dropdowns share their space with its lists, which open over them: each list
			// sits under its own bar, LeftInset from the column's left and Width wide.
			UVerticalBox* Into = Box;
			const auto OpenOver = [&](const FString& Name, const TArray<TTuple<const FDropdown*, float, float>>& Menus)
			{
				UOverlay* Area = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), *(Name + TEXT("Area")));
				UVerticalBox* Rows = Tree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), *(Name + TEXT("Rows")));
				UOverlaySlot* RowsSlot = Area->AddChildToOverlay(Rows);
				RowsSlot->SetHorizontalAlignment(HAlign_Fill);
				RowsSlot->SetVerticalAlignment(VAlign_Top);
				Into->AddChildToVerticalBox(Area)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				for (const auto& [Dropdown, LeftInset, Width] : Menus)
				{
					USizeBox* MenuBox = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *(FString(Dropdown->Prefix) + TEXT("MenuBox")));
					MenuBox->SetWidthOverride(Width);
					MenuBox->SetContent(MakeMenu(*Dropdown));
					UOverlaySlot* MenuSlot = Area->AddChildToOverlay(MenuBox);
					MenuSlot->SetHorizontalAlignment(HAlign_Left);
					MenuSlot->SetVerticalAlignment(VAlign_Top);
					MenuSlot->SetPadding(FMargin(LeftInset, 0.f, 0.f, 0.f));
				}
				Into = Rows;
			};

			// The paired options, two half rows a line.
			for (int32 First = 0; First + 1 < PairedOptions; First += 2)
			{
				UHorizontalBox* Line = Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), *FString::Printf(TEXT("OptionLine%d"), First / 2));
				TArray<TTuple<const FDropdown*, float, float>> Menus;
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const int32 Index = First + Side;
					UHorizontalBox* Row = MakeRow(*FString::Printf(TEXT("Option%dRow"), Index), OptionNames[Index], ValueOf(Index), HalfLabelWidth, FMargin(0.f));
					UHorizontalBoxSlot* RowSlot = Line->AddChildToHorizontalBox(Row);
					RowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					RowSlot->SetPadding(FMargin(Side == 0 ? 0.f : PairGap / 2.f, 0.f, Side == 0 ? PairGap / 2.f : 0.f, 0.f));
					if (const FDropdown* Dropdown = DropdownFor(Index))
					{
						Menus.Add(MakeTuple(Dropdown, Side * (HalfWidth + PairGap) + HalfLabelWidth + 2.f, HalfWidth - HalfLabelWidth - 2.f));
					}
				}
				Into->AddChildToVerticalBox(Line)->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
				if (!Menus.IsEmpty()) OpenOver(FString::Printf(TEXT("OptionLine%d"), First / 2), Menus);
			}

			// The other options, a centred row each.
			constexpr float CentredLeft = (SeedColumnWidth - CentredRowWidth) / 2.f;
			for (int32 Index = PairedOptions; Index < UE_ARRAY_COUNT(OptionNames); ++Index)
			{
				USizeBox* Width = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), *FString::Printf(TEXT("Option%dWidth"), Index));
				Width->SetWidthOverride(CentredRowWidth);
				Width->SetContent(MakeRow(*FString::Printf(TEXT("Option%dRow"), Index), OptionNames[Index], ValueOf(Index), LabelWidth, FMargin(0.f)));
				UVerticalBoxSlot* WidthSlot = Into->AddChildToVerticalBox(Width);
				WidthSlot->SetHorizontalAlignment(HAlign_Center);
				WidthSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
				if (const FDropdown* Dropdown = DropdownFor(Index))
				{
					OpenOver(Dropdown->Prefix, {MakeTuple(Dropdown, CentredLeft + LabelWidth + 2.f, CentredRowWidth - LabelWidth - 2.f)});
				}
			}
		}

		// The game's finger cursor, mirrored and turned so its tip is on the mouse. Not named Cursor:
		// UWidget has a Cursor property.
		UImage* Cursor = Tree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("FingerCursor"));
		Cursor->bIsVariable = true;
		Cursor->SetBrush(TextureBrush(FingerImage, FVector2D(CursorSize)));
		Cursor->SetVisibility(ESlateVisibility::HitTestInvisible);
		Cursor->SetRenderTransform(FWidgetTransform(FVector2D(-7.2f, -7.2f), FVector2D(-1.f, 1.f), FVector2D::ZeroVector, 45.f));
		UCanvasPanelSlot* CursorSlot = Root->AddChildToCanvas(Cursor);
		CursorSlot->SetAutoSize(false);
		CursorSlot->SetSize(FVector2D(CursorSize));
		CursorSlot->SetZOrder(100);

		const FName Seed(TEXT("SeedLabel")), Input(TEXT("SeedInput")), Button(TEXT("ApplyButton"));
		Tree->ForEachWidget([Blueprint](UWidget* Widget)
		{
			Blueprint->OnVariableAdded(Widget->GetFName());
		});

		const FName RequestedSeed(TEXT("RequestedSeed")), RequestedChoice(TEXT("RequestedChoice")), PrevCursor(TEXT("PrevCursor"));
		// PendingInput: the game's menu input for the DLL (see InputDirectionCode). MouseMode: set by the DLL
		// while the mouse is in use, so the finger follows it.
		const FName PendingInput(TEXT("PendingInput")), MouseMode(TEXT("MouseMode"));
		FEdGraphPinType StringType;
		StringType.PinCategory = UEdGraphSchema_K2::PC_String;
		FEdGraphPinType BoolType;
		BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, RequestedSeed, StringType)
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, RequestedChoice, IntPinType())
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, PrevCursor, BoolType)
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, PendingInput, StringType)
			|| !FBlueprintEditorUtils::AddMemberVariable(Blueprint, MouseMode, BoolType, TEXT("true")))
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

			const auto [CreateDimming, PlaceDimming] = CreateInto(Dimming, TEXT("DimHost"));
			Build.Then(Construct, CreateDimming);
			Build.Then(CreateDimming, PlaceDimming);

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
			Build.Then(PlaceDimming, CreateWindow);
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

		// A dropdown's bar opens and closes its list.
		for (const FDropdown& Dropdown : Dropdowns)
		{
			const FString Prefix(Dropdown.Prefix);
			const FName Select(*(Prefix + TEXT("Select")));
			const FName Menu(*(Prefix + TEXT("Menu")));
			UK2Node_ComponentBoundEvent* OnClicked = BoundEvent(Blueprint, Graph, Select, UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnClicked));
			UK2Node_CallFunction* Sound = Build.PlaySound(SoundCursor);
			Build.Then(OnClicked, Sound);
			UK2Node_CallFunction* IsOpen = Build.Call(UWidget::StaticClass(), TEXT("IsVisible"));
			Build.Target(Menu, IsOpen);
			UK2Node_IfThenElse* Branch = Build.Branch();
			Build.Link(IsOpen, Return, Branch, UEdGraphSchema_K2::PN_Condition);
			Build.Then(Sound, Branch);
			Build.Then(Branch, UEdGraphSchema_K2::PN_Then, Visibility(Menu, TEXT("Collapsed")));
			Build.Then(Branch, UEdGraphSchema_K2::PN_Else, Visibility(Menu, TEXT("Visible")));
			UK2Node_ComponentBoundEvent* OnHovered = BoundEvent(Blueprint, Graph, Select, UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnHovered));
			Build.Then(OnHovered, Build.PlaySound(SoundCursor));
		}

		// Choose(Kind, Index): RequestedChoice = Kind * 1000 + Index + 1, which the DLL reads and clears.
		{
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* Choose = Creator.CreateNode();
			Choose->CustomFunctionName = TEXT("Choose");
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
			UK2Node_VariableSet* SetRequest = Build.Set(RequestedChoice);
			Build.Link(PlusOne, Return, SetRequest, RequestedChoice);
			Build.Then(Choose, SetRequest);
			UEdGraphNode* Last = SetRequest;
			for (const FDropdown& Dropdown : Dropdowns)
			{
				UK2Node_CallFunction* Close = Visibility(*(FString(Dropdown.Prefix) + TEXT("Menu")), TEXT("Collapsed"));
				Build.Then(Last, Close);
				Last = Close;
			}
			Build.Then(Last, Build.PlaySound(SoundDecide));
		}

		// UpdateDisplay(SeedText): called by the DLL.
		{
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* Update = Creator.CreateNode();
			Update->CustomFunctionName = TEXT("UpdateDisplay");
			Creator.Finalize();
			Update->CreateUserDefinedPin(TEXT("SeedText"), StringType, EGPD_Output);

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
			Build.Then(Update, SetSeed);
			Build.Then(SetSeed, SetInput);
		}

		// OnRequestOpen: the game opens the screen. As its own screens do: the parent first, then FinishedOpen.
		// The mouse cursor's state is kept to give back on closing.
		{
			UK2Node_Event* Open = OverrideEvent(Graph, UCPP_EU_WBP_Activatable::StaticClass(), TEXT("OnRequestOpen"));
			FGraphNodeCreator<UK2Node_CallParentFunction> Creator(*Graph);
			UK2Node_CallParentFunction* Parent = Creator.CreateNode();
			Parent->SetFromFunction(UCPP_EU_WBP_Activatable::StaticClass()->FindFunctionByName(TEXT("OnRequestOpen")));
			Creator.Finalize();
			UK2Node_VariableGet* WasShown = Build.GetOn(APlayerController::StaticClass(), ShowCursor);
			Build.TargetPlayer(WasShown);
			UK2Node_VariableSet* Remember = Build.Set(PrevCursor);
			Build.Link(WasShown, ShowCursor, Remember, PrevCursor);
			UK2Node_CallFunction* Sound = Build.PlaySound(SoundOpen);
			UK2Node_CallFunction* Finished = Build.Call(UCPP_EU_WBP_Activatable::StaticClass(), TEXT("FinishedOpen"));
			Build.Then(Open, Parent);
			Build.Then(Parent, Remember);
			Build.Then(Remember, Sound);
			Build.Then(Sound, Finished);
		}

		// Closed, by the DLL or the game: the close sound, and the mouse cursor as it was.
		{
			UK2Node_Event* Closed = OverrideEvent(Graph, UCommonActivatableWidget::StaticClass(), TEXT("BP_OnDeactivated"));
			UK2Node_CallFunction* Sound = Build.PlaySound(SoundClose);
			UK2Node_VariableSet* CursorBack = Build.SetOn(APlayerController::StaticClass(), ShowCursor);
			Build.TargetPlayer(CursorBack);
			Build.Link(Build.Get(PrevCursor), PrevCursor, CursorBack, ShowCursor);
			Build.Then(Closed, Sound);
			Build.Then(Sound, CursorBack);
		}

		// The game's menu input, queued for the DLL: PendingInput += code + EventType + ",".
		{
			const auto Queue = [&](FName Function, int32 Code, bool bDirection)
			{
				UK2Node_Event* Input = OverrideEvent(Graph, UCPP_BI_WidgetBasicInput::StaticClass(), Function);
				UK2Node_CallFunction* Type = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Conv_ByteToInt"));
				Build.Link(Input, TEXT("InEventType"), Type, TEXT("InByte"));
				UK2Node_CallFunction* Sum = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Add_IntInt"));
				Build.Link(Type, Return, Sum, TEXT("A"));
				Build.Literal(Sum, TEXT("B"), FString::FromInt(Code));
				UK2Node_CallFunction* Total = Sum;
				if (bDirection)
				{
					UK2Node_CallFunction* Direction = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Conv_ByteToInt"));
					Build.Link(Input, TEXT("InNavigation"), Direction, TEXT("InByte"));
					UK2Node_CallFunction* Tens = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Multiply_IntInt"));
					Build.Link(Direction, Return, Tens, TEXT("A"));
					Build.Literal(Tens, TEXT("B"), TEXT("10"));
					Total = Build.Call(UKismetMathLibrary::StaticClass(), TEXT("Add_IntInt"));
					Build.Link(Sum, Return, Total, TEXT("A"));
					Build.Link(Tens, Return, Total, TEXT("B"));
				}
				UK2Node_CallFunction* AsText = Build.Call(UKismetStringLibrary::StaticClass(), TEXT("Conv_IntToString"));
				Build.Link(Total, Return, AsText, TEXT("InInt"));
				UK2Node_CallFunction* Append = Build.Call(UKismetStringLibrary::StaticClass(), TEXT("Concat_StrStr"));
				Build.Link(Build.Get(PendingInput), PendingInput, Append, TEXT("A"));
				Build.Link(AsText, Return, Append, TEXT("B"));
				UK2Node_CallFunction* Comma = Build.Call(UKismetStringLibrary::StaticClass(), TEXT("Concat_StrStr"));
				Build.Link(Append, Return, Comma, TEXT("A"));
				Build.Literal(Comma, TEXT("B"), TEXT(","));
				UK2Node_VariableSet* Store = Build.Set(PendingInput);
				Build.Link(Comma, Return, Store, PendingInput);
				Build.Then(Input, Store);
			};
			Queue(TEXT("InputDirection"), InputDirectionCode, true);
			Queue(TEXT("InputDecide"), InputDecideCode, false);
			Queue(TEXT("InputCancel"), InputCancelCode, false);
			Queue(TEXT("InputEscape"), InputEscapeCode, false);
		}

		// The game's menu sounds, for the DLL to play when the controller moves or chooses.
		for (const TPair<const TCHAR*, const TCHAR*>& Sound : {TPair<const TCHAR*, const TCHAR*>(TEXT("SoundCursor"), SoundCursor),
			TPair<const TCHAR*, const TCHAR*>(TEXT("SoundDecide"), SoundDecide)})
		{
			FGraphNodeCreator<UK2Node_CustomEvent> Creator(*Graph);
			UK2Node_CustomEvent* Event = Creator.CreateNode();
			Event->CustomFunctionName = Sound.Key;
			Creator.Finalize();
			Build.Then(Event, Build.PlaySound(Sound.Value));
		}

		// Tick, only while shown and the mouse is in use: the game hides the mouse cursor on its own, so keep
		// it on, and move the finger to the mouse.
		{
			UK2Node_Event* Tick = OverrideEvent(Graph, UUserWidget::StaticClass(), TEXT("Tick"));
			UK2Node_IfThenElse* UsingMouse = Build.Branch();
			Build.Link(Build.Get(MouseMode), MouseMode, UsingMouse, UEdGraphSchema_K2::PN_Condition);
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
			Build.Then(Tick, UsingMouse);
			Build.Then(UsingMouse, UEdGraphSchema_K2::PN_Then, CursorOn);
			Build.Then(CursorOn, Mouse);
			Build.Then(Mouse, Move);
		}

		// Choose exists on the class only after this.
		FKismetEditorUtilities::CompileBlueprint(Blueprint);

		const auto ChooseOnClick = [&](const FString& Button, int32 Kind, int32 Index)
		{
			UK2Node_ComponentBoundEvent* OnClicked = BoundEvent(Blueprint, Graph, *Button, UButton::StaticClass(), GET_MEMBER_NAME_CHECKED(UButton, OnClicked));
			UK2Node_CallFunction* Choose = Build.Call(Blueprint->SkeletonGeneratedClass, TEXT("Choose"));
			Build.Literal(Choose, TEXT("Kind"), FString::FromInt(Kind));
			Build.Literal(Choose, TEXT("Index"), FString::FromInt(Index));
			Build.Then(OnClicked, Choose);
		};
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(OptionNames); ++Index)
		{
			if (const FDropdown* Dropdown = DropdownFor(Index))
			{
				for (int32 Choice = 0; Choice < Dropdown->Choices.Num(); ++Choice)
				{
					ChooseOnClick(FString::Printf(TEXT("%sItem%d"), Dropdown->Prefix, Choice), Dropdown->Kind, Choice);
				}
				continue;
			}
			ChooseOnClick(FString::Printf(TEXT("OptionOn%dButton"), Index), OptionOnKind, Index);
			ChooseOnClick(FString::Printf(TEXT("OptionOff%dButton"), Index), OptionOffKind, Index);
		}

		// Focusable, so the seed box can take the keyboard; and the game's prompts for the panel.
		UObject* Defaults = Blueprint->GeneratedClass->GetDefaultObject();
		CastChecked<UUserWidget>(Defaults)->SetIsFocusable(true);
		FStructProperty* Guide = FindFProperty<FStructProperty>(Blueprint->GeneratedClass, TEXT("ActionGuideTag"));
		if (!Guide || Guide->Struct != FGameplayTag::StaticStruct())
		{
			throw FBuildError{TEXT("the panel has no ActionGuideTag")};
		}
		// A tag the editor's tag list does not have: its name is set as it is, for the game to look up.
		*Guide->ContainerPtrToValuePtr<FName>(Defaults) = FName(ActionGuideTag);

		CompileAndSave(Blueprint);
		return Blueprint;
	}
}

int32 UBuildFFRRandomizerUICommandlet::Main(const FString& Params)
{
	try
	{
		BuildPanel();
	}
	catch (const FBuildError& Error)
	{
		UE_LOG(LogFFRRandomizerUI, Error, TEXT("%s"), *Error.Message);
		return 1;
	}
	return 0;
}
