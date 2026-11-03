#include <algorithm>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/Engine/UDataTable.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "BattleGroupPool.h"
#include "EncountTables.h"
#include "ZoneNames.h"

using namespace RC;
using namespace RC::Unreal;

namespace
{
    constexpr float CheckSeconds = 1.0f;
    constexpr float SearchSeconds = 2.0f;

    constexpr const CharType* BattleGroupTable = STR("/Game/Datatable/Encount/DT_BattleGroup.DT_BattleGroup");
    constexpr const CharType* MapNameTable = STR("/Game/Datatable/Strings/Map/ST_MAP_NAME.ST_MAP_NAME");
    constexpr const CharType* UnitNameTable = STR("/Game/Datatable/Strings/ST_Unit/ST_UnitName.ST_UnitName");

    // Actors that copy their table's rows when they spawn.
    constexpr const CharType* EncountActorClasses[] = {
        STR("CPP_MapEncountPile"), STR("CPP_MapEncountBox"), STR("CPP_MapEncountSphere"),
        STR("BP_MapEncountBox_C"), STR("BP_MapEncountSphere_C"),
    };

    // The seed is kept in the save, in the last 64 event flags, which the game never uses.
    constexpr int32 TagFlag = 1984;
    constexpr int32 SeedFlag = 2016;
    constexpr int32 FlagCount = 2048;
    // Guard against edgecase like differentiating between a seed being rolled as full 0 and an 'not generated yet seed' (aka game start)
    // Also might protect against future game update but I have no hope it won't break most of everything if/when that happen
    constexpr uint32_t Tag = 0x52414E44; // "RAND"

    constexpr StringViewType ShownMark = STR("▶ ");

    struct FUpdateDisplayParams
    {
        FString SeedText;
        FString DungeonName;
        FString FieldName;
        FString ZoneText;
    };

    // Bosses, bounties and fights that cannot be escaped are not in the pool.
    bool IsNormalGroup(int32 Group)
    {
        return std::find(std::begin(BattleGroupPool), std::end(BattleGroupPool), Group) != std::end(BattleGroupPool);
    }

    bool IsField(StringViewType Table)
    {
        return Table.find(STR("_Wld_")) != StringViewType::npos || Table.find(STR("_Bounty_")) != StringViewType::npos;
    }

    // FNV-1a 32 bits hashing, mostly used to guarantee randomisation though prolly will be removed or commented out later on
    uint32_t FNVHash(StringViewType Name)
    {
        // FNV offset and prime for 32 bits
        uint32_t hash = 2166136261u;
        uint32_t prime = 16777619u;
        for (const auto Char : Name)
        {
            hash = (hash ^ static_cast<uint32_t>(Char)) * prime;
        }
        return hash;
    }

    std::vector<int32> GroupsForSeed(uint32_t Seed, StringViewType Table, const std::vector<int32>& Originals)
    {
        std::mt19937 Rng{Seed ^ FNVHash(Table)};
        std::vector<int32> Groups(Originals.size());
        for (size_t Index = 0; Index < Originals.size(); ++Index)
        {
            const int32 Drawn = BattleGroupPool[Rng() % std::size(BattleGroupPool)];
            Groups[Index] = IsNormalGroup(Originals[Index]) ? Drawn : Originals[Index];
        }
        return Groups;
    }

    bool TextToSeed(StringViewType Wide, uint32_t& Out)
    {
        std::string Text;
        for (const auto Char : Wide)
        {
            if (Char > 127) return false;
            Text.push_back(static_cast<char>(Char));
        }
        const auto First = Text.find_first_not_of(" \t\r\n");
        if (First == std::string::npos) return false;
        const std::string_view Trimmed = std::string_view(Text).substr(First, Text.find_last_not_of(" \t\r\n") - First + 1);
        if (Trimmed.size() > 8) return false;
        const auto [End, Error] = std::from_chars(Trimmed.data(), Trimmed.data() + Trimmed.size(), Out, 16);
        return Error == std::errc{} && End == Trimmed.data() + Trimmed.size();
    }

    uint32_t ReadBits(const uint8* Flags, int32 First)
    {
        uint32_t Value{};
        for (int32 Bit = 0; Bit < 32; ++Bit)
        {
            if (Flags[First + Bit]) Value |= 1u << Bit;
        }
        return Value;
    }

    void WriteBits(uint8* Flags, int32 First, uint32_t Value)
    {
        for (int32 Bit = 0; Bit < 32; ++Bit)
        {
            Flags[First + Bit] = (Value >> Bit) & 1u;
        }
    }

    template<typename FillType>
    void CallByName(UObject* Target, const CharType* Name, FillType&& Fill)
    {
        UFunction* Function = Target ? Target->GetFunctionByNameInChain(Name) : nullptr;
        if (!Function) return;
        std::vector<uint8> Params(std::max<int32>(Function->GetPropertiesSize(), 16));
        for (FProperty* Param : TFieldRange<FProperty>(Function, EFieldIterationFlags::IncludeDeprecated))
        {
            Fill(Param->GetName(), Params.data() + Param->GetOffset_Internal());
        }
        Target->ProcessEvent(Function, Params.data());
    }

    std::vector<uint8> CallKismet(UObject* Library, const CharType* Name, const std::vector<uint8>& Input)
    {
        UFunction* Function = Library->GetFunctionByNameInChain(Name);
        if (!Function) return {};
        std::vector<uint8> Params(std::max<int32>(Function->GetPropertiesSize(), 16));
        FProperty* Return = Function->GetReturnProperty();
        for (FProperty* Param : TFieldRange<FProperty>(Function, EFieldIterationFlags::IncludeDeprecated))
        {
            if (Param == Return || Param->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
            std::memcpy(Params.data() + Param->GetOffset_Internal(), Input.data(), std::min<size_t>(Input.size(), Param->GetSize()));
            break;
        }
        Library->ProcessEvent(Function, Params.data());
        if (!Return) return {};
        const auto* First = Params.data() + Return->GetOffset_Internal();
        return std::vector<uint8>(First, First + Return->GetSize());
    }

    UObject* LoadByPath(const CharType* Path)
    {
        auto* Library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
        if (!Library) return nullptr;
        std::vector<uint8> PathString(sizeof(FString));
        new (PathString.data()) FString(Path);
        const std::vector<uint8> SoftPath = CallKismet(Library, STR("MakeSoftObjectPath"), PathString);
        if (SoftPath.empty()) return nullptr;
        const std::vector<uint8> SoftReference = CallKismet(Library, STR("Conv_SoftObjPathToSoftObjRef"), SoftPath);
        if (SoftReference.empty()) return nullptr;
        const std::vector<uint8> Loaded = CallKismet(Library, STR("LoadAsset_Blocking"), SoftReference);
        if (Loaded.size() < sizeof(UObject*)) return nullptr;
        UObject* Object{};
        std::memcpy(&Object, Loaded.data(), sizeof(Object));
        return Object;
    }

    StringType StringTableEntry(const CharType* Table, StringViewType Key)
    {
        auto* Library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetTextLibrary"));
        UFunction* FromTable = Library ? Library->GetFunctionByNameInChain(STR("TextFromStringTable")) : nullptr;
        if (!FromTable) return {};
        std::vector<uint8> Params(std::max<int32>(FromTable->GetPropertiesSize(), 64));
        for (FProperty* Param : TFieldRange<FProperty>(FromTable, EFieldIterationFlags::IncludeDeprecated))
        {
            uint8* Value = Params.data() + Param->GetOffset_Internal();
            if (Param->GetName() == STR("TableId"))
            {
                const FName Id(Table, FNAME_Add);
                std::memcpy(Value, &Id, sizeof(Id));
            }
            else if (Param->GetName() == STR("Key"))
            {
                new (Value) FString(StringType(Key).c_str());
            }
        }
        Library->ProcessEvent(FromTable, Params.data());
        FProperty* Return = FromTable->GetReturnProperty();
        if (!Return) return {};
        const auto* First = Params.data() + Return->GetOffset_Internal();
        const std::vector<uint8> Converted = CallKismet(Library, STR("Conv_TextToString"), std::vector<uint8>(First, First + Return->GetSize()));
        if (Converted.size() < sizeof(FString)) return {};
        const auto* String = reinterpret_cast<const FString*>(Converted.data());
        if (String->Len() == 0) return {};
        StringType Result(**String, String->Len());
        if (Result.starts_with(STR("<MISSING STRING TABLE ENTRY>"))) return {};
        return Result;
    }

    void SetWidgetText(UObject* TextBlock, const StringType& Text)
    {
        auto* Library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetTextLibrary"));
        if (!Library || !TextBlock) return;
        std::vector<uint8> String(sizeof(FString));
        new (String.data()) FString(Text.c_str());
        const std::vector<uint8> Converted = CallKismet(Library, STR("Conv_StringToText"), String);
        if (Converted.empty()) return;
        CallByName(TextBlock, STR("SetText"), [&Converted](const auto& Name, uint8* Value) {
            if (Name == STR("InText")) std::memcpy(Value, Converted.data(), Converted.size());
        });
    }

    void SetWidgetShown(UObject* Widget, bool Shown)
    {
        const uint8 Visibility = Shown ? 0 : 1;
        CallByName(Widget, STR("SetVisibility"), [Visibility](const auto& Name, uint8* Value) {
            if (Name == STR("InVisibility")) *Value = Visibility;
        });
    }
}

class FFRRandomizer : public CppUserModBase
{
public:
    FFRRandomizer()
    {
        ModName = STR("FFR Randomizer");
        ModVersion = STR("0.1 ");
        ModDescription = STR("Seeded battle randomizer");
        ModAuthors = STR("Temporas");
    }

    auto on_unreal_init() -> void override
    {
        Hook::RegisterEngineTickPostCallback(
                [this](auto&, UEngine*, float DeltaSeconds, bool) {
                    Tick(DeltaSeconds);
                },
                {false, false, STR("FFRRandomizer"), STR("ApplySeed")});
        Hook::RegisterLoadMapPreCallback(
                [this](auto&, auto&&...) {
                    Reconcile();
                },
                {false, false, STR("FFRRandomizer"), STR("SeedOnLoad")});

        //UE4SS key bind
        register_keydown_event(Input::Key::F2, [this]() { TogglePanel = true; });
    }

private:
    struct FTable
    {
        FWeakObjectPtr Object{};
        std::vector<int32> Originals{};
        uint32_t AppliedSeed{};
        bool Applied{};
        int32 Randomized{};
    };

    // One of the panel's zone lists; Name and Kind match its widgets and ChooseZone, Entries the
    // entries made for it.
    struct FZoneList
    {
        const CharType* Name;
        int32 Kind;
        int32 Entries;
        bool Fields;
        std::vector<size_t> Order{};
        std::vector<StringType> Labels{};
        size_t Selected{};
    };

    uint32_t Seed{};
    bool HasSeed{};
    bool HasPending{};
    uint32_t PendingSeed{};

    std::unordered_map<StringType, FTable> Tables{};
    bool LoadedAllTables{};
    std::unordered_map<int32, std::vector<int32>> GroupUnits{};
    std::unordered_map<int32, StringType> UnitNames{};
    std::unordered_map<StringType, StringType> PlaceNames{};

    FZoneList ZoneLists[2]{{STR("Dungeon"), 1, 64, false}, {STR("Field"), 2, 96, true}};
    size_t ShownList{};
    FWeakObjectPtr ZonesFilledFor{};

    FWeakObjectPtr SaveManager{};
    FWeakObjectPtr Panel{};
    std::atomic<bool> TogglePanel{};
    float SinceCheck{};
    float SinceSearch{SearchSeconds};
    float SincePanelSearch{SearchSeconds};
    bool DisplayDirty{true};
    bool SeedRejected{};
    bool LoggedConflict{};

    auto Tick(float DeltaSeconds) -> void
    {
        TickPanel(DeltaSeconds);
        SinceSearch += DeltaSeconds;
        SinceCheck += DeltaSeconds;
        if (SinceCheck >= CheckSeconds)
        {
            SinceCheck = 0.0f;
            if (!LoadedAllTables && SaveFlags()) LoadAllTables();
            Reconcile();
        }
        if (DisplayDirty) UpdatePanel();
    }

    auto TickPanel(float DeltaSeconds) -> void
    {
        SincePanelSearch += DeltaSeconds;
        UObject* Widget = Panel.Get();
        if (!Widget && TogglePanel) SincePanelSearch = SearchSeconds;
        if (!Widget)
        {
            if (SincePanelSearch < SearchSeconds)
            {
                TogglePanel = false;
                return;
            }
            SincePanelSearch = 0.0f;
            Widget = FindPanel();
            if (!Widget)
            {
                TogglePanel = false;
                return;
            }
            Panel = Widget;
            DisplayDirty = true;
        }

        if (TogglePanel.exchange(false)) CallByName(Widget, STR("TogglePanel"), [](auto&&, auto&&) {});

        if (LoadedAllTables && !Tables.empty() && ZonesFilledFor.Get() != Widget) FillZones(Widget);

        // RequestedZone is Kind * 1000 + entry index + 1.
        if (auto* Zone = Widget->GetValuePtrByPropertyNameInChain<int32>(STR("RequestedZone")); Zone && *Zone > 0)
        {
            const int32 Kind = (*Zone - 1) / 1000;
            const size_t Chosen = static_cast<size_t>((*Zone - 1) % 1000);
            *Zone = 0;
            for (size_t ListIndex = 0; ListIndex < std::size(ZoneLists); ++ListIndex)
            {
                FZoneList& List = ZoneLists[ListIndex];
                if (List.Kind != Kind || Chosen >= List.Order.size()) continue;
                const size_t Previous = List.Selected;
                List.Selected = Chosen;
                ShownList = ListIndex;
                NameZoneItem(Widget, List, Previous);
                NameZoneItem(Widget, List, Chosen);
            }
            DisplayDirty = true;
        }

        auto* Request = Widget->GetValuePtrByPropertyNameInChain<FString>(STR("RequestedSeed"));
        if (!Request || Request->Len() == 0) return;

        uint32_t Parsed{};
        SeedRejected = !TextToSeed(StringViewType(**Request, Request->Len()), Parsed);
        if (!SeedRejected)
        {
            PendingSeed = Parsed;
            HasPending = true;
            Reconcile();
        }
        *Request = FString(STR(""));
        DisplayDirty = true;
    }

    // Brings the tables and live encounters in line with the save's seed; cheap when they match.
    auto Reconcile() -> void
    {
        uint8* Flags = SaveFlags();
        if (!Flags) return;

        uint32_t Want = Seed;
        const uint32_t StoredTag = ReadBits(Flags, TagFlag);
        const uint32_t StoredSeed = ReadBits(Flags, SeedFlag);
        if (HasPending)
        {
            if (StoredTag == Tag || (StoredTag == 0 && StoredSeed == 0))
            {
                WriteBits(Flags, TagFlag, Tag);
                WriteBits(Flags, SeedFlag, PendingSeed);
            }
            Want = PendingSeed;
            HasPending = false;
        }
        else if (StoredTag == Tag)
        {
            Want = StoredSeed;
        }
        else if (StoredTag == 0 && StoredSeed == 0)
        {
            Want = std::random_device{}();
            WriteBits(Flags, TagFlag, Tag);
            WriteBits(Flags, SeedFlag, Want);
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] new seed {:08X} written to this save\n"), Want);
        }
        else
        {
            // Something else uses these flags: keep the seed in memory only.
            if (!LoggedConflict)
            {
                LoggedConflict = true;
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] flags {}-{} are in use, the seed is not kept in this save\n"), TagFlag, FlagCount - 1);
            }
            if (!HasSeed) Want = std::random_device{}();
        }

        if (!HasSeed || Want != Seed)
        {
            Seed = Want;
            HasSeed = true;
            DisplayDirty = true;
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] seed {:08X}\n"), Seed);
        }
        if (ApplyTables())
        {
            PatchLiveEncounters();
            DisplayDirty = true;
        }
    }

    // The loaded save's event flags, or null on the title screen.
    auto SaveFlags() -> uint8*
    {
        UObject* Manager = SaveManager.Get();
        if (!Manager)
        {
            if (SinceSearch < SearchSeconds) return nullptr;
            SinceSearch = 0.0f;
            std::vector<UObject*> Found;
            UObjectGlobals::FindAllOf(STR("CPP_MuchaSaveDataManager"), Found);
            for (UObject* Object : Found)
            {
                if (Object && !Object->IsUnreachable() && !Object->HasAnyFlags(RF_ClassDefaultObject)) Manager = Object;
            }
            if (!Manager) return nullptr;
            SaveManager = Manager;
        }

        auto* SaveData = Manager->GetValuePtrByPropertyNameInChain<UObject*>(STR("m_pGameSaveData"));
        if (!SaveData || !*SaveData) return nullptr;
        auto* Progress = CastField<FStructProperty>((*SaveData)->GetPropertyByNameInChain(STR("Progress")));
        if (!Progress) return nullptr;
        auto* ProgressData = Progress->ContainerPtrToValuePtr<uint8>(*SaveData);

        // No story progress: the title screen's empty save.
        auto* Story = CastField<FArrayProperty>(Progress->GetStruct()->GetPropertyByNameInChain(STR("Progress")));
        if (!Story) return nullptr;
        auto* StoryArray = Story->ContainerPtrToValuePtr<FScriptArray>(ProgressData);
        if (StoryArray->Num() < 1 || static_cast<int32*>(StoryArray->GetData())[0] <= 0) return nullptr;

        auto* EventFlag = CastField<FArrayProperty>(Progress->GetStruct()->GetPropertyByNameInChain(STR("eventFlag")));
        if (!EventFlag) return nullptr;
        auto* FlagArray = EventFlag->ContainerPtrToValuePtr<FScriptArray>(ProgressData);
        if (FlagArray->Num() < FlagCount || EventFlag->GetInner()->GetElementSize() != 1) return nullptr;
        return static_cast<uint8*>(FlagArray->GetData());
    }

    // Randomize zone on loading save while respecting the seed in save file
    auto LoadAllTables() -> void
    {
        LoadedAllTables = true;
        for (const CharType* Path : EncountTables)
        {
            auto* DataTable = static_cast<UDataTable*>(LoadByPath(Path));
            UScriptStruct* Row = DataTable ? DataTable->GetRowStruct() : nullptr;
            FProperty* Id = Row ? Row->GetPropertyByNameInChain(STR("battleGroupId")) : nullptr;
            if (!Id)
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] could not load {}\n"), Path);
                continue;
            }
            DataTable->SetRootSet();
            FTable& Table = Tables[DataTable->GetName()];
            Table.Object = DataTable;
            for (auto& Pair : DataTable->GetRowMap())
            {
                Table.Originals.push_back(*reinterpret_cast<int32*>(Pair.Value + Id->GetOffset_Internal()));
            }
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] loaded {} of {} zone tables\n"), Tables.size(), std::size(EncountTables));

        for (const CharType* Path : {MapNameTable, UnitNameTable})
        {
            if (UObject* Table = LoadByPath(Path)) Table->SetRootSet();
        }
        ReadBattleGroups();
    }

    auto ReadBattleGroups() -> void
    {
        auto* DataTable = static_cast<UDataTable*>(LoadByPath(BattleGroupTable));
        UScriptStruct* Row = DataTable ? DataTable->GetRowStruct() : nullptr;
        FProperty* Id = Row ? Row->GetPropertyByNameInChain(STR("ID")) : nullptr;
        auto* Units = Row ? CastField<FArrayProperty>(Row->GetPropertyByNameInChain(STR("UnitIdList"))) : nullptr;
        if (!Id || !Units || Units->GetInner()->GetElementSize() != sizeof(int32))
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] DT_BattleGroup was not read: the zone list shows group numbers\n"));
            return;
        }
        for (auto& Pair : DataTable->GetRowMap())
        {
            const auto* List = Units->ContainerPtrToValuePtr<FScriptArray>(Pair.Value);
            const auto* First = static_cast<const int32*>(List->GetData());
            GroupUnits[*reinterpret_cast<int32*>(Pair.Value + Id->GetOffset_Internal())] = std::vector<int32>(First, First + List->Num());
        }
    }

    auto ApplyTables() -> bool
    {
        bool Changed = false;
        for (auto& [Name, Table] : Tables)
        {
            auto* DataTable = static_cast<UDataTable*>(Table.Object.Get());
            if (!DataTable || (Table.Applied && Table.AppliedSeed == Seed)) continue;
            const int32 Offset = DataTable->GetRowStruct()->GetPropertyByNameInChain(STR("battleGroupId"))->GetOffset_Internal();

            const std::vector<int32> Groups = GroupsForSeed(Seed, Name, Table.Originals);
            size_t Index = 0;
            Table.Randomized = 0;
            for (auto& Pair : DataTable->GetRowMap())
            {
                if (Index >= Groups.size()) break;
                *reinterpret_cast<int32*>(Pair.Value + Offset) = Groups[Index];
                if (IsNormalGroup(Table.Originals[Index])) ++Table.Randomized;
                ++Index;
            }
            Table.Applied = true;
            Table.AppliedSeed = Seed;
            Changed = true;
        }
        return Changed;
    }

    auto PatchLiveEncounters() -> void
    {
        for (const CharType* Class : EncountActorClasses)
        {
            std::vector<UObject*> Found;
            UObjectGlobals::FindAllOf(Class, Found);
            for (UObject* Actor : Found)
            {
                if (!Actor || Actor->IsUnreachable() || Actor->HasAnyFlags(RF_ClassDefaultObject)) continue;
                auto* Source = Actor->GetValuePtrByPropertyNameInChain<UObject*>(STR("m_pEncountDatatable"));
                if (!Source || !*Source) continue;
                const auto Known = Tables.find((*Source)->GetName());
                if (Known == Tables.end() || Known->second.Object.Get() != *Source) continue;
                auto* DataTable = static_cast<UDataTable*>(*Source);
                FProperty* TableId = DataTable->GetRowStruct()->GetPropertyByNameInChain(STR("battleGroupId"));

                auto* List = CastField<FArrayProperty>(Actor->GetPropertyByNameInChain(STR("m_EncountDataList")));
                if (!List || !TableId) continue;
                auto* Element = CastField<FStructProperty>(List->GetInner());
                if (!Element) continue;
                auto* Id = Element->GetStruct()->GetPropertyByNameInChain(STR("battleGroupId"));
                if (!Id) continue;

                auto* Array = List->ContainerPtrToValuePtr<FScriptArray>(Actor);
                auto* Data = static_cast<uint8*>(Array->GetData());
                const int32 Stride = Element->GetElementSize();
                int32 Index = 0;
                for (auto& Pair : DataTable->GetRowMap())
                {
                    if (Index >= Array->Num()) break;
                    *reinterpret_cast<int32*>(Data + Index * Stride + Id->GetOffset_Internal()) =
                            *reinterpret_cast<int32*>(Pair.Value + TableId->GetOffset_Internal());
                    ++Index;
                }
            }
        }
    }

    auto UnitName(int32 Unit) -> const StringType&
    {
        auto Found = UnitNames.find(Unit);
        if (Found != UnitNames.end()) return Found->second;
        StringType Name = StringTableEntry(UnitNameTable, std::format(STR("UnitName{}"), Unit));
        if (Name.empty()) Name = std::format(STR("unit {}"), Unit);
        return UnitNames.emplace(Unit, std::move(Name)).first->second;
    }

    auto GroupLabel(int32 Group) -> StringType
    {
        const auto Found = GroupUnits.find(Group);
        if (Found == GroupUnits.end() || Found->second.empty()) return std::format(STR("group {}"), Group);
        std::vector<std::pair<int32, int32>> Counts;
        for (const int32 Unit : Found->second)
        {
            if (Unit <= 0) continue;
            auto Same = std::find_if(Counts.begin(), Counts.end(), [Unit](const auto& Count) { return Count.first == Unit; });
            if (Same == Counts.end()) Counts.emplace_back(Unit, 1);
            else ++Same->second;
        }
        StringType Label;
        for (const auto& [Unit, Count] : Counts)
        {
            if (!Label.empty()) Label += STR(", ");
            Label += UnitName(Unit);
            if (Count > 1) Label += std::format(STR(" ×{}"), Count);
        }
        return Label;
    }

    auto PlaceName(const FZoneName& Zone) -> StringType
    {
        auto Found = PlaceNames.find(Zone.MapNameKey);
        if (Found == PlaceNames.end())
        {
            StringType Name = StringTableEntry(MapNameTable, Zone.MapNameKey);
            Found = PlaceNames.emplace(Zone.MapNameKey, Name.empty() ? StringType(Zone.Table) : Name).first;
        }
        return Found->second + Zone.Suffix;
    }

    // Names the entries of both lists in ZoneNames' order and hides the spare ones.
    auto FillZones(UObject* Widget) -> void
    {
        ZonesFilledFor = Widget;
        for (FZoneList& List : ZoneLists)
        {
            List.Order.clear();
            List.Labels.clear();
            for (size_t Index = 0; Index < std::size(ZoneNames); ++Index)
            {
                if (!Tables.contains(ZoneNames[Index].Table) || IsField(ZoneNames[Index].Table) != List.Fields) continue;
                List.Order.push_back(Index);
                List.Labels.push_back(PlaceName(ZoneNames[Index]));
            }
            if (List.Selected >= List.Order.size()) List.Selected = 0;
            if (List.Order.size() > static_cast<size_t>(List.Entries))
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] the panel's {} list shows {} of {} zones\n"), List.Name, List.Entries, List.Order.size());
            }

            for (int32 Index = 0; Index < List.Entries; ++Index)
            {
                auto* Entry = Widget->GetValuePtrByPropertyNameInChain<UObject*>(std::format(STR("{}Item{}"), List.Name, Index).c_str());
                if (!Entry || !*Entry) continue;
                const bool Used = static_cast<size_t>(Index) < List.Order.size();
                SetWidgetShown(*Entry, Used);
                if (Used) NameZoneItem(Widget, List, Index);
            }
        }
        DisplayDirty = true;
    }

    auto NameZoneItem(UObject* Widget, const FZoneList& List, size_t Index) -> void
    {
        if (Index >= List.Labels.size()) return;
        auto* Text = Widget->GetValuePtrByPropertyNameInChain<UObject*>(std::format(STR("{}ItemText{}"), List.Name, Index).c_str());
        if (!Text || !*Text) return;
        SetWidgetText(*Text, (Index == List.Selected ? StringType(ShownMark) : StringType(STR("    "))) + List.Labels[Index]);
    }

    auto ChooserText(size_t ListIndex) const -> StringType
    {
        const FZoneList& List = ZoneLists[ListIndex];
        if (List.Selected >= List.Labels.size()) return {};
        return (ListIndex == ShownList ? StringType(ShownMark) : StringType{}) + List.Labels[List.Selected];
    }

    // The shown zone's battles, one per line.
    auto ZoneText() -> StringType
    {
        const FZoneList& List = ZoneLists[ShownList];
        if (List.Selected >= List.Order.size()) return STR("Shown once a save is loaded.");
        const FZoneName& Zone = ZoneNames[List.Order[List.Selected]];
        const auto Known = Tables.find(Zone.Table);
        if (Known == Tables.end()) return {};
        auto* DataTable = static_cast<UDataTable*>(Known->second.Object.Get());
        FProperty* Id = DataTable ? DataTable->GetRowStruct()->GetPropertyByNameInChain(STR("battleGroupId")) : nullptr;
        if (!Id) return {};

        StringType Text;
        for (auto& Pair : DataTable->GetRowMap())
        {
            if (!Text.empty()) Text += STR("\n");
            Text += GroupLabel(*reinterpret_cast<int32*>(Pair.Value + Id->GetOffset_Internal()));
        }
        if (Known->second.Randomized == 0) Text += STR("\n\n(not randomized: bosses and fights you cannot escape)");
        return Text;
    }

    auto FindPanel() -> UObject*
    {
        std::vector<UObject*> Found;
        UObjectGlobals::FindAllOf(STR("WBP_SeedPanel_C"), Found);
        for (UObject* Widget : Found)
        {
            if (Widget && !Widget->IsUnreachable() && !Widget->HasAnyFlags(RF_ClassDefaultObject)) return Widget;
        }
        return nullptr;
    }

    auto UpdatePanel() -> void
    {
        UObject* Widget = Panel.Get();
        if (!Widget) return;
        UFunction* Update = Widget->GetFunctionByNameInChain(STR("UpdateDisplay"));
        if (!Update) return;

        StringType SeedText = HasSeed ? std::format(STR("{:08X}"), Seed) : StringType(STR("--------"));
        if (SeedRejected) SeedText += STR("    not a seed: use 1 to 8 hexadecimal digits");

        FUpdateDisplayParams Params{FString(SeedText.c_str()), FString(ChooserText(0).c_str()), FString(ChooserText(1).c_str()), FString(ZoneText().c_str())};
        Widget->ProcessEvent(Update, &Params);
        DisplayDirty = false;
    }
};

extern "C"
{
    __declspec(dllexport) RC::CppUserModBase* start_mod()
    {
        return new FFRRandomizer();
    }

    __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* Mod)
    {
        delete Mod;
    }
}
