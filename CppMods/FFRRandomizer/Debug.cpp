#include "FFRRandomizer.h"

// F7: logs the loaded maps' event actors.

namespace ffr
{
    // F7: logs the event settings of the loaded maps' actors (debug only).
    auto FFRRandomizer::dumpMapEvents() -> void
    {
        std::vector<UObject*> actors;
        UObjectGlobals::ForEachUObject([&actors](UObject* object, int32, int32) {
            if (isSettledActor(object)) actors.push_back(object);
            return LoopAction::Continue;
        });

        size_t shown{};
        for (UObject* actor : actors)
        {
            std::vector<StringType> lines;
            describeObject(actor, StringType{}, 0, lines);
            const bool hasEvents = std::any_of(lines.begin(), lines.end(), [](const StringType& line) {
                return line.find(STR("Event")) != StringType::npos || line.find(STR("{flag")) != StringType::npos;
            });
            if (!hasEvents) continue;
            ++shown;

            double at[3]{};
            const std::vector<uint8> location = callReturning(actor, STR("K2_GetActorLocation"), [](auto&&, auto&&) {});
            readVector(location.data(), static_cast<int32>(location.size()), at);
            UObject* level = actor->GetOuterPrivate();
            UObject* world = level ? level->GetOuterPrivate() : nullptr;
            Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] map events: {} ({}) in {} at ({:.0f}, {:.0f}, {:.0f})\n"), actor->GetName(),
                    actor->GetClassPrivate()->GetName(), world ? world->GetName() : StringType(STR("?")), at[0], at[1], at[2]);
            for (const StringType& line : lines) Output::send<LogLevel::Verbose>(STR("[FFRRandomizer]     {}\n"), line);
        }
        Output::send<LogLevel::Verbose>(STR("[FFRRandomizer] map events: {} of {} actors hold event settings\n"), shown, actors.size());
    }

    // An object's names, texts and numbers, by path; and those of the components it owns.
    auto FFRRandomizer::describeObject(UObject* object, const StringType& prefix, int32 depth, std::vector<StringType>& lines) -> void
    {
        for (FProperty* property : TFieldRange<FProperty>(object->GetClassPrivate(), EFieldIterationFlags::IncludeSuper))
        {
            const StringType path = prefix.empty() ? property->GetName() : prefix + STR(".") + property->GetName();
            describeValue(object, property, reinterpret_cast<const uint8*>(object) + property->GetOffset_Internal(), path, depth, lines);
        }
    }

    auto FFRRandomizer::describeValue(UObject* owner, FProperty* property, const uint8* value, const StringType& path, int32 depth, std::vector<StringType>& lines) -> void
    {
        constexpr int32 maxDepth = 8;
        constexpr size_t maxLines = 200;
        constexpr int32 maxItems = 32;
        if (depth > maxDepth || lines.size() >= maxLines) return;
        if (auto* structure = CastField<FStructProperty>(property))
        {
            auto* inner = static_cast<UScriptStruct*>(structure->GetStruct());
            if (!inner) return;
            for (FProperty* field : TFieldRange<FProperty>(inner, EFieldIterationFlags::IncludeSuper))
            {
                describeValue(owner, field, value + field->GetOffset_Internal(), path + STR(".") + field->GetName(), depth + 1, lines);
            }
            return;
        }
        if (auto* array = CastField<FArrayProperty>(property))
        {
            FProperty* inner = array->GetInner();
            const auto* items = reinterpret_cast<const FScriptArray*>(value);
            if (!inner || !sensibleArray(items)) return;
            for (int32 index = 0; index < std::min(items->Num(), maxItems); ++index)
            {
                describeValue(owner, inner, static_cast<const uint8*>(items->GetData()) + index * inner->GetSize(), std::format(STR("{}[{}]"), path, index), depth + 1, lines);
            }
            return;
        }

        const StringType type = property->GetClass().GetFName().ToString();
        if (type == STR("NameProperty"))
        {
            FName name = *reinterpret_cast<const FName*>(value);
            const StringType text = name.ToString();
            if (text != STR("None")) lines.push_back(path + STR(" = ") + text);
        }
        else if (type == STR("StrProperty"))
        {
            const auto* text = reinterpret_cast<const FString*>(value);
            if (text->Len() > 0) lines.push_back(path + STR(" = ") + StringType(text->GetCharArray().GetData(), text->Len()));
        }
        else if (type == STR("IntProperty"))
        {
            const int32 number = *reinterpret_cast<const int32*>(value);
            if (number != 0) lines.push_back(std::format(STR("{} = {}"), path, number));
        }
        else if (type == STR("ObjectProperty") && depth == 0)
        {
            // A component the actor owns: its settings are the actor's.
            UObject* component = *reinterpret_cast<UObject* const*>(value);
            if (component && component != owner && component->GetOuterPrivate() == owner) describeObject(component, path, depth + 1, lines);
        }
    }
}
