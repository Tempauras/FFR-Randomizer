#include "FFRRandomizer.h"

// Write seed and randomizer option to the save file 

namespace ffr
{
    auto FFRRandomizer::isOn(Option option) const -> bool
    {
        return !(optionsOff & optionMask(option));
    }

    auto FFRRandomizer::lootMode() const -> LootMode
    {
        return static_cast<LootMode>((optionsOff >> lootModeShift) & 3u);
    }

    auto FFRRandomizer::isAvailable(Option option) const -> bool
    {
        const int32 battles = scaledBattlesOf(option);
        return battles < 0 || isOn(static_cast<Option>(battles));
    }

    auto FFRRandomizer::scalingChoice(Option option) const -> size_t
    {
        if (!isOn(option)) return 0;
        return (optionsOff & (1u << copyBitOf(option))) ? 1 : 2;
    }

    auto FFRRandomizer::lootChoice() const -> size_t
    {
        if (!isOn(lootOption)) return 0;
        for (size_t choice = 1; choice < std::size(lootChoices); ++choice)
        {
            if (lootModeOf[choice] == lootMode()) return choice;
        }
        return 0;
    }

    auto FFRRandomizer::setOption(Option option, bool on) -> void
    {
        if (isOn(option) == on) return;
        optionsOff ^= optionMask(option);
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] {} {}\n"), optionNames[option], isOn(option) ? STR("on") : STR("off"));
        storeOptions();
    }

    auto FFRRandomizer::setLootChoice(size_t choice) -> void
    {
        if (choice == lootChoice()) return;
        optionsOff &= ~(3u << lootModeShift);
        optionsOff |= static_cast<uint32_t>(lootModeOf[choice]) << lootModeShift;
        if (choice == 0) optionsOff |= optionMask(lootOption);
        else optionsOff &= ~optionMask(lootOption);
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] {}: {}\n"), optionNames[lootOption], lootChoices[choice]);
        storeOptions();
    }

    auto FFRRandomizer::setScalingChoice(size_t choice, Option option) -> void
    {
        if (choice == scalingChoice(option)) return;
        optionsOff &= ~(optionMask(option) | (1u << copyBitOf(option)));
        if (choice == 0) optionsOff |= optionMask(option);
        if (choice == 1) optionsOff |= 1u << copyBitOf(option);
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] {}: {}\n"), optionNames[option], scalingChoices[choice]);
        storeOptions();
    }

    auto FFRRandomizer::storeOptions() -> void
    {
        if (uint8* flags = saveFlags(); flags && readBits(flags, tagFlag) == tag)
        {
            for (int32 bit = 0; bit < optionBits; ++bit) flags[optionFlagOf(bit)] = (optionsOff >> bit) & 1u;
        }
        displayDirty = true;
        reconcile();
    }

    // Brings the tables and live encounters in line with the save's seed
    auto FFRRandomizer::reconcile() -> void
    {
        uint8* flags = saveFlags();
        if (!flags) return;

        uint32_t want = seed;
        const uint32_t storedTag = readBits(flags, tagFlag);
        const uint32_t storedSeed = readBits(flags, seedFlag);
        const bool unclaimed = storedTag == 0 && storedSeed == 0 && optionFlagsClear(flags);
        if (hasPending)
        {
            if (storedTag == tag || unclaimed)
            {
                writeBits(flags, tagFlag, tag);
                writeBits(flags, seedFlag, pendingSeed);
            }
            want = pendingSeed;
            hasPending = false;
        }
        else if (storedTag == tag)
        {
            want = storedSeed;
        }
        else if (unclaimed)
        {
            want = std::random_device{}();
            writeBits(flags, tagFlag, tag);
            writeBits(flags, seedFlag, want);
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] new seed {:08X} written to this save\n"), want);
        }
        else
        {
            // if something else uses these flags (shouldnt happen but who knows) keep the seed in memory only
            if (!loggedConflict)
            {
                loggedConflict = true;
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] flags {}-{} are in use, the seed is not kept in this save\n"), moreOptionsFlag, flagCount - 1);
            }
            if (!hasSeed) want = std::random_device{}();
        }

        if (readBits(flags, tagFlag) == tag)
        {
            uint32_t stored{};
            for (int32 bit = 0; bit < optionBits; ++bit) stored |= static_cast<uint32_t>(flags[optionFlagOf(bit)] != 0) << bit;
            if (stored != optionsOff)
            {
                optionsOff = stored;
                displayDirty = true;
            }
        }

        if (!hasSeed || want != seed)
        {
            seed = want;
            hasSeed = true;
            displayDirty = true;
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] seed {:08X}\n"), seed);
        }
        if (applyEncounterTables())
        {
            patchLiveEncounters();
            displayDirty = true;
        }
        if (!itemsApplied || itemsSeed != seed || itemsOff != optionsOff) applyItems();
    }

    // null on title screen
    auto FFRRandomizer::saveFlags() -> uint8*
    {
        UObject* manager = saveManager.Get();
        if (!manager)
        {
            if (sinceSearch < searchSeconds) return nullptr;
            sinceSearch = 0.0f;
            std::vector<UObject*> found;
            UObjectGlobals::FindAllOf(STR("CPP_MuchaSaveDataManager"), found);
            for (UObject* object : found)
            {
                if (object && !object->IsUnreachable() && !object->HasAnyFlags(RF_ClassDefaultObject)) manager = object;
            }
            if (!manager) return nullptr;
            saveManager = manager;
        }

        auto* saveData = manager->GetValuePtrByPropertyNameInChain<UObject*>(STR("m_pGameSaveData"));
        if (!saveData || !*saveData) return nullptr;
        auto* progress = CastField<FStructProperty>((*saveData)->GetPropertyByNameInChain(STR("Progress")));
        if (!progress) return nullptr;
        auto* progressData = progress->ContainerPtrToValuePtr<uint8>(*saveData);

        // Save object is an empty placeholder on title screen so we check to make sure we don't consider that a save we have to write data to
        auto* story = CastField<FArrayProperty>(progress->GetStruct()->GetPropertyByNameInChain(STR("Progress")));
        if (!story) return nullptr;
        auto* storyArray = story->ContainerPtrToValuePtr<FScriptArray>(progressData);
        if (storyArray->Num() < 1 || static_cast<int32*>(storyArray->GetData())[0] <= 0) return nullptr;

        auto* eventFlag = CastField<FArrayProperty>(progress->GetStruct()->GetPropertyByNameInChain(STR("eventFlag")));
        if (!eventFlag) return nullptr;
        auto* flagArray = eventFlag->ContainerPtrToValuePtr<FScriptArray>(progressData);
        if (flagArray->Num() < flagCount || eventFlag->GetInner()->GetElementSize() != 1) return nullptr;
        return static_cast<uint8*>(flagArray->GetData());
    }
}
