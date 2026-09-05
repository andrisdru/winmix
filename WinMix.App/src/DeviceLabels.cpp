#include "DeviceLabels.h"

#include <algorithm>
#include <cwctype>
#include <unordered_map>
#include <unordered_set>

namespace winmix::app {
namespace {
std::wstring Fold(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return text;
}

std::wstring Shorten(const std::wstring& name)
{
    std::wstring result = name;
    const auto folded = Fold(name);
    // Only unwrap known generic endpoint types. A custom name containing
    // parentheses (or a localized type we do not recognize) stays intact.
    for (const auto* prefix : {L"speakers (", L"headphones (", L"headset (",
                               L"microphone (", L"line out (", L"digital output ("})
    {
        const auto length = std::wstring(prefix).size();
        if (folded.starts_with(prefix) && name.ends_with(L")") && name.size() > length + 1)
        {
            result = name.substr(length, name.size() - length - 1);
            break;
        }
    }
    for (const auto* mark : {L"(r)", L"(tm)", L"\u00ae", L"\u2122"})
    {
        const std::wstring token(mark);
        for (auto pos = Fold(result).find(token); pos != std::wstring::npos; pos = Fold(result).find(token))
            result.erase(pos, token.size());
    }
    // Clean up gaps left by trademark removal without shortening words.
    std::wstring clean;
    for (wchar_t c : result)
    {
        if (std::iswspace(c))
        {
            if (!clean.empty() && clean.back() != L' ') clean.push_back(L' ');
        }
        else clean.push_back(c);
    }
    if (!clean.empty() && clean.back() == L' ') clean.pop_back();
    return clean.empty() ? name : clean;
}
}

std::vector<std::wstring> CompactDeviceLabels(const std::vector<std::wstring>& names)
{
    std::vector<std::wstring> labels;
    for (const auto& name : names) labels.push_back(Shorten(name));

    // Falling back can collide with another compact label, so repeat until
    // every ambiguous label has reached its original name.
    bool changed;
    do
    {
        changed = false;
        std::unordered_map<std::wstring, size_t> counts;
        for (const auto& label : labels) ++counts[Fold(label)];
        for (size_t i = 0; i < labels.size(); ++i)
        {
            if (counts[Fold(labels[i])] > 1 && labels[i] != names[i])
            {
                labels[i] = names[i];
                changed = true;
            }
        }
    } while (changed);

    std::unordered_set<std::wstring> used;
    std::unordered_set<std::wstring> reserved;
    for (const auto& label : labels) reserved.insert(Fold(label));
    for (auto& label : labels)
    {
        const auto base = label;
        size_t ordinal = 1;
        if (!used.insert(Fold(label)).second)
        {
            do
            {
                label = base + L" [" + std::to_wstring(++ordinal) + L"]";
            } while (reserved.contains(Fold(label)) || used.contains(Fold(label)));
            used.insert(Fold(label));
        }
    }
    return labels;
}
} // namespace winmix::app
