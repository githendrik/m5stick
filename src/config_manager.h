#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <Preferences.h>

class ConfigManager {
public:
    String toasterIp;
    String signalGatewayIp;
    int signalGatewayPort = 8080;
    String signalRecipient;
    String signalAuthToken;

    void loadAll() {
        Preferences prefs;
        prefs.begin("m5stick", true);
        toasterIp = prefs.getString("toaster_ip", "");
        signalGatewayIp = prefs.getString("sig_gw_ip", "");
        signalGatewayPort = prefs.getInt("sig_gw_port", 8080);
        signalRecipient = prefs.getString("sig_recipient", "");
        signalAuthToken = prefs.getString("sig_token", "");
        prefs.end();
    }

    void saveAll() {
        Preferences prefs;
        prefs.begin("m5stick", false);
        prefs.putString("toaster_ip", toasterIp);
        prefs.putString("sig_gw_ip", signalGatewayIp);
        prefs.putInt("sig_gw_port", signalGatewayPort);
        prefs.putString("sig_recipient", signalRecipient);
        prefs.putString("sig_token", signalAuthToken);
        prefs.end();
    }

    void clear() {
        Preferences prefs;
        prefs.begin("m5stick", false);
        prefs.clear();
        prefs.end();
        toasterIp = "";
        signalGatewayIp = "";
        signalGatewayPort = 8080;
        signalRecipient = "";
        signalAuthToken = "";
    }
};

#endif
