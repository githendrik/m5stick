#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Preferences.h>

class ConfigManager {
public:
    String toasterIp;

    void loadAll() {
        Preferences prefs;
        prefs.begin("m5stick", true);
        toasterIp = prefs.getString("toaster_ip", "");
        prefs.end();
    }

    void saveAll() {
        Preferences prefs;
        prefs.begin("m5stick", false);
        prefs.putString("toaster_ip", toasterIp);
        prefs.end();
    }

    void clear() {
        Preferences prefs;
        prefs.begin("m5stick", false);
        prefs.clear();
        prefs.end();
        toasterIp = "";
    }
};

#endif
