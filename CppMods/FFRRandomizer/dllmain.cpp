// FFRRandomizer: only earth temple first battle randomize for now

#include <charconv>
#include <cstdint>
#include <format>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Mod/CppUserModBase.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/FWeakObjectPtr.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "BattleGroupPool.h"

using namespace RC;
using namespace RC::Unreal;

namespace
{
    constexpr float SearchSeconds = 5.0f;
    constexpr float PanelSearchSeconds = 2.0f;
    constexpr const CharType* TableName = STR("DT_01Gra_40_01_Encount");

    bool UsesFirstAreaTable(UObject* Actor)
    {
        auto* Table = Actor->GetValuePtrByPropertyNameInChain<UObject*>(STR("m_pEncountDatatable"));
        return Table && *Table && (*Table)->GetName() == TableName;
    }

    std::vector<int32> GroupsForSeed(uint32_t Seed, int32 Count)
    {
        std::mt19937 Rng{Seed};
        std::vector<int32> Groups(Count);
        for (int32& Group : Groups)
        {
            Group = BattleGroupPool[Rng() % std::size(BattleGroupPool)];
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

    struct FUpdateDisplayParams
    {
        FString SeedText;
        FString GroupsText;
    };
} 

class FFRRandomizer : public CppUserModBase
{
public:
    FFRRandomizer()
    {
        ModName = STR("FFR Randomizer");
        ModVersion = STR("2.0");
        ModDescription = STR("Seeded random battle groups in the first area");
        ModAuthors = STR("FFRS");

        Seed = std::random_device{}();
    }

    auto on_unreal_init() -> void override
    {
        Hook::RegisterEngineTickPostCallback(
                [this](auto&, UEngine*, float DeltaSeconds, bool) {
                    Tick(DeltaSeconds);
                },
                {false, false, STR("FFRRandomizer"), STR("ApplySeed")});
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] loaded, {} battle groups in the pool, seed {:08X}\n"),
                                        std::size(BattleGroupPool),
                                        Seed);
    }

private:
    uint32_t Seed{};
    std::vector<int32> Groups{};

    FWeakObjectPtr Area{};
    FWeakObjectPtr Panel{};
    bool NeedsApply{};
    float SinceSearch{SearchSeconds};
    float SincePanelSearch{PanelSearchSeconds};
    bool HasLogged{};
    uint32_t LoggedSeed{};
    bool DisplayDirty{true};
    bool SeedRejected{};

    auto Tick(float DeltaSeconds) -> void
    {
        TickPanel(DeltaSeconds);
        TickArea(DeltaSeconds);
        if (DisplayDirty) UpdatePanel();
    }

    auto TickPanel(float DeltaSeconds) -> void
    {
        SincePanelSearch += DeltaSeconds;
        UObject* Widget = Panel.Get();
        if (!Widget)
        {
            if (SincePanelSearch < PanelSearchSeconds) return;
            SincePanelSearch = 0.0f;
            Widget = FindPanel();
            if (!Widget) return;
            Panel = Widget;
            DisplayDirty = true;
        }

        auto* Request = Widget->GetValuePtrByPropertyNameInChain<FString>(STR("RequestedSeed"));
        if (!Request || Request->Len() == 0) return;

        uint32_t Parsed{};
        SeedRejected = !TextToSeed(StringViewType(**Request, Request->Len()), Parsed);
        if (!SeedRejected)
        {
            Seed = Parsed;
            NeedsApply = true;
        }
        *Request = FString(STR(""));
        DisplayDirty = true;
    }

    auto TickArea(float DeltaSeconds) -> void
    {
        SinceSearch += DeltaSeconds;

        UObject* Actor = Area.Get();
        if (!Actor)
        {
            if (!Groups.empty())
            {
                Groups.clear();
                DisplayDirty = true;
            }
            if (SinceSearch < SearchSeconds) return;
            SinceSearch = 0.0f;
            Actor = FindArea();
            if (!Actor) return;
            Area = Actor;
            NeedsApply = true;
        }

        if (!NeedsApply) return;
        NeedsApply = false;
        Apply(Actor);
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

    auto FindArea() -> UObject*
    {
        std::vector<UObject*> Found;
        UObjectGlobals::FindAllOf(STR("CPP_MapEncountPile"), Found);
        for (UObject* Actor : Found)
        {
            if (Actor && !Actor->IsUnreachable() && UsesFirstAreaTable(Actor)) return Actor;
        }
        return nullptr;
    }

    auto Apply(UObject* Actor) -> void
    {
        auto* List = CastField<FArrayProperty>(Actor->GetPropertyByNameInChain(STR("m_EncountDataList")));
        if (!List) return;
        auto* Element = CastField<FStructProperty>(List->GetInner());
        if (!Element) return;
        UScriptStruct* Struct = Element->GetStruct();
        auto* Id = Struct->GetPropertyByNameInChain(STR("battleGroupId"));
        if (!Id) return;

        auto* Array = List->ContainerPtrToValuePtr<FScriptArray>(Actor);
        auto* Data = static_cast<uint8*>(Array->GetData());
        const int32 Stride = Element->GetElementSize();
        const int32 Offset = Id->GetOffset_Internal();

        std::vector<int32> NewGroups = GroupsForSeed(Seed, Array->Num());
        for (int32 Index = 0; Index < Array->Num(); ++Index)
        {
            *reinterpret_cast<int32*>(Data + Index * Stride + Offset) = NewGroups[Index];
        }

        if (NewGroups != Groups)
        {
            Groups = std::move(NewGroups);
            DisplayDirty = true;
        }

        if (!HasLogged || LoggedSeed != Seed)
        {
            HasLogged = true;
            LoggedSeed = Seed;
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] seed {:08X}:{}\n"), Seed, GroupsText());
        }
    }

    auto GroupsText() const -> StringType
    {
        StringType Text;
        for (const int32 Group : Groups)
        {
            Text += std::format(STR(" {}"), Group);
        }
        return Text;
    }

    auto UpdatePanel() -> void
    {
        UObject* Widget = Panel.Get();
        if (!Widget) return;
        UFunction* Update = Widget->GetFunctionByNameInChain(STR("UpdateDisplay"));
        if (!Update) return;

        StringType Status;
        if (SeedRejected)
        {
            Status = STR("That seed was not valid: use 1 to 8 hexadecimal digits.");
        }
        else if (Groups.empty())
        {
            Status = STR("Battle groups appear here in the first area.");
        }
        else
        {
            Status = STR("Battle groups:") + GroupsText();
        }

        FUpdateDisplayParams Params{FString(std::format(STR("{:08X}"), Seed)), FString(Status)};
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
