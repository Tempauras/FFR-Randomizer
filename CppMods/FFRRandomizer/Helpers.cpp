#include "Helpers.h"

namespace ffr
{
    bool isNormalGroup(int32 group)
    {
        return std::find(std::begin(BattleGroupPool), std::end(BattleGroupPool), group) != std::end(BattleGroupPool);
    }

    std::vector<int32> shuffledPool(uint32_t seed)
    {
        std::vector<int32> pool(std::begin(BattleGroupPool), std::end(BattleGroupPool));
        std::mt19937 rng{seed};
        shuffleWith(pool, rng);
        return pool;
    }

    int64_t readNumber(FProperty* property, const uint8* container)
    {
        if (!property) return 0;
        if (auto* flag = CastField<FBoolProperty>(property)) return flag->GetPropertyValueInContainer(container) ? 1 : 0;
        const uint8* value = container + property->GetOffset_Internal();
        switch (property->GetSize())
        {
        case 1: return *value;
        case 2: return *reinterpret_cast<const int16*>(value);
        case 4: return *reinterpret_cast<const int32*>(value);
        case 8: return *reinterpret_cast<const int64_t*>(value);
        default: return 0;
        }
    }

    std::vector<std::pair<StringType, uint8*>> sortedRows(UDataTable* dataTable)
    {
        std::vector<std::pair<StringType, uint8*>> rows;
        for (auto& pair : dataTable->GetRowMap()) rows.emplace_back(pair.Key.ToString(), pair.Value);
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        return rows;
    }

    FArrayField arrayField(UScriptStruct* row, const CharType* arrayName, const CharType* fieldName)
    {
        auto* array = row ? CastField<FArrayProperty>(row->GetPropertyByNameInChain(arrayName)) : nullptr;
        auto* element = array ? CastField<FStructProperty>(array->GetInner()) : nullptr;
        FProperty* field = element ? element->GetStruct()->GetPropertyByNameInChain(fieldName) : nullptr;
        if (!field || field->GetSize() != sizeof(int32)) return {};
        return {array, element->GetElementSize(), field};
    }

    bool textToSeed(StringViewType wide, uint32_t& out)
    {
        std::string text;
        for (const auto character : wide)
        {
            if (character > 127) return false;
            text.push_back(static_cast<char>(character));
        }
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return false;
        const std::string_view trimmed = std::string_view(text).substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
        if (trimmed.size() > 8) return false;
        const auto [end, error] = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), out, 16);
        return error == std::errc{} && end == trimmed.data() + trimmed.size();
    }

    uint32_t readBits(const uint8* flags, int32 first)
    {
        uint32_t value{};
        for (int32 bit = 0; bit < 32; ++bit)
        {
            if (flags[first + bit]) value |= 1u << bit;
        }
        return value;
    }

    void writeBits(uint8* flags, int32 first, uint32_t value)
    {
        for (int32 bit = 0; bit < 32; ++bit)
        {
            flags[first + bit] = (value >> bit) & 1u;
        }
    }

    std::vector<uint8> callKismet(UObject* library, const CharType* name, const std::vector<uint8>& input)
    {
        UFunction* function = library->GetFunctionByNameInChain(name);
        if (!function) return {};
        std::vector<uint8> params(std::max<int32>(function->GetPropertiesSize(), 16));
        FProperty* returnProperty = function->GetReturnProperty();
        for (FProperty* param : TFieldRange<FProperty>(function, EFieldIterationFlags::IncludeDeprecated))
        {
            if (param == returnProperty || param->HasAnyPropertyFlags(CPF_ReturnParm)) continue;
            std::memcpy(params.data() + param->GetOffset_Internal(), input.data(), std::min<size_t>(input.size(), param->GetSize()));
            break;
        }
        library->ProcessEvent(function, params.data());
        if (!returnProperty) return {};
        const auto* first = params.data() + returnProperty->GetOffset_Internal();
        return std::vector<uint8>(first, first + returnProperty->GetSize());
    }

    UObject* loadByPath(const CharType* path)
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"));
        if (!library) return nullptr;
        std::vector<uint8> pathString(sizeof(FString));
        new (pathString.data()) FString(path);
        const std::vector<uint8> softPath = callKismet(library, STR("MakeSoftObjectPath"), pathString);
        if (softPath.empty()) return nullptr;
        const std::vector<uint8> softReference = callKismet(library, STR("Conv_SoftObjPathToSoftObjRef"), softPath);
        if (softReference.empty()) return nullptr;
        const std::vector<uint8> loaded = callKismet(library, STR("LoadAsset_Blocking"), softReference);
        if (loaded.size() < sizeof(UObject*)) return nullptr;
        UObject* object{};
        std::memcpy(&object, loaded.data(), sizeof(object));
        return object;
    }

    void setWidgetText(UObject* textBlock, const StringType& text)
    {
        auto* library = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetTextLibrary"));
        if (!library || !textBlock) return;
        std::vector<uint8> string(sizeof(FString));
        new (string.data()) FString(text.c_str());
        const std::vector<uint8> converted = callKismet(library, STR("Conv_StringToText"), string);
        if (converted.empty()) return;
        callByName(textBlock, STR("SetText"), [&converted](const auto& name, uint8* value) {
            if (name == STR("InText")) std::memcpy(value, converted.data(), converted.size());
        });
    }

    bool readVector(const uint8* data, int32 size, double out[3])
    {
        for (int32 axis = 0; axis < 3; ++axis)
        {
            if (size == 24) out[axis] = reinterpret_cast<const double*>(data)[axis];
            else if (size == 12) out[axis] = reinterpret_cast<const float*>(data)[axis];
            else return false;
        }
        return true;
    }

    std::vector<uint8> callWithWorld(const CharType* library, const CharType* name, UObject* worldContext)
    {
        auto* defaults = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, library);
        UFunction* function = defaults && worldContext ? defaults->GetFunctionByNameInChain(name) : nullptr;
        FProperty* returnProperty = function ? function->GetReturnProperty() : nullptr;
        if (!returnProperty) return {};
        std::vector<uint8> params(std::max<int32>(function->GetPropertiesSize(), 16));
        for (FProperty* param : TFieldRange<FProperty>(function, EFieldIterationFlags::IncludeDeprecated))
        {
            if (param->GetName() == STR("WorldContextObject")) *reinterpret_cast<UObject**>(params.data() + param->GetOffset_Internal()) = worldContext;
        }
        defaults->ProcessEvent(function, params.data());
        const auto* first = params.data() + returnProperty->GetOffset_Internal();
        return std::vector<uint8>(first, first + returnProperty->GetSize());
    }

    int64_t callInt(UObject* object, const CharType* name)
    {
        UFunction* function = object ? object->GetFunctionByNameInChain(name) : nullptr;
        FProperty* returnProperty = function ? function->GetReturnProperty() : nullptr;
        if (!returnProperty) return -1;
        std::vector<uint8> params(std::max<int32>(function->GetPropertiesSize(), 16));
        object->ProcessEvent(function, params.data());
        const uint8* value = params.data() + returnProperty->GetOffset_Internal();
        switch (returnProperty->GetSize())
        {
        case 1: return *value;
        case 4: return *reinterpret_cast<const int32*>(value);
        case 8: return *reinterpret_cast<const int64_t*>(value);
        default: return -1;
        }
    }

    void setWidgetVisibility(UObject* widget, uint8 visibility)
    {
        callByName(widget, STR("SetVisibility"), [visibility](const auto& name, uint8* value) {
            if (name == STR("InVisibility")) *value = visibility;
        });
    }

    void setWidgetShown(UObject* widget, bool shown)
    {
        setWidgetVisibility(widget, shown ? shownVisibility : collapsedVisibility);
    }

    UObject* objectFrom(const std::vector<uint8>& bytes)
    {
        UObject* object{};
        if (bytes.size() >= sizeof(object)) std::memcpy(&object, bytes.data(), sizeof(object));
        return object;
    }

    size_t stepChoice(size_t current, int32 step, size_t count)
    {
        if (count == 0) return current;
        if (current >= count) return 0;
        return static_cast<size_t>(std::clamp<int64_t>(static_cast<int64_t>(current) + step, 0, static_cast<int64_t>(count) - 1));
    }
}
