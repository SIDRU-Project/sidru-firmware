#pragma once
// PLANTILLA — copia este archivo a secrets.h y pon tus valores reales.
// secrets.h está en .gitignore: NUNCA se commitea.

// --- WiFi (failover: se intentan en orden hasta que una conecte) ---
// La 1ra es la principal. Deja en "" las que no uses; las vacías se saltan.
#define WIFI_SSID   "TU_WIFI"
#define WIFI_PASS   "TU_PASSWORD_WIFI"
#define WIFI_SSID_2 ""
#define WIFI_PASS_2 ""
#define WIFI_SSID_3 ""
#define WIFI_PASS_3 ""
#define WIFI_SSID_4 ""
#define WIFI_PASS_4 ""

// --- Backend (IP LAN del PC que corre el backend; sácala con `ipconfig` → IPv4) ---
#define SERVER_HOST "192.168.1.50"

// --- API key del Smart Bin (tabla smart_bins.api_key en la BD) ---
#define DEVICE_API_KEY "SIDRU-XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"

// --- MQTT (mismas credenciales que infra/mqtt) ---
#define MQTT_USER   "sidru"
#define MQTT_PASS   "sidru_mqtt_pass"
