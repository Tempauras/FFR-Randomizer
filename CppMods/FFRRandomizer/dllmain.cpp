#include "FFRRandomizer.h"

namespace ffr
{
    FFRRandomizer::FFRRandomizer()
    {
        ModName = STR("FFR Randomizer");
        ModVersion = STR("0.1 ");
        ModDescription = STR("Seeded battle randomizer");
        ModAuthors = STR("Temporas");
    }

    auto FFRRandomizer::on_unreal_init() -> void
    {
        Hook::RegisterEngineTickPostCallback(
                [this](auto&, UEngine*, float deltaSeconds, bool) {
                    tick(deltaSeconds);
                },
                {false, false, STR("FFRRandomizer"), STR("ApplySeed")});
        Hook::RegisterLoadMapPreCallback(
                [this](auto&, auto&&...) {
                    reconcile();
                },
                {false, false, STR("FFRRandomizer"), STR("SeedOnLoad")});
        Hook::RegisterLoadMapPostCallback(
                [this](auto&, auto&&...) {
                    hasPawnZ = false;
                    areas.clear();
                    itemsApplied = false;
                    if (shrineSwapEnabled) triggerScanLeft = triggerScanSeconds;
                    sinceTriggerScan = triggerScanStep - triggerScanDelay;
                },
                {false, false, STR("FFRRandomizer"), STR("PlaceOnLoad")});

        // units get their stats right after this; tick scales them on the frame they are filled
        Hook::RegisterStaticConstructObjectPostCallback(
                [this](const auto& data, const FStaticConstructObjectParameters&) {
                    static const FName parameterClass(STR("CPP_BtlUnitParameter"), FNAME_Add);
                    UObject* object = data.GetCurrentResolvedReturnValue();
                    if (!object || !object->GetClassPrivate()->GetNamePrivate().Equals(parameterClass)) return;
                    std::lock_guard lock(madeMutex);
                    madeParameters.emplace_back(object);
                },
                {false, true, STR("FFRRandomizer"), STR("BattleUnits")});

        //UE4SS key bind
        register_keydown_event(Input::Key::F2, [this]() { togglePanel = true; });
        register_keydown_event(Input::Key::F7, [this]() { dumpRequested = true; });
    }

    auto FFRRandomizer::tick(float deltaSeconds) -> void
    {
        tickPanel(deltaSeconds);
        if (dumpRequested.exchange(false)) dumpMapEvents();
        if (triggerScanLeft > 0.0f)
        {
            triggerScanLeft -= deltaSeconds;
            sinceTriggerScan += deltaSeconds;
            if (sinceTriggerScan >= triggerScanStep)
            {
                sinceTriggerScan = 0.0f;
                setShrineTriggers();
            }
        }
        sinceSearch += deltaSeconds;
        sinceCheck += deltaSeconds;
        if (sinceCheck >= checkSeconds)
        {
            sinceCheck = 0.0f;
            if (!loadedAllTables && saveFlags()) loadAllTables();
            addLayoutRows();
            reconcile();
            updateAreas();
        }
        sincePlace += deltaSeconds;
        if (sincePlace >= placeSeconds)
        {
            updatePlace(sincePlace);
            sincePlace = 0.0f;
        }
        scaleNewEnemies(deltaSeconds);
        if (displayDirty) updatePanel();
    }
}

extern "C"
{
    __declspec(dllexport) RC::CppUserModBase* start_mod()
    {
        return new ffr::FFRRandomizer();
    }

    __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
    {
        delete mod;
    }
}
