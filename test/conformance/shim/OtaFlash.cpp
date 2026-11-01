// Two app partitions kept in the storage file, so a restart of the harness boots whichever one
// the last run chose. Flashing takes no download: WBSKT_OTA_INSTALL is "ok" for a good image, or
// the error install() returns; unset means it always fails.
#include <Preferences.h>
#include <WbsktOtaFlash.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace wbskt_ota {

namespace {

constexpr const char* kShim = "shim-ota";
constexpr size_t kSlotBytes = 1966080;

std::string stored(const char* key, const char* fallback) {
    Preferences prefs;
    prefs.begin(kShim, true);
    const String value = prefs.getString(key, fallback);
    prefs.end();
    return value.c_str();
}

void store(const char* key, const std::string& value) {
    Preferences prefs;
    prefs.begin(kShim, false);
    prefs.putString(key, String(value.c_str()));
    prefs.end();
}

// Read once: the partition this run booted from.
const std::string& running() {
    static const std::string label = stored("boot", "app0");
    return label;
}

std::string other(const std::string& label) { return label == "app0" ? "app1" : "app0"; }

}  // namespace

Partitions partitions() {
    Partitions out;
    out.running = running();
    out.boot = stored("boot", out.running.c_str());
    out.next = other(out.running);
    out.nextBytes = kSlotBytes;
    return out;
}

std::string install(const char*, const char*, const std::string&, size_t, const Progress& progress) {
    const char* outcome = std::getenv("WBSKT_OTA_INSTALL");
    if (outcome == nullptr || std::strcmp(outcome, "ok") != 0) {
        return outcome != nullptr ? outcome : "flashing is not available on this host";
    }
    for (uint8_t percent = 0; percent <= 100; percent += 50) {
        if (progress) {
            progress(percent);
        }
    }
    store("boot", other(running()));
    return "";
}

bool bootFrom(const std::string& label) {
    if (label != "app0" && label != "app1") {
        return false;
    }
    store("boot", label);
    return true;
}

void restart() {
    std::printf("{\"event\":\"restart\"}\n");
    std::fflush(stdout);
    std::_Exit(0);
}

}  // namespace wbskt_ota
