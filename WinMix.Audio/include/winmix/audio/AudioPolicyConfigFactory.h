#pragma once

#include <mmdeviceapi.h>
#include <winstring.h>

#include <cstdint>

namespace winmix::audio {

// Calls the undocumented WinRT class Windows.Media.Internal.AudioPolicyConfig
// -- the same mechanism behind Settings > System > Sound > "App volume and
// device preferences" -- to persist one process's preferred output device
// for a given role.
//
// The interface itself is undocumented; the vtable slot layout (see the .cpp)
// is cross-checked against SoundSwitch (github.com/Belphemur/SoundSwitch),
// the same reference the original .NET port used. Unlike that port -- which
// had to read the activated object's vtable by hand because .NET (Core)
// cannot marshal IInspectable/HSTRING through interop attributes at all --
// native C++ has no such restriction: this is a normal (if undocumented)
// COM interface call, declared as a real C++ interface so the compiler lays
// out the vtable for us instead of indexing it manually.
class AudioPolicyConfigFactory
{
public:
    // deviceId is the packed device-interface-path HSTRING AppOutputRouter
    // builds (or a null HSTRING to clear the pin) -- ownership stays with
    // the caller, this does not take or release a reference.
    static HRESULT SetPersistedDefaultAudioEndpoint(uint32_t pid, EDataFlow flow, ERole role, HSTRING deviceId);
};

} // namespace winmix::audio
