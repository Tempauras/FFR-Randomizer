#pragma once

#include "Constants.h"

namespace ffr
{
    struct FUpdateDisplayParams
    {
        FString seedText;
    };
    bool isNormalGroup(int32 group);
    std::vector<int32> shuffledPool(uint32_t seed);
    int64_t readNumber(FProperty* property, const uint8* container);
    std::vector<std::pair<StringType, uint8*>> sortedRows(UDataTable* dataTable);

    struct FArrayField
    {
        FArrayProperty* array{};
        int32 stride{};
        FProperty* field{};
    };

    FArrayField arrayField(UScriptStruct* row, const CharType* arrayName, const CharType* fieldName);
    bool textToSeed(StringViewType wide, uint32_t& out);
    uint32_t readBits(const uint8* flags, int32 first);
    void writeBits(uint8* flags, int32 first, uint32_t value);
    std::vector<uint8> callKismet(UObject* library, const CharType* name, const std::vector<uint8>& input);
    UObject* loadByPath(const CharType* path);
    void setWidgetText(UObject* textBlock, const StringType& text);
    bool readVector(const uint8* data, int32 size, double out[3]);
    std::vector<uint8> callWithWorld(const CharType* library, const CharType* name, UObject* worldContext);
    int64_t callInt(UObject* object, const CharType* name);
    void setWidgetVisibility(UObject* widget, uint8 visibility);
    void setWidgetShown(UObject* widget, bool shown);
    UObject* objectFrom(const std::vector<uint8>& bytes);
    size_t stepChoice(size_t current, int32 step, size_t count);

    // Fisher-Yates shuffle to make the seeds consistent across machines
    template<typename ValueType>
    void shuffleWith(std::vector<ValueType>& list, std::mt19937& rng)
    {
        for (size_t index = list.size(); index > 1; --index)
        {
            std::swap(list[index - 1], list[rng() % index]);
        }
    }

    template<typename FillType>
    void callByName(UObject* target, const CharType* name, FillType&& fill)
    {
        UFunction* function = target ? target->GetFunctionByNameInChain(name) : nullptr;
        if (!function) return;
        std::vector<uint8> params(std::max<int32>(function->GetPropertiesSize(), 16));
        for (FProperty* param : TFieldRange<FProperty>(function, EFieldIterationFlags::IncludeDeprecated))
        {
            fill(param->GetName(), params.data() + param->GetOffset_Internal());
        }
        target->ProcessEvent(function, params.data());
    }


    template<typename FillType>
    std::vector<uint8> callReturning(UObject* target, const CharType* name, FillType&& fill)
    {
        UFunction* function = target ? target->GetFunctionByNameInChain(name) : nullptr;
        if (!function) return {};
        std::vector<uint8> params(std::max<int32>(function->GetPropertiesSize(), 16));
        for (FProperty* param : TFieldRange<FProperty>(function, EFieldIterationFlags::IncludeDeprecated))
        {
            fill(param->GetName(), params.data() + param->GetOffset_Internal());
        }
        target->ProcessEvent(function, params.data());
        FProperty* returnProperty = function->GetReturnProperty();
        if (!returnProperty) return {};
        const auto* first = params.data() + returnProperty->GetOffset_Internal();
        return std::vector<uint8>(first, first + returnProperty->GetSize());
    }
}
