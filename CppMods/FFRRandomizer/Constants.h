#pragma once

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <format>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/Engine/UDataTable.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/AActor.hpp>
#include <Unreal/Property/FArrayProperty.hpp>
#include <Unreal/Property/FStructProperty.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "BattleGroupPool.h"
#include "EncountTables.h"
#include "ScalingData.h"

using namespace RC;
using namespace RC::Unreal;

namespace ffr
{
    constexpr float checkSeconds = 1.0f;
    constexpr float searchSeconds = 2.0f;

    constexpr const CharType* battleGroupTable = STR("/Game/Datatable/Encount/DT_BattleGroup.DT_BattleGroup");

    // Actors that copy their table's rows when they spawn
    constexpr const CharType* encountActorClasses[] = {
        STR("CPP_MapEncountPile"), STR("CPP_MapEncountBox"), STR("CPP_MapEncountSphere"),
        STR("BP_MapEncountBox_C"), STR("BP_MapEncountSphere_C"),
    };

    // The seed and options are kept in the save, in the last 80 event flags (1968+), which the game never uses.
    constexpr int32 tagFlag = 1984;
    constexpr int32 seedFlag = 2016;
    constexpr int32 flagCount = 2048;
    // Guard against edgecase like differentiating between a seed being rolled as full 0 and an 'not generated yet seed' (aka game start)
    // Also might protect against future game update but I have no hope it won't break most of everything if/when that happen
    constexpr uint32_t tag = 0x52414E44; // "RAND"

    // Panel's options, also kept in the save as (presumably) always empty bits (optionBit). 
    // Bits 6-7: drops and steals
    // mode; 8 and 11: Replaced stats (normal, boss). Bits 0-7 are flags 1976+, 8-15 flags 1968+.
    enum Option { battlesOption, bossesOption, scalingOption, bossScalingOption, lootOption, chestsOption, shopsOption, visionsOption, optionCount };
    constexpr const CharType* optionNames[] = {STR("Normal battles"), STR("Boss battles"), STR("Normal scaling"), STR("Boss scaling"),
                                               STR("Enemy drops and steals"), STR("Chests"), STR("Shops"), STR("Visions")};
    constexpr uint32_t optionBit[] = {0, 9, 1, 10, 3, 2, 4, 5};
    static_assert(std::size(optionBit) == optionCount && std::size(optionNames) == optionCount);

    constexpr uint32_t optionMask(int32 option)
    {
        return 1u << optionBit[option];
    }

    // A scaling row only means something while its battles are randomized: the panel grays it out otherwise.
    constexpr int32 scaledBattlesOf(int32 option)
    {
        return option == scalingOption ? battlesOption : option == bossScalingOption ? bossesOption : -1;
    }
    constexpr int32 optionFlag = 1976;
    constexpr int32 moreOptionsFlag = 1968;
    constexpr int32 optionBits = 16;

    inline int32 optionFlagOf(int32 bit)
    {
        return bit < 8 ? optionFlag + bit : moreOptionsFlag + bit - 8;
    }

    // Whether every option flag is clear, as in a save the randomizer has not claimed yet.
    inline bool optionFlagsClear(const uint8* flags)
    {
        for (int32 bit = 0; bit < optionBits; ++bit)
        {
            if (flags[optionFlagOf(bit)] != 0) return false;
        }
        return true;
    }

    constexpr const CharType* scalingChoices[] = {STR("Off"), STR("Replaced stats"), STR("Balanced")};
    constexpr uint32_t copyStatsBit = 8;
    constexpr uint32_t bossCopyStatsBit = 11;

    // A scaling row's Replaced stats bit.
    constexpr uint32_t copyBitOf(int32 option)
    {
        return option == bossScalingOption ? bossCopyStatsBit : copyStatsBit;
    }
    enum LootMode { lootMixed, lootBoth, lootDrops, lootSteals };
    constexpr const CharType* lootChoices[] = {STR("Off"), STR("Drops"), STR("Steals"), STR("Both"), STR("Mixed")};
    constexpr LootMode lootModeOf[] = {lootMixed, lootDrops, lootSteals, lootBoth, lootMixed};
    constexpr uint32_t lootModeShift = 6;

    // Choose's kinds for an option's On and Off, and for an entry of each dropdown (with the entry's index).
    constexpr int32 optionOnKind = 3;
    constexpr int32 optionOffKind = 4;
    constexpr int32 lootKind = 5;
    constexpr int32 scalingKind = 6;
    constexpr int32 bossScalingKind = 7;

    constexpr StringViewType shownMark = STR("▶ ");

    // The panel opens as a game screen, by a layout row copied from the Help screen's.
    constexpr const CharType* layoutTables[] = {
        STR("/Game/Datatable/UI/GameLayout/CDT_GameLayoutWidget.CDT_GameLayoutWidget"),
        STR("/Game/Datatable/UI/GameLayout/DT_GameLayoutWidget_System.DT_GameLayoutWidget_System"),
    };
    constexpr const CharType* panelRowName = STR("System_FFRRandomizer");
    constexpr const CharType* panelLayoutTag = STR("UI.Layout.FFRRandomizer");
    constexpr const CharType* templateLayoutTag = STR("UI.Layout.System.Help");
    constexpr const CharType* templateClassName = STR("WBP_HelpLayout_C");
    constexpr const CharType* panelPackage = STR("/Game/Mods/FFRRandomizer/WBP_SeedPanel");
    constexpr const CharType* panelClassName = STR("WBP_SeedPanel_C");
    constexpr const CharType* panelClassPath = STR("/Game/Mods/FFRRandomizer/WBP_SeedPanel.WBP_SeedPanel_C");
    // Where the panel goes if the game will not open it by its row: the Help screen's layer.
    constexpr const CharType* panelLayer = STR("UI.Layer.System.Front1");
    constexpr const CharType* widgetManager = STR("/Script/mucha.Default__CPP_BFL_GameWidgetManager");
    // A soft class reference: a weak pointer, the package's and the asset's names, and a sub-path.
    constexpr int32 softClassSize = sizeof(FWeakObjectPtr) + 2 * sizeof(FName) + sizeof(FString);

    // The game's menu input, as the panel queues it in PendingInput: group * 100 + direction * 10 + event type.
    enum PanelInput { inputDirection = 1, inputDecide, inputCancel, inputEscape };
    enum Navigation { navLeft, navRight, navUp, navDown };
    // EWidgetInputEventType: Triggered and Pressed move; Released and the rest do not.
    constexpr int32 inputReleased = 2;
    // One press can come as both Pressed and Triggered: one move per this long for each input.
    constexpr float inputRepeatSeconds = 0.12f;
    // The rows the controller moves between: the options.
    constexpr int32 padRows = optionCount;
    // The lists the mouse opens; back closes them before the panel.
    constexpr const CharType* panelMenus[] = {STR("ScalingMenu"), STR("LootMenu")};

    // ESlateVisibility.
    constexpr uint8 shownVisibility = 0;
    constexpr uint8 collapsedVisibility = 1;
    constexpr uint8 passThroughVisibility = 3;
    // How much of a grayed-out row shows.
    constexpr float grayedOpacity = 0.4f;

    // How often the player's place among the encounter areas is noted.
    constexpr float placeSeconds = 0.25f;
    // A fall this far below the last place is the battle parking the field pawn under the map.
    constexpr double battleDrop = 250.0;
    // Past this, a fall is taken as a real move (a new map, a long drop).
    constexpr float parkedSeconds = 10.0f;
    // Scale with the enemies made so far if the whole group has not appeared by then.
    constexpr float battleWaitSeconds = 1.0f;

    // Where UCPP_BtlUnitParameter keeps its copy of the unit's FUNIT_PARAMETER row.
    constexpr int32 parameterSize = 1800;
    constexpr int32 unitIdOffset = 248;
    constexpr int32 levelOffset = 256;
    constexpr int32 hitPointOffset = 268;
    constexpr int32 breakPointOffset = 276;
    constexpr int32 maxHitPointOffset = 288;
    constexpr int32 maxBreakPointOffset = 296;
    constexpr int32 attackOffset = 300;
    constexpr int32 defenceOffset = 304;
    constexpr int32 intelligenceOffset = 308;
    constexpr int32 mindOffset = 312;
    constexpr int32 agilityOffset = 320;
    constexpr int32 experienceOffset = 540;
    constexpr int32 moneyOffset = 544;
    constexpr int64_t enemyRelation = 2;

    // A fight's strength vs the one it replaces lands in this range.
    constexpr double weakestFight = 0.75;
    constexpr double strongestFight = 1.75;
    // Each monster keeps its own strength for its level, within these times a typical one.
    constexpr double weakestMonster = 1.0;
    constexpr double strongestMonster = 2.0;
    // No combat stat ends further than this factor from a typical monster's at that level.
    constexpr double statSpread = 1.5;
    // Same for HP, wider so a lone monster replacing a crowd can still last.
    constexpr double hitPointSpread = 5.0;
    // The same for bosses, next to the typical boss (BossStatCurve).
    constexpr double weakestBoss = 0.75;
    constexpr double strongestBoss = 2.5;
    constexpr double bossStatSpread = 2.0;
    constexpr double bossHitPointSpread = 6.0;

    // Tables the item shuffles move rewards around in.
    constexpr const CharType* chestTable = STR("/Game/Datatable/Map/Gimmick/TreasureBox/CDT_MapGimmickTreasureBox.CDT_MapGimmickTreasureBox");
    constexpr const CharType* unitTable = STR("/Game/Datatable/Unit/DT_UnitParameter.DT_UnitParameter");
    constexpr const CharType* shopTable = STR("/Game/Datatable/Shop/DT_ShopList.DT_ShopList");
    constexpr const CharType* eventTable = STR("/Game/Datatable/Event/TalkEvent/CDT_TalkEventData_Prod.CDT_TalkEventData_Prod");
    // Maps' settings: a fight needs its map to name a battle stage, and only Sephiroth's shrine does.
    constexpr const CharType* mapDataTable = STR("/Game/Datatable/Map/CDT_MapData_Prod.CDT_MapData_Prod");
    constexpr int32 sephirothShrine = 10218;
    // His stage (109) is black outside his shrine, so the host shrine gets a copy of the Otherworld stage (71)
    // that loads its levels as instances, as stage 39 does.
    constexpr const CharType* battleStageTable = STR("/Game/Datatable/Asset/Battle/Stage/CDT_BtlStageAsset_Prod.CDT_BtlStageAsset_Prod");
    constexpr int32 otherworldStage = 71;
    constexpr int32 shrineStage = 1071;
    constexpr const CharType* shrineStageRow = STR("FFR_ShrineOtherworld");
    constexpr const CharType* questTable = STR("/Game/Datatable/Quest/CDT_QuestBoardData.CDT_QuestBoardData");


    //seed looks to be unused (only 2 entry and not translated or not doing anything)
    //artifacts might be post release content, or some of them (particularly Cursed Ring/Runic Bracer) are items that were meant for different system with mention of mp orbs dropping and chain bonus (FFBE didn't have MP orb iirc, probably tried to have FFR have chain)
    //rest is self explanatory
    enum class ItemKind { consumable, equipment, seed, key, artifact, vision };
    constexpr std::pair<ItemKind, const CharType*> itemTables[] = {
        {ItemKind::consumable, STR("/Game/Datatable/Item/DT_ConsumablesItemData.DT_ConsumablesItemData")},
        {ItemKind::equipment, STR("/Game/Datatable/Item/DT_EquipmentItemData.DT_EquipmentItemData")},
        {ItemKind::seed, STR("/Game/Datatable/Item/DT_SeedItemData.DT_SeedItemData")},
        {ItemKind::key, STR("/Game/Datatable/Item/DT_ValuableItemData.DT_ValuableItemData")},
        {ItemKind::artifact, STR("/Game/Datatable/Item/DT_ArtifactItemData.DT_ArtifactItemData")},
        {ItemKind::vision, STR("/Game/Datatable/Item/Vision/DT_VisionItemData.DT_VisionItemData")},
    };

    // Add type to the shuffle, for now only kept to consumable/equipment as they can move to replace each other in shop or chest
    // Visions are swapped between each other
    // Key items are not swapped
    // Seed/Artifacts not present in game files
    inline bool canShuffle(ItemKind kind)
    {
        return kind == ItemKind::consumable || kind == ItemKind::equipment;
    }

    // eUnitCategory: enemies and bosses carry drops and steals; playable, Vision and beast units keep theirs.
    constexpr int64_t enemyCategory = 2;
    constexpr int64_t bossCategory = 3;
    constexpr const CharType* unitItemFields[] = {STR("DropNormalItem"), STR("DropRareItem"), STR("StealNormalItem"), STR("StealRareItem")};
    constexpr size_t visionCount = 26;

    // Each shuffle uses its own stream of the seed.
    constexpr uint32_t chestStream = 0x43485354;  // "CHST"
    constexpr uint32_t lootStream = 0x4C4F4F54;  // "LOOT"
    constexpr uint32_t shopStream = 0x53484F50;   // "SHOP"
    constexpr uint32_t visionStream = 0x5649534E; // "VISN"
    constexpr uint32_t bossStream = 0x424F5353; // "BOSS"
    // Colosseum fights placed in encounter rows are fought as field copies, under this id plus their own.
    constexpr int32 fieldCopyOffset = 10000;
    constexpr const CharType* fieldCopyRow = STR("FFR_Field_{}");
    // The Colosseum rules a field copy drops: what follows a win, an escape and a loss, and the skipped result screens.
    constexpr const CharType* colosseumEvents[] = {STR("battleFinishEventId"), STR("battleFinishEscapeEventId"), STR("battleFinishLoseEventId")};
    constexpr const CharType* colosseumSkips[] = {STR("isResultSkipOnWin"), STR("isResultSkipOnLose")};

    // A battle group's enemy fields, which the boss shuffle moves; the rest stays with the fight.
    constexpr const CharType* enemyFields[] = {STR("UnitIdList"), STR("locationIdList"), STR("addPassiveDataList"), STR("bgmCueName")};
    constexpr uint32_t shrineStream = 0x53485249; // "SHRI"

    // Shrine swap
    constexpr bool shrineSwapEnabled = false;

    // Sephiroth's event and one Legend's trade shrines; each shrine keeps its own flag and cleared map.
    // Legend 01 is left out: its reward leads into a tutorial.
    constexpr const CharType* sephirothStart = STR("Str_LegendEV_07_Boss_01");
    constexpr const CharType* sephirothEnd = STR("Str_LegendEV_07_Boss_05");
    constexpr const CharType* legendRetryCancel = STR("Int_LegendEV_Retry_Cancel");
    constexpr int32 sephirothFlag = 1018;
    constexpr int32 firstSwappedLegend = 2;
    constexpr int32 lastSwappedLegend = 16;
    // A shrine's trigger is set up once its map has loaded; it is looked for this long after a map loads.
    constexpr float triggerScanSeconds = 15.0f;
    constexpr float triggerScanStep = 0.5f;
    // The first look waits this long after a map loads, for its actors to finish loading.
    constexpr float triggerScanDelay = 1.0f;

    inline StringType legendRow(int32 legend, const CharType* part)
    {
        return std::format(STR("Int_LegendEV_{:02}_{}"), legend, part);
    }
}
