#include "FFRRandomizer.h"

// Attempt at swapping shrine event of Sephiroth with other Shrine, did not really work out and is not as important so shelved for now
// Also caused really weird problem when reloading was done inside a shrine

// In game data refer to each 'Named FF' vision as legends, so Cloud, Sephiroth, Tidus, Terra etc
// Tried to keep game reference as it is 

namespace ffr
{
    auto FFRRandomizer::readShrineTables(UDataTable* eventRows) -> void
    {
        readShrineRows(eventRows);
        if (auto* maps = static_cast<UDataTable*>(loadByPath(mapDataTable)); maps && maps->GetRowStruct())
        {
            mapTables = tableFamily(maps);
            UScriptStruct* row = maps->GetRowStruct();
            FProperty* id = row->GetPropertyByNameInChain(STR("ID"));
            FProperty* stage = row->GetPropertyByNameInChain(STR("battleStage"));
            if (id && stage && id->GetSize() == sizeof(int32) && stage->GetSize() == sizeof(int32))
            {
                for (auto& pair : maps->GetRowMap()) battleStages[*reinterpret_cast<int32*>(pair.Value + id->GetOffset_Internal())] = *reinterpret_cast<int32*>(pair.Value + stage->GetOffset_Internal());
            }
        }
        if (auto* stages = static_cast<UDataTable*>(loadByPath(battleStageTable)); stages && stages->GetRowStruct()) stageTables = tableFamily(stages);
        if (!battleStages.contains(sephirothShrine))
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] the maps battle stages could not be read\n"));
        }
    }

    auto FFRRandomizer::eventLists(UScriptStruct* row) -> FEventLists
    {
        FEventLists lists;
        const auto intArray = [row](const CharType* name) -> FArrayProperty* {
            auto* array = row ? CastField<FArrayProperty>(row->GetPropertyByNameInChain(name)) : nullptr;
            return array && array->GetInner() && array->GetInner()->GetSize() == sizeof(int32) ? array : nullptr;
        };
        lists.onFlags = intArray(STR("OnFlagList"));
        lists.offFlags = intArray(STR("OffFlagList"));
        lists.clearedMaps = intArray(STR("ClearedMapIdList"));
        lists.continuous = row ? CastField<FArrayProperty>(row->GetPropertyByNameInChain(STR("ContinuousEventList"))) : nullptr;
        auto* element = lists.continuous ? CastField<FStructProperty>(lists.continuous->GetInner()) : nullptr;
        if (element)
        {
            lists.stride = element->GetElementSize();
            auto* inner = static_cast<UScriptStruct*>(element->GetStruct());
            lists.condition = inner->GetPropertyByNameInChain(STR("Condition"));
            lists.eventId = inner->GetPropertyByNameInChain(STR("EventId"));
        }
        if (lists.condition && lists.condition->GetSize() != sizeof(FString)) lists.condition = nullptr;
        if (lists.eventId && lists.eventId->GetSize() != sizeof(FName)) lists.eventId = nullptr;
        return lists;
    }

    auto FFRRandomizer::readInts(const uint8* row, FArrayProperty* array) -> std::vector<int32>
    {
        const auto* list = array->ContainerPtrToValuePtr<FScriptArray>(row);
        const auto* first = static_cast<const int32*>(list->GetData());
        return std::vector<int32>(first, first + list->Num());
    }

    auto FFRRandomizer::writeInts(uint8* row, FArrayProperty* array, const std::vector<int32>& values) -> void
    {
        auto* list = array->ContainerPtrToValuePtr<FScriptArray>(row);
        const int32 wanted = static_cast<int32>(values.size());
        if (list->Num() < wanted) list->AddZeroed(wanted - list->Num(), sizeof(int32), alignof(int32));
        if (list->Num() > wanted) list->Remove(wanted, list->Num() - wanted, sizeof(int32), alignof(int32));
        if (wanted > 0) std::memcpy(list->GetData(), values.data(), values.size() * sizeof(int32));
    }

    // Reads the event rows the shrine swap changes
    auto FFRRandomizer::readShrineRows(UDataTable* dataTable) -> void
    {
        const FEventLists lists = eventLists(dataTable->GetRowStruct());
        if (!lists.onFlags || !lists.offFlags || !lists.clearedMaps || !lists.condition || !lists.eventId)
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] event rows unknown layout\n"));
            return;
        }
        const auto& rowMap = dataTable->GetRowMap();
        const auto findRow = [&rowMap](const StringType& name) -> const uint8* {
            unsigned char* const* found = rowMap.Find(FName(name.c_str(), FNAME_Add));
            return found ? *found : nullptr;
        };
        const auto keep = [this, &lists](const StringType& name, const uint8* row) {
            shrineRows[name] = {readInts(row, lists.onFlags), readInts(row, lists.offFlags), readInts(row, lists.clearedMaps)};
        };
        const uint8* ending = findRow(sephirothEnd);
        if (!ending) return;
        keep(sephirothEnd, ending);

        for (int32 legend = firstSwappedLegend; legend <= lastSwappedLegend; ++legend)
        {
            const StringType last = legendRow(legend, STR("07"));
            const StringType vision = legendRow(legend, STR("06_VisionGet"));
            const StringType retry = std::format(STR("Int_LegendEV_Retry_Start_{:02}_02"), legend);
            const uint8* lastRow = findRow(last);
            const uint8* visionRow = findRow(vision);
            const uint8* retryRow = findRow(retry);
            if (!lastRow || !visionRow || !findRow(legendRow(legend, STR("01")))) continue;
            keep(last, lastRow);
            if (shrineRows[last].onFlags.empty() || shrineRows[last].clearedMaps.empty()) continue;

            std::vector<StringType> conditions;
            const auto* entries = lists.continuous->ContainerPtrToValuePtr<FScriptArray>(visionRow);
            for (int32 index = 0; index < entries->Num(); ++index)
            {
                const auto* text = reinterpret_cast<const FString*>(static_cast<const uint8*>(entries->GetData()) + index * lists.stride + lists.condition->GetOffset_Internal());
                conditions.emplace_back(text->GetCharArray().GetData() ? StringType(text->GetCharArray().GetData(), text->Len()) : StringType{});
            }
            shrineConditions[vision] = conditions;
            if (retryRow)
            {
                const auto* retries = lists.continuous->ContainerPtrToValuePtr<FScriptArray>(retryRow);
                if (retries->Num() > 0)
                {
                    FName next = *reinterpret_cast<const FName*>(static_cast<const uint8*>(retries->GetData()) + lists.eventId->GetOffset_Internal());
                    shrineRetries[retry] = next.ToString();
                }
            }
            swappableLegends.push_back(legend);
        }
    }

    // Writes the seed's shrine swap into every table of the event
    auto FFRRandomizer::applyShrineSwap() -> void
    {
        if (!shrineSwapEnabled) return;
        int32 legend{};
        if (isOn(visionsOption) && hasSeed && !swappableLegends.empty() && shrineRows.contains(sephirothEnd))
        {
            std::mt19937 rng{seed ^ shrineStream};
            legend = swappableLegends[std::uniform_int_distribution<size_t>(0, swappableLegends.size() - 1)(rng)];
        }

        for (FWeakObjectPtr& member : families[events])
        {
            auto* dataTable = static_cast<UDataTable*>(member.Get());
            if (!dataTable) continue;
            const FEventLists lists = eventLists(dataTable->GetRowStruct());
            if (!lists.onFlags || !lists.offFlags || !lists.clearedMaps || !lists.condition || !lists.eventId) continue;
            auto& rowMap = dataTable->GetRowMap();
            const auto findRow = [&rowMap](const StringType& name) -> uint8* {
                unsigned char** found = rowMap.Find(FName(name.c_str(), FNAME_Add));
                return found ? *found : nullptr;
            };
            const auto writeRow = [&](const StringType& name, const FShrineRow& values) {
                uint8* row = findRow(name);
                if (!row) return;
                writeInts(row, lists.onFlags, values.onFlags);
                writeInts(row, lists.offFlags, values.offFlags);
                writeInts(row, lists.clearedMaps, values.clearedMaps);
            };

            for (const auto& [name, values] : shrineRows) writeRow(name, values);
            for (const auto& [name, conditions] : shrineConditions)
            {
                uint8* row = findRow(name);
                if (!row) continue;
                auto* entries = lists.continuous->ContainerPtrToValuePtr<FScriptArray>(row);
                for (int32 index = 0; index < entries->Num() && index < static_cast<int32>(conditions.size()); ++index)
                {
                    *reinterpret_cast<FString*>(static_cast<uint8*>(entries->GetData()) + index * lists.stride + lists.condition->GetOffset_Internal()) = FString(conditions[index].c_str());
                }
            }
            for (const auto& [name, next] : shrineRetries)
            {
                uint8* row = findRow(name);
                auto* entries = row ? lists.continuous->ContainerPtrToValuePtr<FScriptArray>(row) : nullptr;
                if (entries && entries->Num() > 0) *reinterpret_cast<FName*>(static_cast<uint8*>(entries->GetData()) + lists.eventId->GetOffset_Internal()) = FName(next.c_str(), FNAME_Add);
            }
            if (legend == 0) continue;

            const StringType last = legendRow(legend, STR("07"));
            const FShrineRow& legendEnd = shrineRows[last];
            const FShrineRow& sephirothEnding = shrineRows[sephirothEnd];
            const int32 legendFlag = legendEnd.onFlags.front();

            writeRow(sephirothEnd, {legendEnd.onFlags, {}, legendEnd.clearedMaps});

            std::vector<int32> sephirothFlags = sephirothEnding.onFlags;
            writeRow(last, {sephirothFlags, legendEnd.offFlags, {}});

            const StringType vision = legendRow(legend, STR("06_VisionGet"));
            if (uint8* row = findRow(vision); row && shrineConditions.contains(vision))
            {
                auto* entries = lists.continuous->ContainerPtrToValuePtr<FScriptArray>(row);
                const auto& conditions = shrineConditions[vision];
                const StringType from = std::format(STR("{{flag:{}}}"), legendFlag);
                const StringType to = std::format(STR("{{flag:{}}}"), sephirothFlag);
                for (int32 index = 0; index < entries->Num() && index < static_cast<int32>(conditions.size()); ++index)
                {
                    StringType condition = conditions[index];
                    for (size_t at = condition.find(from); at != StringType::npos; at = condition.find(from, at + to.size())) condition.replace(at, from.size(), to);
                    *reinterpret_cast<FString*>(static_cast<uint8*>(entries->GetData()) + index * lists.stride + lists.condition->GetOffset_Internal()) = FString(condition.c_str());
                }
            }
            // If player ever try to reenter the shrine Sephiroth was swapped to play make sure it's flagged as being done
            // not a real check in original game because can't reacess Sephiroth shrine normally
            const StringType retry = std::format(STR("Int_LegendEV_Retry_Start_{:02}_02"), legend);
            if (uint8* row = findRow(retry))
            {
                auto* entries = lists.continuous->ContainerPtrToValuePtr<FScriptArray>(row);
                if (entries->Num() > 0) *reinterpret_cast<FName*>(static_cast<uint8*>(entries->GetData()) + lists.eventId->GetOffset_Internal()) = FName(legendRetryCancel, FNAME_Add);
            }
        }

        if (legend != swappedLegend)
        {
            if (legend) Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] Sephiroth's event is in Legend {:02}'s shrine, and that Legend in his\n"), legend);
            else Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] Sephiroth's event is in his own shrine\n"));
        }

        // Try to fix background being black problem from swapping the fight to a new shrine by swapping current shrine battle stage to Sephiroth one and vice versa
        const int32 hostMap = legend ? shrineRows[legendRow(legend, STR("07"))].clearedMaps.front() : 0;
        if (legend) addShrineStage();
        const int32 hostStage = shrineStageAdded ? shrineStage : otherworldStage;
        for (FWeakObjectPtr& member : mapTables)
        {
            auto* maps = static_cast<UDataTable*>(member.Get());
            UScriptStruct* row = maps ? static_cast<UScriptStruct*>(maps->GetRowStruct()) : nullptr;
            FProperty* id = row ? row->GetPropertyByNameInChain(STR("ID")) : nullptr;
            FProperty* stage = row ? row->GetPropertyByNameInChain(STR("battleStage")) : nullptr;
            if (!id || !stage || id->GetSize() != sizeof(int32) || stage->GetSize() != sizeof(int32)) continue;
            for (auto& pair : maps->GetRowMap())
            {
                const int32 map = *reinterpret_cast<int32*>(pair.Value + id->GetOffset_Internal());
                const auto own = battleStages.find(map);
                if (own == battleStages.end()) continue;
                *reinterpret_cast<int32*>(pair.Value + stage->GetOffset_Internal()) = map == hostMap ? hostStage : own->second;
            }
        }
        swappedLegend = legend;
        triggerScanLeft = std::max(triggerScanLeft, triggerScanStep);
    }

    // The shrine stage, in every table of the battle stage family
    auto FFRRandomizer::addShrineStage() -> void
    {
        if (shrineStageAdded) return;
        size_t added{};
        for (FWeakObjectPtr& member : stageTables)
        {
            auto* stages = static_cast<UDataTable*>(member.Get());
            UScriptStruct* row = stages ? static_cast<UScriptStruct*>(stages->GetRowStruct()) : nullptr;
            FProperty* id = row ? row->GetPropertyByNameInChain(STR("ID")) : nullptr;
            auto* instance = row ? CastField<FBoolProperty>(row->GetPropertyByNameInChain(STR("isLoadLevelInstance"))) : nullptr;
            if (!id || id->GetSize() != sizeof(int32) || !instance) continue;
            const uint8* source{};
            for (auto& pair : stages->GetRowMap())
            {
                if (*reinterpret_cast<int32*>(pair.Value + id->GetOffset_Internal()) == otherworldStage) source = pair.Value;
            }
            if (!source) continue;
            const FName name(shrineStageRow, FNAME_Add);
            stages->AddRow(name, source, row);
            uint8** copy = stages->GetRowMap().Find(name);
            if (!copy || !*copy) continue;
            *reinterpret_cast<int32*>(*copy + id->GetOffset_Internal()) = shrineStage;
            *(*copy + instance->GetOffset_Internal() + instance->GetByteOffset()) |= instance->GetFieldMask();
            ++added;
        }
        shrineStageAdded = added > 0;
        if (!shrineStageAdded) Output::send<LogLevel::Warning>(STR("[FFRRandomizer] the shrine stage could not be added: Sephiroth's fight shows the Otherworld unlit\n"));
    }

    auto FFRRandomizer::isSettledActor(UObject* object) -> bool
    {
        constexpr EObjectFlags unsettled = static_cast<EObjectFlags>(RF_ClassDefaultObject | RF_NeedInitialization | RF_NeedLoad | RF_NeedPostLoad
                | RF_NeedPostLoadSubobjects | RF_BeginDestroyed | RF_FinishDestroyed);
        constexpr EInternalObjectFlags elsewhere = static_cast<EInternalObjectFlags>(static_cast<int32>(EInternalObjectFlags::Async)
                | static_cast<int32>(EInternalObjectFlags::AsyncLoading) | static_cast<int32>(EInternalObjectFlags::Unreachable)
                | static_cast<int32>(EInternalObjectFlags::PendingKill) | static_cast<int32>(EInternalObjectFlags::PendingConstruction));
        return object && !object->HasAnyFlags(unsettled) && !object->HasAnyInternalFlags(elsewhere) && object->GetClassPrivate() && object->IsA<AActor>();
    }

    auto FFRRandomizer::sensibleArray(const FScriptArray* entries) -> bool
    {
        constexpr int32 maxEntries = 64;
        return entries->Num() >= 0 && entries->Num() <= maxEntries && entries->Max() >= entries->Num() && (entries->Num() == 0 || entries->GetData());
    }

    auto FFRRandomizer::setShrineTriggers() -> void
    {
        std::erase_if(triggerEvents, [](auto& known) { return !known.first.Get(); });
        const StringType legendStart = swappedLegend ? legendRow(swappedLegend, STR("01")) : StringType{};
        const StringType legendShort = swappedLegend ? legendRow(swappedLegend, STR("01_Short")) : StringType{};
        const auto swapped = [&](const FName& original) -> FName {
            if (!swappedLegend) return original;
            FName copy = original;
            const StringType name = copy.ToString();
            if (name == sephirothStart) return FName(legendStart.c_str(), FNAME_Add);
            if (name == legendStart || name == legendShort) return FName(sephirothStart, FNAME_Add);
            return original;
        };

        UObjectGlobals::ForEachUObject([&](UObject* object, int32, int32) {
            if (!isSettledActor(object)) return LoopAction::Continue;
            // Look each time in case a class has been unloaded and its memory freed
            auto* list = CastField<FArrayProperty>(object->GetClassPrivate()->GetPropertyByNameInChain(STR("m_EventList")));
            auto* element = list ? CastField<FStructProperty>(list->GetInner()) : nullptr;
            FProperty* id = element ? static_cast<UScriptStruct*>(element->GetStruct())->GetPropertyByNameInChain(STR("EventId")) : nullptr;
            if (!id || id->GetSize() != sizeof(FName)) return LoopAction::Continue;

            const int32 stride = element->GetElementSize();
            const int32 idOffset = id->GetOffset_Internal();
            auto* entries = list->ContainerPtrToValuePtr<FScriptArray>(object);
            if (!sensibleArray(entries)) return LoopAction::Continue;
            const auto at = [&](int32 index) { return reinterpret_cast<FName*>(static_cast<uint8*>(entries->GetData()) + index * stride + idOffset); };

            auto record = std::find_if(triggerEvents.begin(), triggerEvents.end(), [object](auto& known) { return known.first.Get() == object; });
            if (record == triggerEvents.end())
            {
                std::vector<FName> original;
                for (int32 index = 0; index < entries->Num(); ++index) original.push_back(*at(index));
                triggerEvents.emplace_back(FWeakObjectPtr(object), std::move(original));
                record = std::prev(triggerEvents.end());
            }
            for (int32 index = 0; index < entries->Num() && index < static_cast<int32>(record->second.size()); ++index)
            {
                const FName wanted = swapped(record->second[index]);
                if (*at(index) == wanted) continue;
                *at(index) = wanted;
                FName shown = wanted;
                Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] {} now starts {}\n"), object->GetName(), shown.ToString());
            }
            return LoopAction::Continue;
        });
    }
}
