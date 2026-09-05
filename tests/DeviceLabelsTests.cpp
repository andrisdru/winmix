#include "doctest.h"
#include "DeviceLabels.h"

using winmix::app::CompactDeviceLabels;

TEST_CASE("Device labels preserve identifying hardware names")
{
    const auto labels = CompactDeviceLabels({L"Speakers (TOPPING USB DAC)",
        L"Headphones (Sony WH-1000XM5)", L"Speakers (Realtek(R) Audio)", L"My Studio Monitors"});
    CHECK(labels == std::vector<std::wstring>{L"TOPPING USB DAC", L"Sony WH-1000XM5", L"Realtek Audio", L"My Studio Monitors"});
}

TEST_CASE("Custom localized and nested device names survive shortening")
{
    const auto labels = CompactDeviceLabels({L"Studio (Left)", L"\u30b9\u30d4\u30fc\u30ab\u30fc (USB)",
        L"Headphones (DAC (USB))", L"SPEAKERS (Acme\u00ae Audio\u2122)", L"Speakers ()", L""});
    CHECK(labels == std::vector<std::wstring>{L"Studio (Left)", L"\u30b9\u30d4\u30fc\u30ab\u30fc (USB)",
        L"DAC (USB)", L"Acme Audio", L"Speakers ()", L""});
}

TEST_CASE("Colliding compact device labels retain distinguishing endpoint types")
{
    const auto labels = CompactDeviceLabels({L"Speakers (USB Audio)", L"Headphones (USB Audio)", L"USB Audio"});
    CHECK(labels == std::vector<std::wstring>{L"Speakers (USB Audio)", L"Headphones (USB Audio)", L"USB Audio"});
}

TEST_CASE("Identically named devices and preexisting numbered names stay distinct")
{
    const auto labels = CompactDeviceLabels({L"USB Audio", L"USB Audio", L"USB Audio [2]", L"usb audio"});
    CHECK(labels == std::vector<std::wstring>{L"USB Audio", L"USB Audio [3]", L"USB Audio [2]", L"usb audio [4]"});
    CHECK(CompactDeviceLabels({}).empty());
}
