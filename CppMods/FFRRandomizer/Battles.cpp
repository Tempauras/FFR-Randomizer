#include "FFRRandomizer.h"

// Scaling a battle's enemies when the game fills in their stats.

namespace ffr
{
    // Every unit's own stats from DT_UnitParameter.
    auto FFRRandomizer::readUnitStats() -> void
    {
        auto* dataTable = static_cast<UDataTable*>(loadByPath(unitTable));
        UScriptStruct* row = dataTable ? dataTable->GetRowStruct() : nullptr;
        if (!row) return;
        const CharType* names[] = {STR("ID"), STR("Level"), STR("MaxHitPoint"), STR("MaxBreakPoint"), STR("Attack"), STR("Defence"),
                                   STR("Intelligence"), STR("Mind"), STR("Agility"), STR("Experience"), STR("Money")};
        std::vector<FProperty*> fields;
        for (const CharType* name : names)
        {
            FProperty* field = row->GetPropertyByNameInChain(name);
            if (!field || field->GetSize() != sizeof(int32))
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] DT_UnitParameter has no {} as expected: moved bosses keep their own stats\n"), name);
                return;
            }
            fields.push_back(field);
        }
        auto* isBoss = CastField<FBoolProperty>(row->GetPropertyByNameInChain(STR("IsBoss")));
        for (auto& pair : dataTable->GetRowMap())
        {
            if (isBoss && (*(pair.Value + isBoss->GetOffset_Internal() + isBoss->GetByteOffset()) & isBoss->GetFieldMask()))
            {
                bossUnits.insert(*reinterpret_cast<int32*>(pair.Value + fields[0]->GetOffset_Internal()));
            }
            int32 value[std::size(names)]{};
            for (size_t index = 0; index < fields.size(); ++index) value[index] = *reinterpret_cast<int32*>(pair.Value + fields[index]->GetOffset_Internal());
            unitStats[value[0]] = {value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8], value[9], value[10]};
        }
    }

    auto FFRRandomizer::groupFighters(int32 group) -> std::vector<FFighter>
    {
        std::vector<FFighter> fighters;
        const auto found = groupUnits.find(group);
        if (found == groupUnits.end()) return fighters;
        for (const int32 unit : found->second)
        {
            if (unit <= 0) continue;
            const FUnitStats* stats = poolUnit(unit);
            if (!stats) return {};
            FFighter fighter = fighterOf(*stats);
            fighter.boss = bossUnits.contains(unit);
            fighters.push_back(fighter);
        }
        return fighters;
    }

    auto FFRRandomizer::battleGroup() -> int32
    {
        UObject* worldContext = panel.Get() ? panel.Get() : saveManager.Get();
        const std::vector<uint8> found = callWithWorld(STR("/Script/mucha.Default__CPP_BFL_Battle"), STR("GetEncountBattleGroupdId"), worldContext);
        int32 group{};
        if (found.size() >= sizeof(group)) std::memcpy(&group, found.data(), sizeof(group));
        return group;
    }

    auto FFRRandomizer::field(UObject* parameter, int32 offset) -> int32&
    {
        return *reinterpret_cast<int32*>(reinterpret_cast<uint8*>(parameter) + offset);
    }

    // Scales a battle's enemies on the frame the game fills their stats in, once per battle.
    auto FFRRandomizer::scaleNewEnemies(float deltaSeconds) -> void
    {
        {
            std::lock_guard lock(madeMutex);
            pendingParameters.insert(pendingParameters.end(), madeParameters.begin(), madeParameters.end());
            madeParameters.clear();
        }
        if (pendingParameters.empty()) return;
        pendingSeconds += deltaSeconds;

        // Enemies whose stats are in; party members and other copies drop out.
        std::vector<UObject*> enemies;
        bool filling = false;
        for (auto it = pendingParameters.begin(); it != pendingParameters.end();)
        {
            UObject* parameter = it->Get();
            if (!parameter || parameter->GetClassPrivate()->GetPropertiesSize() != parameterSize)
            {
                it = pendingParameters.erase(it);
                continue;
            }
            const int64_t relation = field(parameter, unitIdOffset) > 0 ? callInt(parameter, STR("GetRelation")) : 0;
            if (relation == enemyRelation) enemies.push_back(parameter);
            else if (relation > 0)
            {
                it = pendingParameters.erase(it);
                continue;
            }
            else filling = true;
            ++it;
        }
        if (enemies.empty())
        {
            if (pendingSeconds >= battleWaitSeconds)
            {
                pendingParameters.clear();
                pendingSeconds = 0.0f;
            }
            return;
        }

        const int32 group = battleGroup();
        const size_t listed = expectedEnemies(group, enemies);
        if ((filling || enemies.size() < listed) && pendingSeconds < battleWaitSeconds) return;
        pendingParameters.clear();
        pendingSeconds = 0.0f;

        // Enemies joining a battle already scaled (a summon) keep their stats.
        const bool sameBattle = group == lastBattleGroup && std::any_of(lastBattle.begin(), lastBattle.end(), [](FWeakObjectPtr& enemy) {
            UObject* parameter = enemy.Get();
            return parameter && field(parameter, hitPointOffset) > 0;
        });
        if (sameBattle) return;
        lastBattleGroup = group;
        lastBattle.assign(enemies.begin(), enemies.end());
        scaleEnemies(group, enemies);
    }

    // Writes a scaled fighter's numbers into an enemy, at full HP and Break. A form change (keepCurrent) keeps the HP
    // and Break it carried over instead: only the maximums are scaled, and the current ones are held under them.
    auto FFRRandomizer::writeFighter(UObject* parameter, const FFighter& enemy, bool keepCurrent) -> void
    {
        field(parameter, levelOffset) = enemy.level;
        field(parameter, maxHitPointOffset) = std::max<int32>(1, static_cast<int32>(std::lround(enemy.stats[hitPoint])));
        field(parameter, maxBreakPointOffset) = std::max<int32>(1, static_cast<int32>(std::lround(enemy.stats[breakPoint])));
        for (const auto [offset, stat] : {std::pair{attackOffset, attack}, std::pair{defenceOffset, defence}, std::pair{intelligenceOffset, intelligence},
                                          std::pair{mindOffset, mind}, std::pair{agilityOffset, agility}, std::pair{experienceOffset, experience},
                                          std::pair{moneyOffset, money}})
        {
            field(parameter, offset) = std::max<int32>(0, static_cast<int32>(std::lround(enemy.stats[stat])));
        }
        // The getters give the new maximum with the difficulty applied. Nothing carried over (0) reads as full.
        const auto refill = [parameter, keepCurrent](int32 offset, const CharType* maxGetter) {
            const int64_t max = std::max<int64_t>(1, callInt(parameter, maxGetter));
            const int64_t now = field(parameter, offset);
            field(parameter, offset) = static_cast<int32>(keepCurrent && now > 0 ? std::min(now, max) : max);
        };
        refill(hitPointOffset, STR("GetMaxHitPoint"));
        refill(breakPointOffset, STR("GetMaxBreakPoint"));
    }

    auto FFRRandomizer::scaleEnemies(int32 group, const std::vector<UObject*>& enemies) -> void
    {
        setBattleAp(group, group, 1.0);
        if (enemiesOf(group) != group)
        {
            scaleMovedEnemies(group, enemies);
            return;
        }
        if (!isOn(scalingOption)) return;
        const int32 replaced = originalGroup(group);
        const std::vector<FFighter> original = groupFighters(replaced);
        if (original.empty() || replaced == group) return;

        std::vector<FFighter> replacement;
        for (UObject* parameter : enemies)
        {
            const FUnitStats* stats = poolUnit(field(parameter, unitIdOffset));
            if (!stats) return;
            FFighter fighter = fighterOf(*stats);
            fighter.boss = bossUnits.contains(field(parameter, unitIdOffset));
            replacement.push_back(fighter);
        }
        const std::vector<FFighter> scaled = scalingChoice() == 1 ? matchGroup(original, replacement) : scaleGroup(original, replacement);

        for (size_t index = 0; index < enemies.size(); ++index) writeFighter(enemies[index], scaled[index]);

        const double before = fightThreat(original);
        double after{};
        for (const FFighter& enemy : scaled) after += threat(enemy);
        const double strength = before > 0 ? after / before : 1.0;
        setBattleAp(group, replaced, scalingChoice() == 1 ? 1.0 : strength);
        const auto ap = groupAp.find(group);
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] group {} scaled ({}) to the group {} it replaced (table {}): level {}, strength x{:.2f}, HP {}, AP {}\n"),
                group, scalingChoices[scalingChoice()], replaced, nearbyTables.empty() ? StringType{} : nearbyTables.front(), scaled.front().level,
                strength, field(enemies.front(), hitPointOffset), ap == groupAp.end() ? -1 : *ap->second.second);
    }
}
