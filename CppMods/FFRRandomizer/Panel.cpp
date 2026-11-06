#include "FFRRandomizer.h"

// The seed panel: opening it, controller and mouse input, and drawing it.

namespace ffr
{
    auto FFRRandomizer::tickPanel(float deltaSeconds) -> void
    {
        panelClock += deltaSeconds;
        sincePanelSearch += deltaSeconds;
        UObject* widget = panel.Get();
        if (togglePanel.exchange(false))
        {
            if (widget && isShown(widget)) closePanel(widget);
            else openPanel();
            widget = panel.Get();
        }
        if (!widget)
        {
            if (sincePanelSearch < searchSeconds) return;
            sincePanelSearch = 0.0f;
            widget = findPanel();
            if (!widget) return;
            panel = widget;
            displayDirty = true;
        }

        // RequestedChoice is kind * 1000 + entry index + 1.
        if (auto* request = widget->GetValuePtrByPropertyNameInChain<int32>(STR("RequestedChoice")); request && *request > 0)
        {
            const int32 kind = (*request - 1) / 1000;
            const size_t chosen = static_cast<size_t>((*request - 1) % 1000);
            *request = 0;
            choose(kind, chosen);
        }
        if (isShown(widget)) tickController(widget);

        auto* request = widget->GetValuePtrByPropertyNameInChain<FString>(STR("RequestedSeed"));
        if (!request || request->Len() == 0) return;

        uint32_t parsed{};
        seedRejected = !textToSeed(StringViewType(**request, request->Len()), parsed);
        if (!seedRejected)
        {
            pendingSeed = parsed;
            hasPending = true;
            reconcile();
        }
        *request = FString(STR(""));
        displayDirty = true;
    }

    // A choice from the panel: Choose's kind and entry.
    auto FFRRandomizer::choose(int32 kind, size_t chosen) -> void
    {
        if ((kind == optionOnKind || kind == optionOffKind) && chosen < optionCount) setOption(static_cast<Option>(chosen), kind == optionOnKind);
        if (kind == lootKind && chosen < std::size(lootChoices)) setLootChoice(chosen);
        if (kind == scalingKind && chosen < std::size(scalingChoices)) setScalingChoice(chosen);
        if (kind == bossScalingKind && chosen < std::size(scalingChoices)) setScalingChoice(chosen, bossScalingOption);
        displayDirty = true;
    }

    // The panel's layout row: a copy of the Help screen's with the panel's tag and class.
    auto FFRRandomizer::addLayoutRows() -> void
    {
        for (size_t index = 0; index < std::size(layoutTables); ++index)
        {
            if (layoutRowAdded[index]) continue;
            auto* dataTable = UObjectGlobals::StaticFindObject<UDataTable*>(nullptr, nullptr, layoutTables[index]);
            UScriptStruct* row = dataTable ? static_cast<UScriptStruct*>(dataTable->GetRowStruct()) : nullptr;
            if (!row) continue;
            layoutRowAdded[index] = true;

            FProperty* tag = row->GetPropertyByNameInChain(STR("LayoutTag"));
            auto* data = CastField<FStructProperty>(row->GetPropertyByNameInChain(STR("LayoutData")));
            FProperty* widget = data ? static_cast<UScriptStruct*>(data->GetStruct())->GetPropertyByNameInChain(STR("LayoutWidget")) : nullptr;
            if (!tag || tag->GetSize() != sizeof(FName) || !widget || widget->GetSize() != softClassSize)
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] {} is not laid out as expected: the panel opens without it\n"), layoutTables[index]);
                continue;
            }
            const int32 widgetOffset = data->GetOffset_Internal() + widget->GetOffset_Internal();

            const uint8* source{};
            for (auto& pair : dataTable->GetRowMap())
            {
                FName rowTag = *reinterpret_cast<FName*>(pair.Value + tag->GetOffset_Internal());
                if (rowTag.ToString() == templateLayoutTag) source = pair.Value;
            }
            FName sourceClass = source ? reinterpret_cast<const FName*>(source + widgetOffset + sizeof(FWeakObjectPtr))[1] : FName{};
            if (!source || sourceClass.ToString() != templateClassName)
            {
                Output::send<LogLevel::Warning>(STR("[FFRRandomizer] {} has no Help screen row to copy: the panel opens without it\n"), layoutTables[index]);
                continue;
            }

            const FName rowName(panelRowName, FNAME_Add);
            dataTable->AddRow(rowName, source, row);
            uint8** added = dataTable->GetRowMap().Find(rowName);
            if (!added || !*added) continue;
            *reinterpret_cast<FName*>(*added + tag->GetOffset_Internal()) = FName(panelLayoutTag, FNAME_Add);
            writePanelClass(*added + widgetOffset);
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] the panel's layout row is in {}\n"), layoutTables[index]);
        }
    }

    // A soft reference to the panel's class, over the bytes of one.
    auto FFRRandomizer::writePanelClass(uint8* soft) -> void
    {
        *reinterpret_cast<FWeakObjectPtr*>(soft) = FWeakObjectPtr(loadPanelClass());
        auto* path = reinterpret_cast<FName*>(soft + sizeof(FWeakObjectPtr));
        path[0] = FName(panelPackage, FNAME_Add);
        path[1] = FName(panelClassName, FNAME_Add);
    }

    auto FFRRandomizer::isShown(UObject* widget) -> bool
    {
        return callInt(widget, STR("IsActivated")) == 1;
    }

    // The panel's class. Its pak sits in Paks/~mods, which the engine mounts but no mod loader spawns anything from,
    // so nothing else loads the class: it is loaded here the first time the panel opens.
    auto FFRRandomizer::loadPanelClass() -> UObject*
    {
        if (UObject* loaded = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, panelClassPath)) return loaded;
        UObject* loaded = loadByPath(panelClassPath);
        if (!loaded) Output::send<LogLevel::Warning>(STR("[FFRRandomizer] {} could not be loaded: is FFRRandomizer.pak in Content/Paks/~mods?\n"), panelClassPath);
        return loaded;
    }

    // Opens the panel as the game opens a screen: by its layout row, or straight onto the Help screen's layer.
    auto FFRRandomizer::openPanel() -> void
    {
        addLayoutRows();
        if (!loadPanelClass()) return;
        auto* manager = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, widgetManager);
        if (!manager)
        {
            Output::send<LogLevel::Warning>(STR("[FFRRandomizer] the game's widget manager is not loaded: the panel cannot open\n"));
            return;
        }
        const FName tag(panelLayoutTag, FNAME_Add);
        UObject* opened = objectFrom(callReturning(manager, STR("PushLayoutWidget"), [&tag](const auto& name, uint8* value) {
            if (name == STR("LayoutTag")) std::memcpy(value, &tag, sizeof(tag));
        }));
        panelByLayout = opened != nullptr;
        if (!opened)
        {
            const FName layer(panelLayer, FNAME_Add);
            opened = objectFrom(callReturning(manager, STR("PushWidgetToLayerStack"), [this, &layer](const auto& name, uint8* value) {
                if (name == STR("InLayerName")) std::memcpy(value, &layer, sizeof(layer));
                if (name == STR("InWidgetClass")) writePanelClass(value);
            }));
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] panel {}\n"),
                opened ? (panelByLayout ? STR("opened by its layout row") : STR("opened on the Help screen's layer")) : STR("did not open"));
        if (!opened) return;
        panel = opened;
        displayDirty = true;
    }

    auto FFRRandomizer::closePanel(UObject* widget) -> void
    {
        if (panelByLayout)
        {
            const FName tag(panelLayoutTag, FNAME_Add);
            callByName(UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, widgetManager), STR("RemoveLayoutWidget"), [&tag](const auto& name, uint8* value) {
                if (name == STR("InLayoutTag")) std::memcpy(value, &tag, sizeof(tag));
            });
        }
        if (isShown(widget)) callByName(widget, STR("DeactivateWidget"), [](auto&&, auto&&) {});
    }

    // Controller input: up/down pick a row, left/right change it, confirm steps through it, back closes
    // a list or the panel. Once the mouse moves, its own finger takes over.
    auto FFRRandomizer::tickController(UObject* widget) -> void
    {
        if (fingersFor.Get() != widget)
        {
            fingersFor = widget;
            fingerShown = -2;
        }

        const std::vector<uint8> mouse = callWithWorld(STR("/Script/UMG.Default__WidgetLayoutLibrary"), STR("GetMousePositionOnViewport"), widget);
        double at[2]{};
        if (mouse.size() == 2 * sizeof(double)) std::memcpy(at, mouse.data(), sizeof(at));
        else if (mouse.size() == 2 * sizeof(float)) for (int32 axis = 0; axis < 2; ++axis) at[axis] = reinterpret_cast<const float*>(mouse.data())[axis];
        if (hasMouse && (at[0] != lastMouse[0] || at[1] != lastMouse[1])) mouseMode = true;
        hasMouse = true;
        lastMouse[0] = at[0];
        lastMouse[1] = at[1];

        if (auto* pending = widget->GetValuePtrByPropertyNameInChain<FString>(STR("PendingInput")); pending && pending->Len() > 0)
        {
            const StringType codes(**pending, pending->Len());
            *pending = FString(STR(""));
            int32 code{};
            bool digits{};
            for (const CharType character : codes)
            {
                if (character >= STR('0') && character <= STR('9'))
                {
                    code = code * 10 + (character - STR('0'));
                    digits = true;
                    continue;
                }
                if (digits) controllerInput(widget, code);
                code = 0;
                digits = false;
            }
        }

        const int32 finger = mouseMode ? -1 : padRow;
        if (finger == fingerShown) return;
        fingerShown = finger;
        for (int32 row = 0; row < padRows; ++row)
        {
            const StringType name = std::format(STR("Option{}RowFinger"), row);
            if (auto* image = widget->GetValuePtrByPropertyNameInChain<UObject*>(name.c_str()); image && *image)
            {
                setWidgetVisibility(*image, row == finger ? passThroughVisibility : collapsedVisibility);
            }
        }
        if (auto* cursor = widget->GetValuePtrByPropertyNameInChain<UObject*>(STR("FingerCursor")); cursor && *cursor)
        {
            setWidgetVisibility(*cursor, mouseMode ? passThroughVisibility : collapsedVisibility);
        }
        if (auto* usingMouse = widget->GetValuePtrByPropertyNameInChain<bool>(STR("MouseMode"))) *usingMouse = mouseMode;
    }

    auto FFRRandomizer::controllerInput(UObject* widget, int32 code) -> void
    {
        const int32 group = code / 100;
        const int32 direction = (code / 10) % 10;
        const int32 type = code % 10;
        constexpr int32 inputsToLog = 40;
        if (inputsLogged < inputsToLog)
        {
            ++inputsLogged;
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] panel input {}\n"), code);
        }
        if (type >= inputReleased) return;
        // The game sends a mouse double-click as confirm: ignored while the mouse has the panel.
        if (group == inputDecide && mouseMode) return;
        const int32 key = group == inputDirection ? code / 10 : group * 10;
        if (const auto last = lastInputAt.find(key); last != lastInputAt.end() && panelClock - last->second < inputRepeatSeconds) return;
        lastInputAt[key] = panelClock;
        mouseMode = false;

        bool moved{};
        switch (group)
        {
        case inputDirection:
            if (direction == navUp || direction == navDown)
            {
                const int32 row = std::clamp(padRow + (direction == navUp ? -1 : 1), 0, padRows - 1);
                moved = row != padRow;
                padRow = row;
            }
            else if (direction == navLeft || direction == navRight) moved = stepRow(direction == navLeft ? -1 : 1, false);
            if (moved) callByName(widget, STR("SoundCursor"), [](auto&&, auto&&) {});
            break;
        case inputDecide:
            if (stepRow(1, true)) callByName(widget, STR("SoundDecide"), [](auto&&, auto&&) {});
            break;
        case inputCancel:
        case inputEscape:
            if (!closeMenus(widget)) closePanel(widget);
            break;
        default:
            break;
        }
        displayDirty = true;
    }

    // Steps the controller's row one choice: left/right held at the ends, confirm wraps.
    auto FFRRandomizer::stepRow(int32 step, bool wrap) -> bool
    {
        const auto next = [step, wrap](size_t current, size_t count) {
            return wrap && count > 0 && current < count ? (current + 1) % count : stepChoice(current, step, count);
        };
        if (padRow < optionCount)
        {
            const auto option = static_cast<Option>(padRow);
            if (!isAvailable(option)) return false;
            if (option == scalingOption || option == bossScalingOption)
            {
                const size_t current = scalingChoice(option);
                const size_t chosen = next(current, std::size(scalingChoices));
                if (chosen == current) return false;
                setScalingChoice(chosen, option);
                return true;
            }
            if (option == lootOption)
            {
                const size_t current = lootChoice();
                const size_t chosen = next(current, std::size(lootChoices));
                if (chosen == current) return false;
                setLootChoice(chosen);
                return true;
            }
            const bool on = wrap ? !isOn(option) : step < 0;
            if (isOn(option) == on) return false;
            setOption(option, on);
            return true;
        }
        return false;
    }

    // Closes the lists the mouse opened; whether any was open.
    auto FFRRandomizer::closeMenus(UObject* widget) -> bool
    {
        bool closed{};
        for (const CharType* name : panelMenus)
        {
            auto* menu = widget->GetValuePtrByPropertyNameInChain<UObject*>(name);
            if (!menu || !*menu || callInt(*menu, STR("IsVisible")) != 1) continue;
            setWidgetShown(*menu, false);
            closed = true;
        }
        return closed;
    }

    auto FFRRandomizer::findPanel() -> UObject*
    {
        std::vector<UObject*> found;
        UObjectGlobals::FindAllOf(STR("WBP_SeedPanel_C"), found);
        for (UObject* widget : found)
        {
            if (widget && !widget->IsUnreachable() && !widget->HasAnyFlags(RF_ClassDefaultObject)) return widget;
        }
        return nullptr;
    }

    auto FFRRandomizer::updatePanel() -> void
    {
        UObject* widget = panel.Get();
        if (!widget) return;
        UFunction* update = widget->GetFunctionByNameInChain(STR("UpdateDisplay"));
        if (!update) return;

        StringType seedText = hasSeed ? std::format(STR("{:08X}"), seed) : StringType(STR("--------"));
        if (seedRejected) seedText += STR("    not a seed: use 1 to 8 hexadecimal digits");

        FUpdateDisplayParams params{FString(seedText.c_str())};
        widget->ProcessEvent(update, &params);
        // The mark under each option's current choice.
        const auto showMark = [widget](const CharType* choice, int32 option, bool shown) {
            auto* mark = widget->GetValuePtrByPropertyNameInChain<UObject*>(std::format(STR("Option{}{}Mark"), choice, option).c_str());
            if (mark && *mark) setWidgetShown(*mark, shown);
        };
        for (int32 option = 0; option < optionCount; ++option)
        {
            if (option == lootOption)
            {
                showDropdown(widget, STR("Loot"), lootChoices, lootChoice());
                continue;
            }
            if (option == scalingOption || option == bossScalingOption)
            {
                showDropdown(widget, option == scalingOption ? STR("Scaling") : STR("BossScaling"), scalingChoices, scalingChoice(static_cast<Option>(option)));
                continue;
            }
            const bool on = isOn(static_cast<Option>(option));
            showMark(STR("On"), option, on);
            showMark(STR("Off"), option, !on);
        }
        // A scaling row is grayed out and locked while its battles are off, its list closed.
        for (const Option option : {scalingOption, bossScalingOption})
        {
            const bool available = isAvailable(option);
            auto* row = widget->GetValuePtrByPropertyNameInChain<UObject*>(std::format(STR("Option{}Row"), static_cast<int32>(option)).c_str());
            if (!row || !*row) continue;
            callByName(*row, STR("SetIsEnabled"), [available](const auto& name, uint8* value) {
                if (name == STR("bInIsEnabled")) *reinterpret_cast<bool*>(value) = available;
            });
            callByName(*row, STR("SetRenderOpacity"), [available](const auto& name, uint8* value) {
                if (name == STR("InOpacity")) *reinterpret_cast<float*>(value) = available ? 1.0f : grayedOpacity;
            });
            if (available) continue;
            const CharType* menu = option == scalingOption ? STR("ScalingMenu") : STR("BossScalingMenu");
            if (auto* list = widget->GetValuePtrByPropertyNameInChain<UObject*>(menu); list && *list) setWidgetShown(*list, false);
        }
        displayDirty = false;
    }
}
