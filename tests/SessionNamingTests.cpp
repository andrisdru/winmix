#include "doctest.h"
#include "winmix/audio/SessionNaming.h"

#include <optional>
#include <string>

using winmix::audio::SessionNaming;

namespace {
std::optional<std::wstring> Opt(const wchar_t* s)
{
    return s == nullptr ? std::nullopt : std::make_optional(std::wstring(s));
}
} // namespace

TEST_CASE("SessionsWithoutANameYieldNull")
{
    // Null tells the caller to fall back to the process identity, which is
    // the common path: most applications never populate this field.
    CHECK(!SessionNaming::Resolve(std::nullopt).has_value());
    CHECK(!SessionNaming::Resolve(Opt(L"")).has_value());
    CHECK(!SessionNaming::Resolve(Opt(L"   ")).has_value());
    CHECK(!SessionNaming::Resolve(Opt(L"\t\r\n")).has_value());
}

TEST_CASE("LiteralNamesPassThroughTrimmed")
{
    CHECK(SessionNaming::Resolve(Opt(L"Spotify")) == Opt(L"Spotify"));
    CHECK(SessionNaming::Resolve(Opt(L"  Firefox  ")) == Opt(L"Firefox"));
    CHECK(SessionNaming::Resolve(Opt(L"Game Audio")) == Opt(L"Game Audio"));
}

TEST_CASE("AnUnresolvableIndirectReferenceYieldsNull")
{
    // Rather than surfacing the raw "@..." string to the user, an
    // unresolvable reference has to degrade to null (the process-name
    // fallback happens one layer up).
    const auto result = SessionNaming::Resolve(Opt(LR"(@%SystemRoot%\System32\definitely-not-real.dll,-999)"));
    CHECK(!result.has_value());
}

TEST_CASE("AMalformedIndirectReferenceDoesNotThrow")
{
    CHECK(!SessionNaming::Resolve(Opt(L"@")).has_value());
    CHECK(!SessionNaming::Resolve(Opt(L"@garbage")).has_value());
}

TEST_CASE("ANameContainingAnAtSignIsNotTreatedAsIndirect")
{
    // Only a leading '@' marks an indirect reference; an address-like name
    // is literal text the application chose.
    CHECK(SessionNaming::Resolve(Opt(L"user@example.com")) == Opt(L"user@example.com"));
}

TEST_CASE("ResolvedNamesNeverKeepTheIndirectionMarker")
{
    // Whatever an indirect reference resolves to (OS/locale-dependent), the
    // user must never see the "@module,-id" form leak through.
    const auto result = SessionNaming::Resolve(Opt(LR"(@%SystemRoot%\System32\AudioSrv.Dll,-202)"));
    CHECK((!result.has_value() || result->front() != L'@'));
}
