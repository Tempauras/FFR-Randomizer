#pragma once

#include "Constants.h"

// How strong a fight is, and how a fighter is moved to another level or strength.
namespace ffr
{
    // Same order as StatCurve's columns.
    enum Stat { hitPoint, breakPoint, attack, defence, intelligence, mind, agility, experience, money, statCount };
    // ReferenceParty's columns.
    enum PartyStat { partyAttack, partyDefence, partyIntelligence, partyMind, partyAgility };

    struct FFighter
    {
        int32 level{};
        double stats[statCount]{};
        // Scaled next to the typical boss rather than the typical monster.
        bool boss{};
    };

    // What a fighter is scaled next to: its kind's stat curve, typical threat and limits.
    struct FModel
    {
        const double (*curve)[statCount];
        const double* typicalThreat;
        int32 maxLevel;
        double weakest, strongest, spread, hitPointSpread;
    };
    constexpr FModel monsterModel{StatCurve, TypicalThreat, ScalingMaxLevel, weakestMonster, strongestMonster, statSpread, hitPointSpread};
    constexpr FModel bossModel{BossStatCurve, TypicalBossThreat, PartyMaxLevel, weakestBoss, strongestBoss, bossStatSpread, bossHitPointSpread};
    const FModel& modelOf(const FFighter& enemy);
    double curveAt(const FModel& model, int32 level, int32 stat);
    double damageCurve(double ratio);
    int32 partyLevel(int32 level);
    double pressure(const FFighter& enemy, Stat stat, PartyStat guard);
    double endurance(const FFighter& enemy);
    double threat(const FFighter& enemy);
    double typicalThreat(const FModel& model, int32 level);
    FFighter fighterOf(const FUnitStats& unit);
    const FUnitStats* poolUnit(int32 id);
    std::pair<double, double> statRange(const FModel& model, Stat stat, int32 level);
    FFighter moveTo(const FFighter& enemy, int32 level);
    void reach(FFighter& enemy, double goal);
    double fightStrength(double ratio);
    double keptStrength(const FFighter& enemy);
    double fairThreat(const FFighter& enemy);
    double fightThreat(const std::vector<FFighter>& group);
    std::vector<FFighter> matchGroup(const std::vector<FFighter>& original, const std::vector<FFighter>& replacement);
    std::vector<FFighter> scaleGroup(const std::vector<FFighter>& original, const std::vector<FFighter>& replacement);
}
