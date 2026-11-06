#include "FFRRandomizer.h"

// Chest - Drops/Steal - Vision - Shop are all randomized by themselves but kept separated for now

//TODO randomize them together -> Add key items (?) -> logic -> Make settings to maybe guarantee some items early
//wouldnt want to play the game with 0 vision for the first 5 chapters, could be fun tho

namespace ffr
{
    // Data table, if composite table also every parent it copies from
    auto FFRRandomizer::tableFamily(UDataTable* root) -> std::vector<FWeakObjectPtr>
    {
        std::vector<FWeakObjectPtr> family;
        std::vector<UObject*> pending{root};
        while (!pending.empty())
        {
            UObject* dataTable = pending.back();
            pending.pop_back();
            if (!dataTable || std::any_of(family.begin(), family.end(), [dataTable](FWeakObjectPtr& known) { return known.Get() == dataTable; })) continue;
            dataTable->SetRootSet();
            family.emplace_back(dataTable);
            auto* parents = CastField<FArrayProperty>(dataTable->GetPropertyByNameInChain(STR("ParentTables")));
            if (!parents) continue;
            auto* list = parents->ContainerPtrToValuePtr<FScriptArray>(dataTable);
            auto* first = static_cast<UObject**>(list->GetData());
            pending.insert(pending.end(), first, first + list->Num());
        }
        return family;
    }

    auto FFRRandomizer::loadFamily(TableFamily family, const CharType* path) -> UDataTable*
    {
        auto* dataTable = static_cast<UDataTable*>(loadByPath(path));
        if (!dataTable || !dataTable->GetRowStruct())
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] could not load {}\n"), path);
            return nullptr;
        }
        families[family] = tableFamily(dataTable);
        return dataTable;
    }

    auto FFRRandomizer::canMove(int32 item) const -> bool
    {
        const auto found = itemKinds.find(item);
        return found != itemKinds.end() && canShuffle(found->second) && !questItems.contains(item);
    }

    // Read items which we can shuffle and every slot that is considered shuffable aka Chest/Drops/Steals/Shops/Vision
    auto FFRRandomizer::loadItemTables() -> void
    {
        for (const auto& [kind, path] : itemTables)
        {
            auto* dataTable = static_cast<UDataTable*>(loadByPath(path));
            UScriptStruct* row = dataTable ? dataTable->GetRowStruct() : nullptr;
            FProperty* id = row ? row->GetPropertyByNameInChain(STR("ID")) : nullptr;
            if (!id)
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] could not load {}\n"), path);
                return;
            }
            for (auto& pair : dataTable->GetRowMap()) itemKinds[static_cast<int32>(readNumber(id, pair.Value))] = kind;
        }

        UDataTable* chestRows = loadFamily(chests, chestTable);
        UDataTable* unitRows = loadFamily(units, unitTable);
        UDataTable* shopRows = loadFamily(shops, shopTable);
        UDataTable* eventRows = loadFamily(events, eventTable);
        auto* questRows = static_cast<UDataTable*>(loadByPath(questTable));

        // Add items that events/quests require -- If this fail stop every shuffle
        const auto addQuestItems = [this](UDataTable* dataTable, const CharType* arrayName) {
            const FArrayField list = dataTable ? arrayField(dataTable->GetRowStruct(), arrayName, STR("ID")) : FArrayField{};
            if (!list.array) return false;
            for (auto& pair : dataTable->GetRowMap())
            {
                const auto* entries = list.array->ContainerPtrToValuePtr<FScriptArray>(pair.Value);
                for (int32 index = 0; index < entries->Num(); ++index)
                {
                    const int32 item = static_cast<int32>(readNumber(list.field, static_cast<const uint8*>(entries->GetData()) + index * list.stride));
                    if (item > 0) questItems.insert(item);
                }
            }
            return true;
        };
        if (!addQuestItems(eventRows, STR("ConsumeItemList")) || !addQuestItems(questRows, STR("ClearConditionList")))
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] quest and event items could not be read: items stay in place\n"));
            return;
        }

        // All items/gil present in chest minus key items
        if (chestRows)
        {
            UScriptStruct* chest = chestRows->GetRowStruct();
            FProperty* item = chest->GetPropertyByNameInChain(STR("ItemId"));
            FProperty* count = chest->GetPropertyByNameInChain(STR("ItemNum"));
            FProperty* money = chest->GetPropertyByNameInChain(STR("Money"));
            if (item && count && money && item->GetSize() == 4 && count->GetSize() == 4 && money->GetSize() == 4)
            {
                for (const auto& [name, row] : sortedRows(chestRows))
                {
                    const int32 itemId = static_cast<int32>(readNumber(item, row));
                    const int32 gil = static_cast<int32>(readNumber(money, row));
                    if (itemId > 0 ? !canMove(itemId) : gil <= 0) continue;
                    chestRewards.push_back({{chests, name, item->GetOffset_Internal()}, {chests, name, count->GetOffset_Internal()},
                                            {chests, name, money->GetOffset_Internal()}, itemId, static_cast<int32>(readNumber(count, row)), gil});
                }
            }
        }

        // Drop/Steal of every enemy and boss
        if (unitRows)
        {
            UScriptStruct* unit = unitRows->GetRowStruct();
            FProperty* category = unit->GetPropertyByNameInChain(STR("Category"));
            for (const auto& [name, row] : sortedRows(unitRows))
            {
                const int64_t kind = readNumber(category, row);
                if (kind != enemyCategory && kind != bossCategory) continue;
                for (const CharType* fieldName : unitItemFields)
                {
                    FProperty* field = unit->GetPropertyByNameInChain(fieldName);
                    if (!field || field->GetSize() != 4) continue;
                    const int32 original = static_cast<int32>(readNumber(field, row));
                    if (original <= 0 || !canMove(original)) continue;
                    enemyItems.push_back({{units, name, field->GetOffset_Internal()}, original});
                    enemyItemIsSteal.push_back(StringViewType(fieldName).starts_with(STR("Steal")));
                }
            }
        }

        // every slot that isn't selling a key items (I dont even remember if theres one, maybe?) 
        // also make sure shop don't sell the same item twice -- maybe change in a full random version?
        if (const FArrayField list = shopRows ? arrayField(shopRows->GetRowStruct(), STR("ItemList"), STR("ItemId")) : FArrayField{}; list.array)
        {
            for (const auto& [name, row] : sortedRows(shopRows))
            {
                const size_t shop = shopFixed.size();
                auto& fixed = shopFixed.emplace_back();
                const auto* entries = list.array->ContainerPtrToValuePtr<FScriptArray>(row);
                for (int32 index = 0; index < entries->Num(); ++index)
                {
                    const int32 original = static_cast<int32>(readNumber(list.field, static_cast<const uint8*>(entries->GetData()) + index * list.stride));
                    if (original <= 0) continue;
                    if (!canMove(original))
                    {
                        fixed.insert(original);
                        continue;
                    }
                    shopStock.push_back({{{shops, name, list.field->GetOffset_Internal(), list.array->GetOffset_Internal(), list.stride, index}, original}, shop});
                }
            }
        }

        // visions given by event 
        // _Kari = placeholder (仮) so we don't count them to hand out as we'd have more vision to hand out than the real number of vision
        if (const FArrayField list = eventRows ? arrayField(eventRows->GetRowStruct(), STR("ObtainItemList"), STR("ID")) : FArrayField{}; list.array)
        {
            for (const auto& [name, row] : sortedRows(eventRows))
            {
                if (name == STR("VisionGetTemplate")) continue;
                const auto* entries = list.array->ContainerPtrToValuePtr<FScriptArray>(row);
                for (int32 index = 0; index < entries->Num(); ++index)
                {
                    const int32 original = static_cast<int32>(readNumber(list.field, static_cast<const uint8*>(entries->GetData()) + index * list.stride));
                    const auto found = itemKinds.find(original);
                    if (found == itemKinds.end() || found->second != ItemKind::vision) continue;
                    const FSlot slot{{events, name, list.field->GetOffset_Internal(), list.array->GetOffset_Internal(), list.stride, index}, original};
                    (name.ends_with(STR("_Kari")) ? visionDrafts : visionSlots).push_back(slot);
                }
            }
            if (visionSlots.size() != visionCount)
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] found {} Vision rewards instead of {}: Visions stay in place\n"), visionSlots.size(), visionCount);
                visionSlots.clear();
                visionDrafts.clear();
            }
            // If shrine swap was enabled, sephiroth vision isn't shuffled and stay behind its event (we randomize its event location instead)
            if (shrineSwapEnabled) std::erase_if(visionSlots, [](const FSlot& slot) { return slot.field.row == sephirothEnd; });
        }
        if (shrineSwapEnabled && eventRows) readShrineTables(eventRows);
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] {} chests, {} enemy drops and steals, {} shop slots, {} Visions\n"),
                chestRewards.size(), enemyItems.size(), shopStock.size(), visionSlots.size());
    }

    // Build the list of now randomized item for applyItems - No item creation only shuffling
    auto FFRRandomizer::itemWrites() -> std::vector<std::pair<const FField*, int32>>
    {
        std::vector<std::pair<const FField*, int32>> writes;

        std::vector<size_t> order(chestRewards.size());
        for (size_t index = 0; index < order.size(); ++index) order[index] = index;
        std::mt19937 chestRng{seed ^ chestStream};
        if (isOn(chestsOption)) shuffleWith(order, chestRng);
        for (size_t index = 0; index < chestRewards.size(); ++index)
        {
            const FChest& place = chestRewards[index];
            const FChest& reward = chestRewards[order[index]];
            writes.emplace_back(&place.item, reward.itemId);
            writes.emplace_back(&place.count, reward.itemCount);
            writes.emplace_back(&place.money, reward.gil);
        }

        // Enemy loot and steal either randomize by themselves or between each other depending on options
        std::vector<int32> loot;
        for (const FSlot& slot : enemyItems) loot.push_back(slot.original);
        std::mt19937 lootRng{seed ^ lootStream};
        const auto shuffleKind = [&](bool steals) {
            std::vector<size_t> places;
            for (size_t index = 0; index < enemyItems.size(); ++index)
                if (enemyItemIsSteal[index] == steals) places.push_back(index);
            std::vector<int32> kind;
            for (const size_t index : places) 
                kind.push_back(loot[index]);
            shuffleWith(kind, lootRng);
            for (size_t index = 0; index < places.size(); ++index) 
                loot[places[index]] = kind[index];
        };
        if (isOn(lootOption))
        {
            switch (lootMode())
            {
            case lootMixed: shuffleWith(loot, lootRng); break;
            case lootBoth: shuffleKind(false); shuffleKind(true); break;
            case lootDrops: shuffleKind(false); break;
            case lootSteals: shuffleKind(true); break;
            }
        }
        for (size_t index = 0; index < enemyItems.size(); ++index) writes.emplace_back(&enemyItems[index].field, loot[index]);

        std::vector<int32> stock;
        for (const auto& [slot, shop] : shopStock) stock.push_back(slot.original);
        std::mt19937 shopRng{seed ^ shopStream};
        if (isOn(shopsOption)) shuffleWith(stock, shopRng);
        const auto sells = [this, &stock](size_t shop, int32 item, size_t except) {
            if (shopFixed[shop].contains(item)) return true;
            for (size_t index = 0; index < shopStock.size(); ++index)
            {
                if (index != except && shopStock[index].second == shop && stock[index] == item) return true;
            }
            return false;
        };
        // if an item is already present in shop swap it with another shop, because a lot of shops are selling potion/ether etc
        if (isOn(shopsOption))
        {
            for (size_t index = 0; index < shopStock.size(); ++index)
            {
                const size_t shop = shopStock[index].second;
                if (!sells(shop, stock[index], index)) continue;
                for (size_t other = 0; other < shopStock.size(); ++other)
                {
                    const size_t otherShop = shopStock[other].second;
                    if (otherShop != shop && !sells(shop, stock[other], index) && !sells(otherShop, stock[index], other))
                    {
                        std::swap(stock[index], stock[other]);
                        break;
                    }
                }
            }
        }
        
        for (size_t index = 0; index < shopStock.size(); ++index) writes.emplace_back(&shopStock[index].first.field, stock[index]);

        // visions trade between each other
        std::vector<int32> visions;
        for (const FSlot& slot : visionSlots) visions.push_back(slot.original);
        std::mt19937 visionRng{seed ^ visionStream};
        if (isOn(visionsOption)) shuffleWith(visions, visionRng);
        std::unordered_map<int32, int32> visionFor;
        for (size_t index = 0; index < visionSlots.size(); ++index)
        {
            visionFor[visionSlots[index].original] = visions[index];
            writes.emplace_back(&visionSlots[index].field, visions[index]);
        }
        for (const FSlot& draft : visionDrafts)
        {
            if (const auto found = visionFor.find(draft.original); found != visionFor.end()) writes.emplace_back(&draft.field, found->second);
        }
        return writes;
    }

    // Writes the seed's items into every table of each family
    // Redone after each map load because its pretty cheap and while I haven't found data tables moving after a few reload, I didn't check *everything* (and cba checking everything)
    auto FFRRandomizer::applyItems() -> void
    {
        if (!loadedAllTables) return;
        itemsApplied = true;
        itemsSeed = seed;
        itemsOff = optionsOff;
        applyBosses();
        applyShrineSwap();
        if (chestRewards.empty() && enemyItems.empty() && shopStock.empty() && visionSlots.empty()) return;

        std::vector<std::vector<std::unordered_map<StringType, uint8*>>> rows(familyCount);
        for (int32 family = 0; family < familyCount; ++family)
        {
            for (FWeakObjectPtr& member : families[family])
            {
                auto* dataTable = static_cast<UDataTable*>(member.Get());
                if (!dataTable) continue;
                auto& byName = rows[family].emplace_back();
                for (auto& pair : dataTable->GetRowMap()) byName.emplace(pair.Key.ToString(), pair.Value);
            }
        }

        size_t written = 0;
        for (const auto& [field, value] : itemWrites())
        {
            for (const auto& byName : rows[field->family])
            {
                const auto found = byName.find(field->row);
                if (found == byName.end()) continue;
                uint8* target = found->second;
                if (field->arrayOffset >= 0)
                {
                    auto* list = reinterpret_cast<FScriptArray*>(target + field->arrayOffset);
                    if (field->index >= list->Num()) continue;
                    target = static_cast<uint8*>(list->GetData()) + field->index * field->stride;
                }
                *reinterpret_cast<int32*>(target + field->offset) = value;
                ++written;
            }
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] items, shops and Visions for seed {:08X}: {} fields written\n"), seed, written);
    }
}
