#pragma once

#include "Helpers.h"
#include "ScalingModel.h"

namespace ffr
{
    class FFRRandomizer : public CppUserModBase
    {
    public:
        FFRRandomizer();
        auto on_unreal_init() -> void override;

    private:
        struct FTable
        {
            FWeakObjectPtr object{};
            std::vector<int32> originals{};
            uint32_t appliedSeed{};
            bool applied{};
        };

        // An encounter actor's box in the world and the table it reads.
        struct FEncounterArea
        {
            StringType table;
            double origin[3]{};
            double extent[3]{};
        };

        uint32_t seed{};
        bool hasSeed{};
        bool hasPending{};
        uint32_t pendingSeed{};

        std::unordered_map<StringType, FTable> tables{};
        bool loadedAllTables{};
        std::unordered_map<int32, std::vector<int32>> groupUnits{};
        // Each boss and Colosseum group's row, the offsets of its enemy fields, and whose enemies each now holds.
        std::unordered_map<int32, uint8*> poolRows{};
        std::vector<int32> enemyFieldOffsets{};
        std::vector<FProperty*> enemyFieldProperties{};
        // Colosseum fights and their field copies, if copies can't be made stay empty.
        std::unordered_map<int32, uint8*> colosseumRows{};
        std::unordered_map<int32, uint8*> colosseumOriginals{};
        // the reason we have to create field copies of them is because normal colosseum fight don't give xp, gil, ap, even show an end battle screen, or even can 'game over you'
        // these field copies
        std::unordered_map<int32, int32> fieldCopies{};

        std::unordered_map<int32, uint8*> groupRows{};
        std::unordered_map<int32, int32> enemiesIn{};

        std::unordered_map<int32, FUnitStats> unitStats{};
        std::unordered_set<int32> bossUnits{};

        std::unordered_map<int32, std::pair<int32, int32*>> groupAp{};

        // see prepareZone
        struct FEscape
        {
            bool allowed{};
            float odds{};
            uint8* allowedByte{};
            uint8 allowedMask{};
            float* oddsValue{};
        };

        std::unordered_map<int32, FEscape> groupEscape{};

        // The zone, seed and options the groups' AP and escape rules were last set for.
        StringType preparedZone{};
        uint32_t preparedSeed{};
        uint32_t preparedOptions{};

        std::vector<FEncounterArea> areas{};
        FWeakObjectPtr areaActor{};
        // Encounter tables by distance from where the player last walked, nearest first; kept through battles.
        std::vector<StringType> nearbyTables{};
        float sincePlace{};
        double lastPawnZ{};
        bool hasPawnZ{};
        float parked{};

        // Battle stat objects the game made (filled in on a later frame), and those waiting to be scaled.
        std::mutex madeMutex{};
        std::vector<FWeakObjectPtr> madeParameters{};
        std::vector<FWeakObjectPtr> pendingParameters{};
        float pendingSeconds{};
        std::vector<FWeakObjectPtr> lastBattle{};
        int32 lastBattleGroup{};

        // Item, shop and Vision shuffles. A write goes to every table of a composite family.
        enum TableFamily { chests, units, shops, events, familyCount };
        // One int32 in a row: row + offset, or element index of the array at arrayOffset, then offset.
        struct FField
        {
            TableFamily family{};
            StringType row{};
            int32 offset{};
            int32 arrayOffset{-1};
            int32 stride{};
            int32 index{};
        };
        struct FSlot
        {
            FField field{};
            int32 original{};
        };
        // What a chest holds: an item and how many, or gil.
        struct FChest
        {
            FField item{};
            FField count{};
            FField money{};
            int32 itemId{};
            int32 itemCount{};
            int32 gil{};
        };
        std::vector<FWeakObjectPtr> families[familyCount]{};
        std::unordered_map<int32, ItemKind> itemKinds{};
        std::unordered_set<int32> questItems{};
        std::vector<FChest> chestRewards{};
        std::vector<FSlot> enemyItems{};
        std::vector<bool> enemyItemIsSteal{};
        // Every shop slot that can move, with the shop it is in, and what each shop sells that stays.
        std::vector<std::pair<FSlot, size_t>> shopStock{};
        std::vector<std::unordered_set<int32>> shopFixed{};
        std::vector<FSlot> visionSlots{};
        std::vector<FSlot> visionDrafts{};
        // The game's own flags and lists in the rows the shrine swap writes, read before any write.
        struct FShrineRow
        {
            std::vector<int32> onFlags{};
            std::vector<int32> offFlags{};
            std::vector<int32> clearedMaps{};
        };
        std::unordered_map<StringType, FShrineRow> shrineRows{};
        std::unordered_map<StringType, std::vector<StringType>> shrineConditions{};
        std::unordered_map<StringType, StringType> shrineRetries{};
        // Each map's battle stage as the game has it, by map id, and the map tables they are in.
        std::unordered_map<int32, int32> battleStages{};
        std::vector<FWeakObjectPtr> mapTables{};
        std::vector<FWeakObjectPtr> stageTables{};
        bool shrineStageAdded{};
        std::vector<int32> swappableLegends{};
        // The Legend whose shrine Sephiroth's event is in now (0: none), and each trigger's events as the game had them.
        int32 swappedLegend{};
        std::vector<std::pair<FWeakObjectPtr, std::vector<FName>>> triggerEvents{};
        float triggerScanLeft{};
        float sinceTriggerScan{};
        bool itemsApplied{};
        uint32_t itemsSeed{};
        uint32_t itemsOff{};

        // One bit per Option that is switched off.
        uint32_t optionsOff{};
        bool battlesShuffled{true};

        FWeakObjectPtr saveManager{};
        FWeakObjectPtr panel{};
        std::atomic<bool> togglePanel{};
        std::atomic<bool> dumpRequested{};
        // Whether the panel was opened by its layout row, so it is closed by its tag.
        bool panelByLayout{};
        bool layoutRowAdded[std::size(layoutTables)]{};
        // The controller: the row it is on, whether the mouse has the panel instead, and when each input last moved.
        float panelClock{};
        int32 padRow{};
        bool mouseMode{true};
        bool hasMouse{};
        double lastMouse[2]{};
        std::unordered_map<int32, float> lastInputAt{};
        // The first inputs are logged, to see which event types the game sends (Pressed, Triggered or both).
        int32 inputsLogged{};
        // What the shown panel draws: the finger's row (-1 for the mouse's finger), -2 when not drawn yet.
        FWeakObjectPtr fingersFor{};
        int32 fingerShown{-2};
        float sinceCheck{};
        float sinceSearch{searchSeconds};
        float sincePanelSearch{searchSeconds};
        bool displayDirty{true};
        bool seedRejected{};
        bool loggedConflict{};

        // The event rows' lists the shrine swap uses.
        struct FEventLists
        {
            FArrayProperty* onFlags{};
            FArrayProperty* offFlags{};
            FArrayProperty* clearedMaps{};
            FArrayProperty* continuous{};
            int32 stride{};
            FProperty* condition{};
            FProperty* eventId{};
        };

        // Debug.cpp
        auto dumpMapEvents() -> void;
        static auto describeObject(UObject* object, const StringType& prefix, int32 depth, std::vector<StringType>& lines) -> void;
        static auto describeValue(UObject* owner, FProperty* property, const uint8* value, const StringType& path, int32 depth, std::vector<StringType>& lines) -> void;

        // Panel.cpp
        auto tickPanel(float deltaSeconds) -> void;
        auto choose(int32 kind, size_t chosen) -> void;
        auto addLayoutRows() -> void;
        auto writePanelClass(uint8* soft) -> void;
        auto loadPanelClass() -> UObject*;
        static auto isShown(UObject* widget) -> bool;
        auto openPanel() -> void;
        auto closePanel(UObject* widget) -> void;
        auto tickController(UObject* widget) -> void;
        auto controllerInput(UObject* widget, int32 code) -> void;
        auto stepRow(int32 step, bool wrap) -> bool;
        auto closeMenus(UObject* widget) -> bool;
        auto findPanel() -> UObject*;
        auto updatePanel() -> void;

        // Save.cpp
        auto isOn(Option option) const -> bool;
        auto lootMode() const -> LootMode;
        auto isAvailable(Option option) const -> bool;
        auto scalingChoice(Option option = scalingOption) const -> size_t;
        auto lootChoice() const -> size_t;
        auto setOption(Option option, bool on) -> void;
        auto setLootChoice(size_t choice) -> void;
        auto setScalingChoice(size_t choice, Option option = scalingOption) -> void;
        auto storeOptions() -> void;
        auto reconcile() -> void;
        auto saveFlags() -> uint8*;

        // Encounters.cpp: player and encounter actors
        auto liveEncounters() -> std::vector<std::pair<UObject*, UDataTable*>>;
        auto patchLiveEncounters() -> void;
        auto updateAreas() -> void;
        auto playerLocation(double out[3]) -> bool;
        auto updatePlace(float elapsed) -> void;

        // Encounters.cpp: every battle
        auto loadAllTables() -> void;
        auto readBattleGroups() -> void;
        auto readGroupRow(UScriptStruct* row, uint8* data) -> void;
        auto lineupOf(int32 group) const -> std::vector<int32>;
        auto waveOf(const std::vector<UObject*>& enemies) const -> int32;
        auto expectedEnemies(int32 group, const std::vector<UObject*>& enemies) -> size_t;
        auto enemiesOf(int32 group) const -> int32;
        auto scaleMovedEnemies(int32 group, const std::vector<UObject*>& enemies) -> void;

        // Encounters.cpp: normal encounters
        auto applyEncounterTables() -> bool;
        auto originalGroup(int32 group) -> int32;
        auto setBattleEscape(int32 group, const std::vector<int32>& originals) -> void;
        auto setBattleAp(int32 group, int32 replaced, double strength) -> void;
        auto prepareZone() -> void;

        // Encounters.cpp: bosses
        auto shuffledBosses() -> std::unordered_map<int32, int32>;
        auto applyBosses() -> void;

        // Encounters.cpp: Colosseum
        auto makeFieldCopies(UDataTable* dataTable) -> void;
        auto applyColosseum(const std::vector<int32>& shuffled) -> void;

        // Battles.cpp
        auto readUnitStats() -> void;
        auto groupFighters(int32 group) -> std::vector<FFighter>;
        auto battleGroup() -> int32;
        static auto field(UObject* parameter, int32 offset) -> int32&;
        auto scaleNewEnemies(float deltaSeconds) -> void;
        static auto writeFighter(UObject* parameter, const FFighter& enemy, bool keepCurrent = false) -> void;
        auto scaleEnemies(int32 group, const std::vector<UObject*>& enemies) -> void;

        // Items.cpp
        auto tableFamily(UDataTable* root) -> std::vector<FWeakObjectPtr>;
        auto loadFamily(TableFamily family, const CharType* path) -> UDataTable*;
        auto canMove(int32 item) const -> bool;
        auto loadItemTables() -> void;
        auto itemWrites() -> std::vector<std::pair<const FField*, int32>>;
        auto applyItems() -> void;

        // ShrineSwap.cpp
        auto readShrineTables(UDataTable* eventRows) -> void;
        static auto eventLists(UScriptStruct* row) -> FEventLists;
        static auto readInts(const uint8* row, FArrayProperty* array) -> std::vector<int32>;
        static auto writeInts(uint8* row, FArrayProperty* array, const std::vector<int32>& values) -> void;
        auto readShrineRows(UDataTable* dataTable) -> void;
        auto applyShrineSwap() -> void;
        auto addShrineStage() -> void;
        static auto isSettledActor(UObject* object) -> bool;
        static auto sensibleArray(const FScriptArray* entries) -> bool;
        auto setShrineTriggers() -> void;

        // dllmain.cpp
        auto tick(float deltaSeconds) -> void;

        // A dropdown's bar shows the choice; its list marks it.
        template<size_t count>
        static auto showDropdown(UObject* widget, const CharType* prefix, const CharType* const (&choices)[count], size_t current) -> void
        {
            if (auto* text = widget->GetValuePtrByPropertyNameInChain<UObject*>(std::format(STR("{}SelectText"), prefix).c_str()); text && *text)
            {
                setWidgetText(*text, choices[current]);
            }
            for (size_t choice = 0; choice < count; ++choice)
            {
                auto* entry = widget->GetValuePtrByPropertyNameInChain<UObject*>(std::format(STR("{}ItemText{}"), prefix, choice).c_str());
                if (entry && *entry) setWidgetText(*entry, (choice == current ? StringType(shownMark) : StringType(STR("    "))) + choices[choice]);
            }
        }
    };
}
