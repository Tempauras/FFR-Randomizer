#include "ScalingModel.h"

namespace ffr
{
    const FModel& modelOf(const FFighter& enemy)
    {
        return enemy.boss ? bossModel : monsterModel;
    }

    // A kind's curve at a level; past 52 the monster curve grows as the boss curve does.
    double curveAt(const FModel& model, int32 level, int32 stat)
    {
        const int32 at = std::clamp(level, 1, PartyMaxLevel);
        if (at <= model.maxLevel) return model.curve[at][stat];
        const double growth = BossStatCurve[model.maxLevel][stat] > 0 ? BossStatCurve[at][stat] / BossStatCurve[model.maxLevel][stat] : 1.0;
        return model.curve[model.maxLevel][stat] * growth;
    }

    // The game's damage curve on attack / defence (Attack vs Defence, Intelligence vs Mind).
    double damageCurve(double ratio)
    {
        if (ratio < 0.5) return 0.5 * ratio;
        if (ratio < 1.0) return ratio * ratio;
        return 8.0 / (1.0 + std::exp(1.0 - ratio)) - 3.0;
    }

    int32 partyLevel(int32 level)
    {
        return std::clamp(level, 1, PartyMaxLevel);
    }

    // Damage per round this enemy deals with one stat to the reference party at its level.
    double pressure(const FFighter& enemy, Stat stat, PartyStat guard)
    {
        if (enemy.stats[stat] <= 0) return 0;
        double sum{};
        for (const auto& member : ReferenceParty[partyLevel(enemy.level)])
        {
            sum += damageCurve(enemy.stats[stat] / std::max(member[guard], 1.0));
        }
        return enemy.stats[agility] * (enemy.level + 2) * sum / std::size(ReferenceParty[0]);
    }

    // Rounds the reference party needs to kill this enemy.
    double endurance(const FFighter& enemy)
    {
        const int32 level = partyLevel(enemy.level);
        double damage{};
        for (const auto& member : ReferenceParty[level])
        {
            damage += member[partyAgility] * (level + 2) * damageCurve(member[partyAttack] / std::max(enemy.stats[defence], 1.0));
        }
        return damage > 0 ? enemy.stats[hitPoint] / damage : 0;
    }

    // Damage the party takes over the whole fight with this enemy.
    double threat(const FFighter& enemy)
    {
        return (pressure(enemy, attack, partyDefence) + pressure(enemy, intelligence, partyMind)) * endurance(enemy);
    }

    // A kind's typical threat at a level; past the monsters' last level it grows as the typical boss's does.
    double typicalThreat(const FModel& model, int32 level)
    {
        const int32 at = std::clamp(level, 1, PartyMaxLevel);
        if (at <= model.maxLevel) return model.typicalThreat[at];
        return model.typicalThreat[model.maxLevel] * TypicalBossThreat[at] / TypicalBossThreat[model.maxLevel];
    }

    FFighter fighterOf(const FUnitStats& unit)
    {
        return {unit.level, {double(unit.hitPoint), double(unit.breakPoint), double(unit.attack), double(unit.defence), double(unit.intelligence),
                             double(unit.mind), double(unit.agility), double(unit.experience), double(unit.money)}};
    }

    const FUnitStats* poolUnit(int32 id)
    {
        static const std::unordered_map<int32, const FUnitStats*> byId = [] {
            std::unordered_map<int32, const FUnitStats*> map;
            for (const FUnitStats& unit : PoolUnits) map[unit.id] = &unit;
            return map;
        }();
        const auto found = byId.find(id);
        return found == byId.end() ? nullptr : found->second;
    }

    // The range a combat stat may take at a level: around the typical fighter's of its kind.
    std::pair<double, double> statRange(const FModel& model, Stat stat, int32 level)
    {
        const double typical = curveAt(model, level, stat);
        return {typical / model.spread, typical * model.spread};
    }

    // Moves a fighter to a level along its kind's stat curve, so it stays as strong for that level as it was for its own.
    FFighter moveTo(const FFighter& enemy, int32 level)
    {
        const FModel& model = modelOf(enemy);
        FFighter moved{level};
        moved.boss = enemy.boss;
        for (int32 stat = 0; stat < statCount; ++stat)
        {
            const double from = curveAt(model, enemy.level, stat);
            moved.stats[stat] = from > 0 ? enemy.stats[stat] * curveAt(model, level, stat) / from : enemy.stats[stat];
        }
        for (const Stat stat : {attack, defence, intelligence, mind, agility})
        {
            const auto [low, high] = statRange(model, stat, level);
            if (moved.stats[stat] > 0) moved.stats[stat] = std::clamp(moved.stats[stat], low, high);
        }
        return moved;
    }

    // Brings an enemy's threat to goal: half the change through its damage, the rest through its HP.
    void reach(FFighter& enemy, double goal)
    {
        const double now = threat(enemy);
        if (now <= 0 || goal <= 0) return;
        const double damageChange = std::sqrt(goal / now);
        for (const auto [stat, guard] : {std::pair{attack, partyDefence}, std::pair{intelligence, partyMind}})
        {
            const double base = enemy.stats[stat];
            if (base <= 0) continue;
            const double target = pressure(enemy, stat, guard) * damageChange;
            const auto [lowest, highest] = statRange(modelOf(enemy), stat, enemy.level);
            double low = std::min(1.0, lowest / base), high = std::max(1.0, highest / base);
            for (int32 step = 0; step < 60; ++step)
            {
                const double middle = (low + high) / 2;
                enemy.stats[stat] = base * middle;
                (pressure(enemy, stat, guard) < target ? low : high) = middle;
            }
            enemy.stats[stat] = base * (low + high) / 2;
        }
        const double afterDamage = threat(enemy);
        if (afterDamage > 0) enemy.stats[hitPoint] *= goal / afterDamage;
        const FModel& model = modelOf(enemy);
        const double typicalHitPoint = curveAt(model, enemy.level, hitPoint);
        enemy.stats[hitPoint] = std::clamp(enemy.stats[hitPoint], typicalHitPoint / model.hitPointSpread, typicalHitPoint * model.hitPointSpread);
    }

    // Squeezes a replacement's strength vs the original into weakestFight-strongestFight; a stronger one always stays stronger.
    double fightStrength(double ratio)
    {
        const double x = std::log(ratio);
        if (x <= 0) return 1.0 - (1.0 - weakestFight) * std::tanh(-x);
        const double top = std::log(strongestFight);
        return std::exp(top * std::tanh(x / (2 * top)));
    }

    // A fighter's strength for its level next to a typical one of its kind, kept within its kind's limits.
    double keptStrength(const FFighter& enemy)
    {
        const FModel& model = modelOf(enemy);
        const double own = threat(enemy) / typicalThreat(model, enemy.level);
        return own > 0 ? std::clamp(own, model.weakest, model.strongest) : 1.0;
    }

    // Threat with the kept strength, so gimmick monsters (metal ones, pots) count as ordinary fights.
    double fairThreat(const FFighter& enemy)
    {
        return keptStrength(enemy) * typicalThreat(modelOf(enemy), enemy.level);
    }

    double fightThreat(const std::vector<FFighter>& group)
    {
        double total{};
        for (const FFighter& enemy : group) total += fairThreat(enemy);
        return total;
    }

    // Replaced stats: one factor per stat for the whole group. HP, Break, EXP, gil, Attack and Intelligence
    // match the old group's totals; Defence, Mind and Agility its averages; level its average level.
    std::vector<FFighter> matchGroup(const std::vector<FFighter>& original, const std::vector<FFighter>& replacement)
    {
        double levels{};
        for (const FFighter& enemy : original) levels += enemy.level;
        const int32 level = static_cast<int32>(std::lround(levels / original.size()));

        std::vector<FFighter> matched = replacement;
        for (int32 stat = 0; stat < statCount; ++stat)
        {
            double originalTotal{}, replacementTotal{};
            for (const FFighter& enemy : original) originalTotal += enemy.stats[stat];
            for (const FFighter& enemy : replacement) replacementTotal += enemy.stats[stat];
            if (replacementTotal <= 0) continue;
            const bool byAverage = stat == defence || stat == mind || stat == agility;
            const double factor = byAverage ? (originalTotal / original.size()) / (replacementTotal / replacement.size()) : originalTotal / replacementTotal;
            for (FFighter& enemy : matched) enemy.stats[stat] *= factor;
        }
        for (FFighter& enemy : matched) enemy.level = level;
        return matched;
    }

    // The replacement group at the original group's level, as strong as fightStrength allows.
    std::vector<FFighter> scaleGroup(const std::vector<FFighter>& original, const std::vector<FFighter>& replacement)
    {
        double levels{};
        for (const FFighter& enemy : original) levels += enemy.level;
        const double originalThreat = fightThreat(original);
        const int32 level = static_cast<int32>(std::lround(levels / original.size()));

        std::vector<FFighter> scaled;
        double scaledThreat{};
        for (const FFighter& enemy : replacement)
        {
            FFighter moved = moveTo(enemy, level);
            reach(moved, keptStrength(enemy) * typicalThreat(modelOf(enemy), level));
            scaledThreat += threat(moved);
            scaled.push_back(moved);
        }
        if (originalThreat <= 0 || scaledThreat <= 0) return scaled;

        const double strength = fightStrength(scaledThreat / originalThreat);
        for (FFighter& enemy : scaled) reach(enemy, threat(enemy) * strength * originalThreat / scaledThreat);

        // Rewards: the original fight's, times how much stronger this one is, shared like the monsters' own.
        for (const Stat reward : {experience, money})
        {
            double originalTotal{}, scaledTotal{};
            for (const FFighter& enemy : original) originalTotal += enemy.stats[reward];
            for (const FFighter& enemy : scaled) scaledTotal += enemy.stats[reward];
            for (FFighter& enemy : scaled)
            {
                const double share = scaledTotal > 0 ? enemy.stats[reward] / scaledTotal : 1.0 / scaled.size();
                enemy.stats[reward] = originalTotal * strength * share;
            }
        }
        return scaled;
    }
}
