#include "prx/libSceSystemService/include/SystemService.hpp"
#include "prx/libSceSystemService/SystemService.hpp"
#include "SceTypes.hpp"
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <limits>
#include <initializer_list>
#include <string_view>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace {

constexpr char Sentinel = 0x5A;

struct GuardedBuffer {
    char bytes[10 + 6];
    GuardedBuffer() { std::memset(bytes, Sentinel, sizeof(bytes)); }
    bool Untouched() const {
        for (const char byte : bytes) {
            if (byte != Sentinel) return false;
        }
        return true;
    }
    bool GuardIntact() const {
        for (std::size_t index = 10; index < sizeof(bytes); ++index) {
            if (bytes[index] != Sentinel) return false;
        }
        return true;
    }
};

void Require(bool value) { if (!value) std::abort(); }

template<typename TCall>
bool Throws(TCall call) {
    try {
        call();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

bool GetAppIdThrows(const char* titleId) {
    return Throws([&] { static_cast<void>(sceSystemServiceGetAppId(titleId)); });
}

bool GetAppIdRejects(const char* titleId) {
    return sceSystemServiceGetAppId(titleId) == SYSTEM_SERVICE_INVALID_TITLE_APP_ID && sceLncUtilGetAppId(titleId) == SYSTEM_SERVICE_INVALID_TITLE_APP_ID;
}

bool IsAppLaunchedRejects(const char* titleId) {
    bool launched = false;
    return sceLncUtilIsAppLaunched(titleId, &launched) == LNC_UTIL_ERROR_INVALID_ARGUMENT && !launched && sceLncUtilIsAppLaunched(titleId, nullptr) == LNC_UTIL_ERROR_INVALID_ARGUMENT;
}

void CheckLaunchedOutput(const char* titleId) {
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    const std::size_t pageSize = info.dwPageSize;
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, pageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Require(memory != nullptr);
    DWORD previous = 0;
    Require(VirtualProtect(memory + pageSize, pageSize, PAGE_NOACCESS, &previous) != 0);
#else
    const long hostPageSize = sysconf(_SC_PAGESIZE);
    Require(hostPageSize > 0);
    const auto pageSize = static_cast<std::size_t>(hostPageSize);
    auto* memory = static_cast<unsigned char*>(mmap(nullptr, pageSize * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    Require(memory != MAP_FAILED);
    Require(mprotect(memory + pageSize, pageSize, PROT_NONE) == 0);
#endif
    for (bool* output : {reinterpret_cast<bool*>(0x10), reinterpret_cast<bool*>(memory + pageSize)}) {
        errno = 71;
        Require(sceLncUtilIsAppLaunched(titleId, output) == LNC_UTIL_ERROR_INVALID_ARGUMENT);
        Require(errno == 71);
        Require(sceLncUtilIsAppLaunched("", output) == LNC_UTIL_ERROR_INVALID_ARGUMENT);
        Require(Throws([&] { sceLncUtilIsAppLaunched("PPSA00001", output); }));
    }
    std::memset(memory, Sentinel, pageSize);
    auto* output = reinterpret_cast<bool*>(memory + pageSize - sizeof(bool));
    *output = false;
#ifdef _WIN32
    Require(VirtualProtect(memory, pageSize, PAGE_READONLY, &previous) != 0);
#else
    Require(mprotect(memory, pageSize, PROT_READ) == 0);
#endif
    errno = 71;
    Require(sceLncUtilIsAppLaunched(titleId, output) == LNC_UTIL_ERROR_INVALID_ARGUMENT);
    Require(errno == 71 && !*output);
#ifdef _WIN32
    Require(VirtualProtect(memory, pageSize, PAGE_READWRITE, &previous) != 0);
#else
    Require(mprotect(memory, pageSize, PROT_READ | PROT_WRITE) == 0);
#endif
    errno = 71;
    Require(sceLncUtilIsAppLaunched(titleId, output) == 0 && *output);
    Require(errno == 71);
    for (std::size_t index = 0; index < pageSize - sizeof(bool); ++index) Require(memory[index] == Sentinel);
#ifdef _WIN32
    Require(VirtualFree(memory, 0, MEM_RELEASE) != 0);
#else
    Require(munmap(memory, pageSize * 2) == 0);
#endif
}

void CheckRunningTitleLookups(const char* titleId, int runningAppId) {
    Require(sceLncUtilGetAppId(titleId) == runningAppId);
    bool launched = false;
    Require(sceLncUtilIsAppLaunched(titleId, &launched) == 0 && launched);
    Require(sceLncUtilIsAppLaunched(titleId, nullptr) == LNC_UTIL_ERROR_INVALID_ARGUMENT);
}

bool GetAppTitleIdThrows(int appId, GuardedBuffer& buffer) {
    return Throws([&] { static_cast<void>(sceSystemServiceGetAppTitleId(appId, buffer.bytes)); }) && buffer.Untouched();
}

bool LncGetAppTitleIdThrows(std::uint32_t appId, GuardedBuffer& buffer) {
    return Throws([&] { static_cast<void>(sceLncUtilGetAppTitleId(static_cast<std::int32_t>(appId), buffer.bytes)); }) && buffer.Untouched();
}

void CheckOtherApplications(int runningAppId) {
    const int otherAppIds[] = {runningAppId + 1, runningAppId - 1, 0, -1, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()};
    for (const int appId : otherAppIds) {
        GuardedBuffer buffer;
        Require(GetAppTitleIdThrows(appId, buffer));
        Require(LncGetAppTitleIdThrows(static_cast<std::uint32_t>(appId), buffer));
    }
    GuardedBuffer buffer;
    Require(LncGetAppTitleIdThrows(static_cast<std::uint32_t>(runningAppId) | 0x80000000u, buffer));
    Require(LncGetAppTitleIdThrows(std::numeric_limits<std::uint32_t>::max(), buffer));
}

void CheckNullBuffers(int runningAppId) {
    Require(Throws([&] { static_cast<void>(sceSystemServiceGetAppTitleId(runningAppId, nullptr)); }));
    Require(Throws([&] { static_cast<void>(sceLncUtilGetAppTitleId(runningAppId, nullptr)); }));
    Require(Throws([&] { static_cast<void>(sceSystemServiceGetAppTitleId(runningAppId + 1, nullptr)); }));
    Require(sceSystemServiceGetMainAppTitleId(nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
    Require(GetAppIdRejects(nullptr));
    Require(IsAppLaunchedRejects(nullptr));
}

void CheckKnownTitle(int runningAppId) {
    constexpr std::string_view titleId = "PPSA00000";
    Require(runningAppId >= 0);
    Require(sceSystemServiceGetAppId("PPSA00000") == runningAppId);
    Require(sceSystemServiceGetAppId("PPSA00000") == runningAppId);
    const char* const otherTitles[] = {"PPSA00001", "PPSA00010", "ppsa00000", "CUSA00000"};
    for (const char* other : otherTitles) {
        Require(GetAppIdThrows(other));
        Require(sceLncUtilIsAppLaunched(other, nullptr) == LNC_UTIL_ERROR_INVALID_ARGUMENT);
    }
    const char* const malformedTitles[] = {"PPSA0000", "PPSA000000", "PPSA00000 ", " PPSA00000", ""};
    for (const char* malformed : malformedTitles) {
        Require(GetAppIdRejects(malformed));
        Require(IsAppLaunchedRejects(malformed));
    }
    CheckLaunchedOutput("PPSA00000");
    CheckRunningTitleLookups("PPSA00000", runningAppId);

    GuardedBuffer buffer;
    Require(sceSystemServiceGetAppTitleId(runningAppId, buffer.bytes) == 0);
    Require(std::memcmp(buffer.bytes, "PPSA00000", titleId.size() + 1) == 0);
    Require(buffer.GuardIntact());
    Require(sceSystemServiceGetAppId(buffer.bytes) == runningAppId);
    Require(sceLncUtilGetAppId(buffer.bytes) == runningAppId);

    GuardedBuffer lncBuffer;
    Require(sceLncUtilGetAppTitleId(runningAppId, lncBuffer.bytes) == 0);
    Require(std::memcmp(lncBuffer.bytes, "PPSA00000", titleId.size() + 1) == 0);
    Require(lncBuffer.GuardIntact());

    GuardedBuffer mainBuffer;
    Require(sceSystemServiceGetMainAppTitleId(mainBuffer.bytes) == 0);
    Require(std::memcmp(mainBuffer.bytes, "PPSA00000", titleId.size() + 1) == 0);
    Require(mainBuffer.GuardIntact());

    GuardedBuffer dirty;
    std::memset(dirty.bytes, 'x', 10);
    Require(sceSystemServiceGetAppTitleId(runningAppId, dirty.bytes) == 0);
    Require(std::strcmp(dirty.bytes, "PPSA00000") == 0 && dirty.GuardIntact());
    auto* const inaccessible = reinterpret_cast<char*>(0x10);
    Require(Throws([&] { static_cast<void>(sceSystemServiceGetAppTitleId(runningAppId, inaccessible)); }));
    Require(Throws([&] { static_cast<void>(sceLncUtilGetAppTitleId(runningAppId, inaccessible)); }));
    Require(sceSystemServiceGetAppTitleId(runningAppId, dirty.bytes) == 0);
    Require(std::strcmp(dirty.bytes, "PPSA00000") == 0 && dirty.GuardIntact());
    Require(sceSystemServiceGetMainAppTitleId(dirty.bytes) == 0);
    Require(std::strcmp(dirty.bytes, "PPSA00000") == 0 && dirty.GuardIntact());
    Require(Throws([&] { static_cast<void>(sceSystemServiceGetMainAppTitleId(inaccessible)); }));

    CheckOtherApplications(runningAppId);
    CheckNullBuffers(runningAppId);
}

void CheckLongTitle(int runningAppId) {
    Require(GetAppIdRejects("PPSA000000"));
    Require(IsAppLaunchedRejects("PPSA000000"));
    Require(GetAppIdThrows("PPSA00000"));
    Require(GetAppIdRejects("PPSA0000000"));
    GuardedBuffer buffer;
    Require(GetAppTitleIdThrows(runningAppId, buffer));
    Require(LncGetAppTitleIdThrows(static_cast<std::uint32_t>(runningAppId), buffer));
    GuardedBuffer mainBuffer;
    Require(Throws([&] { static_cast<void>(sceSystemServiceGetMainAppTitleId(mainBuffer.bytes)); }) && mainBuffer.Untouched());
}

void CheckMissingMetadata(int runningAppId) {
    Require(GetAppIdThrows("PPSA00000"));
    Require(GetAppIdRejects(nullptr));
    Require(IsAppLaunchedRejects(nullptr));
    GuardedBuffer buffer;
    Require(GetAppTitleIdThrows(runningAppId, buffer));
    Require(LncGetAppTitleIdThrows(static_cast<std::uint32_t>(runningAppId), buffer));
    GuardedBuffer mainBuffer;
    Require(Throws([&] { static_cast<void>(sceSystemServiceGetMainAppTitleId(mainBuffer.bytes)); }) && mainBuffer.Untouched());
}

}

int main(int argc, char** argv) {
    Require(argc == 2);
    const std::string_view mode = argv[1];
    const int runningAppId = sceSystemServiceGetAppIdOfRunningBigApp();
    if (mode == "known") CheckKnownTitle(runningAppId);
    else if (mode == "long") CheckLongTitle(runningAppId);
    else if (mode == "missing") CheckMissingMetadata(runningAppId);
    else Require(false);
}
