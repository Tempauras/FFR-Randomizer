#include "FFRRandomizer.h"

// Encounters
// Normal encounter, Boss and Colosseum (important distinction)

namespace ffr
{
    // ---------------------- //
    // Player and actor stuff //
    // ---------------------- //

    auto FFRRandomizer::liveEncounters() -> std::vector<std::pair<UObject*, UDataTable*>>
    {
        std::vector<std::pair<UObject*, UDataTable*>> live;
        for (const CharType* actorClass : encountActorClasses)
        {
            std::vector<UObject*> found;
            UObjectGlobals::FindAllOf(actorClass, found);
            for (UObject* actor : found)
            {
                if (!actor || actor->IsUnreachable() || actor->HasAnyFlags(RF_ClassDefaultObject)) continue;
                auto* source = actor->GetValuePtrByPropertyNameInChain<UObject*>(STR("m_pEncountDatatable"));
                if (!source || !*source) continue;
                const auto known = tables.find((*source)->GetName());
                if (known == tables.end() || known->second.object.Get() != *source) continue;
                live.emplace_back(actor, static_cast<UDataTable*>(*source));
            }
        }
        return live;
    }

    // Encounter actors copy their table's rows when they spawn onto themselves -- They spawn as soon as you load a map
    // if you were to change seed while in a map the actors would not update their own stats
    // This fixes this problem
    auto FFRRandomizer::patchLiveEncounters() -> void
    {
        for (auto [actor, dataTable] : liveEncounters())
        {
            FProperty* tableId = dataTable->GetRowStruct()->GetPropertyByNameInChain(STR("battleGroupId"));
            auto* list = CastField<FArrayProperty>(actor->GetPropertyByNameInChain(STR("m_EncountDataList")));
            if (!list || !tableId) continue;
            auto* element = CastField<FStructProperty>(list->GetInner());
            if (!element) continue;
            auto* id = element->GetStruct()->GetPropertyByNameInChain(STR("battleGroupId"));
            if (!id) continue;

            auto* array = list->ContainerPtrToValuePtr<FScriptArray>(actor);
            auto* data = static_cast<uint8*>(array->GetData());
            const int32 stride = element->GetElementSize();
            int32 index = 0;
            for (auto& pair : dataTable->GetRowMap())
            {
                if (index >= array->Num()) break;
                *reinterpret_cast<int32*>(data + index * stride + id->GetOffset_Internal()) =
                    *reinterpret_cast<int32*>(pair.Value + tableId->GetOffset_Internal());
                ++index;
            }
        }
    }

    // Update the map of each encounter in the game that we then compare to player location against with updatePlace
    // If it finds nothing we just keep the last update actors
    auto FFRRandomizer::updateAreas() -> void
    {
        std::vector<FEncounterArea> found;
        for (auto [actor, dataTable] : liveEncounters())
        {
            UFunction* bounds = actor->GetFunctionByNameInChain(STR("GetActorBounds"));
            if (!bounds) continue;
            std::vector<uint8> params(std::max<int32>(bounds->GetPropertiesSize(), 64));
            actor->ProcessEvent(bounds, params.data());
            FEncounterArea area{ dataTable->GetName() };
            bool read = true;
            for (FProperty* param : TFieldRange<FProperty>(bounds, EFieldIterationFlags::IncludeDeprecated))
            {
                const uint8* value = params.data() + param->GetOffset_Internal();
                if (param->GetName() == STR("Origin")) read &= readVector(value, param->GetSize(), area.origin);
                else if (param->GetName() == STR("BoxExtent")) read &= readVector(value, param->GetSize(), area.extent);
            }
            if (!read) continue;
            found.push_back(std::move(area));
            areaActor = actor;
        }
        if (!found.empty()) areas = std::move(found);
    }

    // The player pawn's location
    auto FFRRandomizer::playerLocation(double out[3]) -> bool
    {
        const std::vector<uint8> found = callWithWorld(STR("/Script/Engine.Default__GameplayStatics"), STR("GetPlayerPawn"), areaActor.Get());
        if (found.size() < sizeof(UObject*)) return false;
        UObject* pawn{};
        std::memcpy(&pawn, found.data(), sizeof(pawn));

        UFunction* location = pawn ? pawn->GetFunctionByNameInChain(STR("K2_GetActorLocation")) : nullptr;
        FProperty* locationReturn = location ? location->GetReturnProperty() : nullptr;
        if (!locationReturn) return false;
        std::vector<uint8> where(std::max<int32>(location->GetPropertiesSize(), 32));
        pawn->ProcessEvent(location, where.data());
        return readVector(where.data() + locationReturn->GetOffset_Internal(), locationReturn->GetSize(), out);
    }

    // Update the zone the player is in, with the nearest area to player pawn first
    auto FFRRandomizer::updatePlace(float elapsed) -> void
    {
        // X Y Z
        double playerPosition[3];
        if (areas.empty() || !playerLocation(playerPosition)) return;

        // On battle starts the game move the pawn way below the map (more than 250 unit negatively), every other change in Z axis is way smaller (stairs/slopes) or over a much longer period of time
        // while there *could* be a 250 drops that is not a battle drop, we verify how long it has been since the last Z axis movement
        // if its longer than parkedSeconds (05/11/2026 = 10s) we consider it a normal movement 
        const bool dropped = hasPawnZ && playerPosition[2] < lastPawnZ - battleDrop;
        // battle can obviously last longer than parkedSecond so if we're currently in a battle (and have dropped recently) just return
        const bool inBattle = std::any_of(lastBattle.begin(), lastBattle.end(), [](FWeakObjectPtr& enemy) { return enemy.Get() != nullptr; });
        if (dropped && inBattle) return;
        if (dropped && (parked += elapsed) < parkedSeconds) return;
        parked = 0.0f;
        lastPawnZ = playerPosition[2];
        hasPawnZ = true;

        std::vector<std::pair<double, const StringType*>> byDistance;
        for (const FEncounterArea& area : areas)
        {
            double distance{};
            for (int32 axis = 0; axis < 3; ++axis)
            {
                const double outside = std::max(0.0, std::abs(playerPosition[axis] - area.origin[axis]) - area.extent[axis]);
                distance += outside * outside;
            }
            byDistance.emplace_back(distance, &area.table);
        }
        std::stable_sort(byDistance.begin(), byDistance.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        nearbyTables.clear();
        for (const auto& [distance, table] : byDistance)
        {
            if (std::find(nearbyTables.begin(), nearbyTables.end(), *table) == nearbyTables.end()) nearbyTables.push_back(*table);
        }
        prepareZone();
    }

    // ----------------- //
    // Global encounters //
    // ----------------- //

    auto FFRRandomizer::loadAllTables() -> void
    {
        loadedAllTables = true;
        for (const CharType* path : EncountTables)
        {
            auto* dataTable = static_cast<UDataTable*>(loadByPath(path));
            UScriptStruct* row = dataTable ? dataTable->GetRowStruct() : nullptr;
            FProperty* id = row ? row->GetPropertyByNameInChain(STR("battleGroupId")) : nullptr;
            if (!id)
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] could not load {}\n"), path);
                continue;
            }
            dataTable->SetRootSet();
            FTable& table = tables[dataTable->GetName()];
            table.object = dataTable;
            for (auto& pair : dataTable->GetRowMap())
            {
                table.originals.push_back(*reinterpret_cast<int32*>(pair.Value + id->GetOffset_Internal()));
            }
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] loaded {} of {} zone tables\n"), tables.size(), std::size(EncountTables));

        readBattleGroups();
        loadItemTables();
    }

    // read DT_BattleGroup with ID that we reference from BattleGroupPool.h
    // also create colosseum's group field copy
    auto FFRRandomizer::readBattleGroups() -> void
    {
        auto* dataTable = static_cast<UDataTable*>(loadByPath(battleGroupTable));
        UScriptStruct* row = dataTable ? dataTable->GetRowStruct() : nullptr;
        FProperty* id = row ? row->GetPropertyByNameInChain(STR("ID")) : nullptr;
        auto* units = row ? CastField<FArrayProperty>(row->GetPropertyByNameInChain(STR("UnitIdList"))) : nullptr;
        if (!id || !units || units->GetInner()->GetElementSize() != sizeof(int32))
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] DT_BattleGroup was not read\n"));
            return;
        }
        dataTable->SetRootSet();
        for (const CharType* name : enemyFields)
        {
            FProperty* field = row->GetPropertyByNameInChain(name);
            if (!field || field->GetSize() != sizeof(FScriptArray))
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] DT_BattleGroup has no {} as expected\n"), name);
                enemyFieldOffsets.clear();
                enemyFieldProperties.clear();
                break;
            }
            enemyFieldOffsets.push_back(field->GetOffset_Internal());
            enemyFieldProperties.push_back(field);
        }
        if (!enemyFieldOffsets.empty())
        {
            std::unordered_set<int32> pooled(std::begin(BossGroupPool), std::end(BossGroupPool));
            for (auto& pair : dataTable->GetRowMap())
            {
                const int32 group = *reinterpret_cast<int32*>(pair.Value + id->GetOffset_Internal());
                if (!pooled.contains(group)) continue;
                poolRows[group] = pair.Value;
                enemiesIn[group] = group;
            }
            if (poolRows.size() != pooled.size())
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] found {} of {} boss groups\n"), poolRows.size(), pooled.size());
                poolRows.clear();
                enemiesIn.clear();
            }
        }

        for (auto& pair : dataTable->GetRowMap()) readGroupRow(row, pair.Value);
        makeFieldCopies(dataTable);
        readUnitStats();
    }

    // Read one row units, ap and escape battle condition
    auto FFRRandomizer::readGroupRow(UScriptStruct* row, uint8* data) -> void
    {
        FProperty* id = row->GetPropertyByNameInChain(STR("ID"));
        auto* units = CastField<FArrayProperty>(row->GetPropertyByNameInChain(STR("UnitIdList")));
        FProperty* ap = row->GetPropertyByNameInChain(STR("ap"));
        auto* canEscape = CastField<FBoolProperty>(row->GetPropertyByNameInChain(STR("CanEscape")));
        FProperty* odds = row->GetPropertyByNameInChain(STR("probabilityOfSuccessfulEscape"));
        const int32 group = *reinterpret_cast<int32*>(data + id->GetOffset_Internal());
        groupRows[group] = data;
        const auto* list = units->ContainerPtrToValuePtr<FScriptArray>(data);
        const auto* first = static_cast<const int32*>(list->GetData());
        groupUnits[group] = std::vector<int32>(first, first + list->Num());
        if (ap && ap->GetSize() == sizeof(int32))
        {
            auto* value = reinterpret_cast<int32*>(data + ap->GetOffset_Internal());
            groupAp[group] = {*value, value};
        }
        if (canEscape && odds && odds->GetSize() == sizeof(float))
        {
            uint8* byte = data + canEscape->GetOffset_Internal() + canEscape->GetByteOffset();
            auto* value = reinterpret_cast<float*>(data + odds->GetOffset_Internal());
            groupEscape[group] = {(*byte & canEscape->GetFieldMask()) != 0, *value, byte, canEscape->GetFieldMask(), value};
        }
    }

    // A group's units, without the empty slots.
    auto FFRRandomizer::lineupOf(int32 group) const -> std::vector<int32>
    {
        std::vector<int32> units;
        if (const auto found = groupUnits.find(group); found != groupUnits.end())
        {
            for (const int32 unit : found->second) if (unit > 0) units.push_back(unit);
        }
        return units;
    }

    // The later wave (WaveGroups, FormChangeGroups, ColosseumWaveGroups) these enemies are all part of, or 0.
    auto FFRRandomizer::waveOf(const std::vector<UObject*>& enemies) const -> int32
    {
        std::vector<int32> waves(std::begin(WaveGroups), std::end(WaveGroups));
        waves.insert(waves.end(), std::begin(FormChangeGroups), std::end(FormChangeGroups));
        waves.insert(waves.end(), std::begin(ColosseumWaveGroups), std::end(ColosseumWaveGroups));
        for (const int32 wave : waves)
        {
            std::vector<int32> left = lineupOf(wave);
            const bool inWave = !left.empty() && std::all_of(enemies.begin(), enemies.end(), [&left](UObject* parameter) {
                const auto at = std::find(left.begin(), left.end(), field(parameter, unitIdOffset));
                if (at == left.end()) return false;
                left.erase(at);
                return true;
            });
            if (inWave) return wave;
        }
        return 0;
    }

    // How many enemies to wait for before scaling: a later wave's full lineup when these enemies are one
    // (WaveGroups), else the battle group's. A wave's enemies arrive a few frames apart.
    auto FFRRandomizer::expectedEnemies(int32 group, const std::vector<UObject*>& enemies) -> size_t
    {
        if (const int32 wave = waveOf(enemies)) return lineupOf(wave).size();
        return lineupOf(enemiesOf(group)).size();
    }

    // The group whose enemies a battle group now holds.
    auto FFRRandomizer::enemiesOf(int32 group) const -> int32
    {
        const auto found = enemiesIn.find(group);
        return found == enemiesIn.end() ? group : found->second;
    }

    // Scales a boss or Colosseum fight that holds another group's enemies against the fight it replaces, each wave on
    // its own. Boss rows follow Boss scaling, Colosseum rows Normal scaling: Replaced stats takes the replaced
    // numbers, Balanced works as for random battles.
    auto FFRRandomizer::scaleMovedEnemies(int32 group, const std::vector<UObject*>& enemies) -> void
    {
        const Option scaling = poolRows.contains(group) ? bossScalingOption : scalingOption;
        if (!isOn(scaling)) return;
        const auto fighters = [this](const std::vector<int32>& ids) {
            std::vector<FFighter> out;
            for (const int32 unit : ids)
            {
                const auto found = unitStats.find(unit);
                if (found == unitStats.end()) return std::vector<FFighter>{};
                FFighter fighter = fighterOf(found->second);
                fighter.boss = bossUnits.contains(unit);
                out.push_back(fighter);
            }
            return out;
        };
        std::vector<int32> originalIds;
        for (const int32 unit : groupUnits[group]) if (unit > 0) originalIds.push_back(unit);
        std::vector<int32> replacementIds;
        for (UObject* parameter : enemies) replacementIds.push_back(field(parameter, unitIdOffset));
        const std::vector<FFighter> original = fighters(originalIds);
        const std::vector<FFighter> replacement = fighters(replacementIds);
        if (original.empty() || replacement.empty()) return;

        const std::vector<FFighter> scaled = scalingChoice(scaling) == 1 ? matchGroup(original, replacement) : scaleGroup(original, replacement);
        // A form change keeps the HP and Break it carried over.
        const int32 wave = waveOf(enemies);
        const bool formChange = wave && std::find(std::begin(FormChangeGroups), std::end(FormChangeGroups), wave) != std::end(FormChangeGroups);
        for (size_t index = 0; index < enemies.size(); ++index) writeFighter(enemies[index], scaled[index], formChange);
        double before{}, after{};
        for (const FFighter& enemy : original) before += fairThreat(enemy);
        for (const FFighter& enemy : scaled) after += threat(enemy);
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] group {} holds group {}'s enemies, scaled ({}) to its own: level {}, strength x{:.2f}, HP {}\n"),
                group, enemiesOf(group), scalingChoices[scalingChoice(scaling)], scaled.front().level, before > 0 ? after / before : 0.0,
                field(enemies.front(), hitPointOffset));
    }

    // ----------------- //
    // Normal encounters //
    // ----------------- //

    // Does nothing when the tables already match the seed/we haven't changed encounter randomization
    auto FFRRandomizer::applyEncounterTables() -> bool
    {
        const bool current = std::all_of(tables.begin(), tables.end(), [this](const auto& entry) {
            return entry.second.applied && entry.second.appliedSeed == seed;
        });
        if (current && battlesShuffled == isOn(battlesOption)) return false;
        battlesShuffled = isOn(battlesOption);

        std::vector<int32> shuffled = shuffledPool(seed);
        // Without field copies, the Colosseum keeps its fights: its entries leave the pool.
        if (fieldCopies.empty())
        {
            std::erase_if(shuffled, [](int32 group) { return std::find(std::begin(ColosseumGroups), std::end(ColosseumGroups), group) != std::end(ColosseumGroups); });
        }

        size_t used = 0;
        for (const CharType* path : EncountTables)
        {
            const StringViewType fullPath(path);
            const auto known = tables.find(StringType(fullPath.substr(fullPath.rfind(STR('.')) + 1)));
            if (known == tables.end()) continue;
            FTable& table = known->second;
            auto* dataTable = static_cast<UDataTable*>(table.object.Get());
            if (!dataTable)
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] {} is no longer loaded\n"), known->first);
                continue;
            }
            const int32 offset = dataTable->GetRowStruct()->GetPropertyByNameInChain(STR("battleGroupId"))->GetOffset_Internal();

            size_t index = 0;
            for (auto& pair : dataTable->GetRowMap())
            {
                if (index >= table.originals.size()) break;
                int32 group = table.originals[index++];
                if (battlesShuffled && isNormalGroup(group) && used < shuffled.size())
                {
                    group = shuffled[used++];
                    // Fight field copies of colosseum fight
                    if (const auto copy = fieldCopies.find(group); copy != fieldCopies.end()) group = copy->second;
                }
                *reinterpret_cast<int32*>(pair.Value + offset) = group;
            }
            table.applied = true;
            table.appliedSeed = seed;
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] {} of {} pool entries used\n"), used, shuffled.size());
        if (!fieldCopies.empty()) applyColosseum(shuffled);
        return true;
    }

    // The fight a shuffled group replaced here: of the rows now holding it in the nearest table that has one, the
    // strongest original.
    auto FFRRandomizer::originalGroup(int32 group) -> int32
    {
        int32 strongest{};
        double strongestThreat = -1;
        for (const StringType& name : nearbyTables)
        {
            const auto known = tables.find(name);
            if (known == tables.end()) continue;
            auto* dataTable = static_cast<UDataTable*>(known->second.object.Get());
            FProperty* id = dataTable ? dataTable->GetRowStruct()->GetPropertyByNameInChain(STR("battleGroupId")) : nullptr;
            if (!id) continue;
            size_t index = 0;
            for (auto& pair : dataTable->GetRowMap())
            {
                if (index >= known->second.originals.size()) break;
                const int32 original = known->second.originals[index++];
                if (*reinterpret_cast<int32*>(pair.Value + id->GetOffset_Internal()) != group) continue;
                const double total = fightThreat(groupFighters(original));
                if (total > strongestThreat)
                {
                    strongest = original;
                    strongestThreat = total;
                }
            }
            if (strongest) break;
        }
        return strongest;
    }

    // this allow us to put non escapable mobs (aka dragon's maw) in other zone and still make them escapable
    auto FFRRandomizer::setBattleEscape(int32 group, const std::vector<int32>& originals) -> void
    {
        const auto own = groupEscape.find(group);
        if (own == groupEscape.end() || originals.empty()) return;
        bool allowed{};
        float odds = own->second.odds;
        for (const int32 original : originals)
        {
            const auto rule = groupEscape.find(original);
            if (rule == groupEscape.end() || !rule->second.allowed) continue;
            odds = allowed ? std::max(odds, rule->second.odds) : rule->second.odds;
            allowed = true;
        }
        FEscape& written = own->second;
        *written.allowedByte = allowed ? (*written.allowedByte | written.allowedMask) : (*written.allowedByte & ~written.allowedMask);
        *written.oddsValue = odds;
    }

    // Funny thing is AP isn't per unit, but per battle group, so we have to do a little bit extra to get the correct payout
    auto FFRRandomizer::setBattleAp(int32 group, int32 replaced, double strength) -> void
    {
        const auto fight = groupAp.find(group);
        if (fight == groupAp.end()) return;
        const auto old = groupAp.find(replaced);
        *fight->second.second = old == groupAp.end() ? fight->second.first : static_cast<int32>(std::lround(old->second.first * strength));
    }

    // The game reads a fight's ap and escape rule from the group's row before the enemies exist
    // so we set them on zone/seed/option change
    auto FFRRandomizer::prepareZone() -> void
    {
        if (nearbyTables.empty() || (nearbyTables.front() == preparedZone && preparedSeed == seed && preparedOptions == optionsOff)) return;
        preparedZone = nearbyTables.front();
        preparedSeed = seed;
        preparedOptions = optionsOff;
        const auto known = tables.find(preparedZone);
        auto* dataTable = known == tables.end() ? nullptr : static_cast<UDataTable*>(known->second.object.Get());
        FProperty* id = dataTable ? dataTable->GetRowStruct()->GetPropertyByNameInChain(STR("battleGroupId")) : nullptr;
        if (!id) return;

        // All the groups in the zone
        std::unordered_map<int32, std::vector<int32>> groups;
        size_t index = 0;
        for (auto& pair : dataTable->GetRowMap())
        {
            const int32 group = *reinterpret_cast<int32*>(pair.Value + id->GetOffset_Internal());
            auto& originals = groups[group];
            if (index < known->second.originals.size()) originals.push_back(known->second.originals[index]);
            ++index;
        }
        for (const auto& [group, originals] : groups)
        {
            setBattleEscape(group, originals);
            setBattleAp(group, group, 1.0);
            if (!isOn(scalingOption)) continue;

            const int32 replaced = originalGroup(group);
            const std::vector<FFighter> original = groupFighters(replaced);
            const std::vector<FFighter> replacement = groupFighters(group);

            if (original.empty() || replacement.empty() || replaced == group) continue;
            double strength = 1.0;
            // Balanced: the strength the battle's scaling will give it.
            if (scalingChoice() == 2)
            {
                double after{};
                for (const FFighter& enemy : scaleGroup(original, replacement)) after += threat(enemy);
                const double before = fightThreat(original);
                if (before > 0) strength = after / before;
            }
            setBattleAp(group, replaced, strength);
        }
    }

    // ----------------- //
    // Bosses encounters //
    // ----------------- //

    // Whose enemies each boss group gets: the pool shuffled among itself, or its own with Boss battles off.
    auto FFRRandomizer::shuffledBosses() -> std::unordered_map<int32, int32>
    {
        std::unordered_map<int32, int32> shuffled;
        std::mt19937 rng{seed ^ bossStream};
        const std::vector<int32> pool(std::begin(BossGroupPool), std::end(BossGroupPool));
        std::vector<int32> enemies = pool;
        if (isOn(bossesOption) && hasSeed) shuffleWith(enemies, rng);
        for (size_t index = 0; index < pool.size(); ++index) shuffled[pool[index]] = enemies[index];
        return shuffled;
    }

    // Swaps enemy fields between rows until each group holds the enemies the shuffle gives it. Each array and string
    // keeps a single owner, so nothing is copied or freed.
    auto FFRRandomizer::applyBosses() -> void
    {
        if (poolRows.empty()) return;
        size_t moved{};
        for (const auto& [group, wanted] : shuffledBosses())
        {
            if (enemiesIn[group] == wanted) continue;
            const auto holder = std::find_if(enemiesIn.begin(), enemiesIn.end(), [this, wanted](const auto& entry) { return poolRows.contains(entry.first) && entry.second == wanted; });
            if (holder == enemiesIn.end()) continue;
            for (const int32 offset : enemyFieldOffsets)
            {
                std::swap_ranges(poolRows[group] + offset, poolRows[group] + offset + sizeof(FScriptArray), poolRows[holder->first] + offset);
            }
            std::swap(enemiesIn[group], holder->second);
            ++moved;
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] bosses {} for seed {:08X}: {} groups changed\n"), isOn(bossesOption) ? STR("shuffled") : STR("in place"), seed, moved);
    }

    // -------------------- //
    // Colosseum encounters //
    // -------------------- //

    // For each Colosseum fight: a copy of its row as the game has it, and a field copy added to DT_BattleGroup
    // (id + fieldCopyOffset) without the Colosseum's follow-ups or skipped result screens, for the encounter rows
    // that get a Colosseum fight. If anything is off, the Colosseum keeps its fights.
    auto FFRRandomizer::makeFieldCopies(UDataTable* dataTable) -> void
    {
        UScriptStruct* row = dataTable->GetRowStruct();
        FProperty* id = row->GetPropertyByNameInChain(STR("ID"));
        std::vector<FProperty*> events;
        for (const CharType* name : colosseumEvents)
        {
            FProperty* field = row->GetPropertyByNameInChain(name);
            if (field && field->GetSize() == sizeof(FName)) events.push_back(field);
        }
        std::vector<FBoolProperty*> skips;
        for (const CharType* name : colosseumSkips)
        {
            if (auto* field = CastField<FBoolProperty>(row->GetPropertyByNameInChain(name))) skips.push_back(field);
        }
        if (enemyFieldProperties.size() != std::size(enemyFields) || events.size() != std::size(colosseumEvents) || skips.size() != std::size(colosseumSkips))
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] DT_BattleGroup is not laid out as expected: the Colosseum keeps its fights\n"));
            return;
        }

        for (const int32 group : ColosseumGroups)
        {
            const auto live = groupRows.find(group);
            if (live == groupRows.end()) continue;
            auto* original = static_cast<uint8*>(FMemory::Malloc(row->GetSize()));
            row->InitializeStruct(original);
            row->CopyScriptStruct(original, live->second);

            const FName name(std::format(fieldCopyRow, group).c_str(), FNAME_Add);
            dataTable->AddRow(name, live->second, row);
            uint8** copy = dataTable->GetRowMap().Find(name);
            if (!copy || !*copy) continue;
            *reinterpret_cast<int32*>(*copy + id->GetOffset_Internal()) = group + fieldCopyOffset;
            for (FProperty* event : events) *reinterpret_cast<FName*>(*copy + event->GetOffset_Internal()) = FName{};
            for (FBoolProperty* skip : skips) *(*copy + skip->GetOffset_Internal() + skip->GetByteOffset()) &= ~skip->GetFieldMask();
            readGroupRow(row, *copy);

            colosseumRows[group] = live->second;
            colosseumOriginals[group] = original;
            fieldCopies[group] = group + fieldCopyOffset;
        }
        if (fieldCopies.size() != std::size(ColosseumGroups))
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] made {} of {} Colosseum field copies: the Colosseum keeps its fights\n"), fieldCopies.size(), std::size(ColosseumGroups));
            fieldCopies.clear();
            colosseumRows.clear();
        }
    }

    // Gives each Colosseum fight the enemies of the shuffled pool's last 20 entries, copied from the game's own
    // rows; with Normal battles off, its own.
    auto FFRRandomizer::applyColosseum(const std::vector<int32>& shuffled) -> void
    {
        if (colosseumRows.empty()) return;
        const bool shuffling = battlesShuffled && shuffled.size() >= std::size(ColosseumGroups);
        for (size_t index = 0; index < std::size(ColosseumGroups); ++index)
        {
            const int32 group = ColosseumGroups[index];
            const int32 source = shuffling ? shuffled[shuffled.size() - std::size(ColosseumGroups) + index] : group;
            const auto original = colosseumOriginals.find(source);
            const auto row = groupRows.find(source);
            const uint8* sourceRow = original != colosseumOriginals.end() ? original->second : row != groupRows.end() ? row->second : nullptr;
            if (!sourceRow) continue;
            for (FProperty* field : enemyFieldProperties) field->CopyCompleteValue_InContainer(colosseumRows[group], sourceRow);
            enemiesIn[group] = source;
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] Colosseum {}\n"), shuffling ? STR("fights shuffled") : STR("fights as the game has them"));
    }
}
