# Loxone 1-Wire Integration — Setup-Anleitung

## Überblick

Dieses Projekt erweitert HomeKey-ESP32 um eine direkte Loxone 1-Wire Integration.
Nach erfolgreicher Apple HomeKey Authentifizierung (iPhone, Apple Watch) emuliert der
ESP32 einen Dallas DS1990A iButton auf dem Loxone 1-Wire Bus. Loxone übernimmt die
Zugangssteuerung wie gewohnt — keine Änderung der Loxone-Konfiguration nötig.

**Vorteile gegenüber MQTT:**
- Kein stabiles WLAN-Signal am Verbauort erforderlich
- Kein MQTT-Broker nötig
- Kabelgebunden → keine Verbindungsabbrüche
- Gleiche Logik wie physische iButtons

## Hardware

| Komponente | Beschreibung |
|------------|-------------|
| ESP32 WROOM NodeMCU | Mikrocontroller |
| PN532 NFC Reader | Liest Apple HomeKey via NFC |
| 4.7 kΩ Widerstand | Pull-up für 1-Wire Bus (GPIO4 → 3.3V) |
| Loxone 1-Wire Extension | Liest den emulierten iButton |
| 5V Netzteil | Dedizierte Stromversorgung am Verbauort |

## Verdrahtung

Schaltplan: [`docs/wiring/homekey-loxone-wiring.svg`](wiring/homekey-loxone-wiring.svg)

**1-Wire Verbindung:**
```
ESP32 GPIO4 ──┬── 4.7kΩ ──► 3.3V (ESP32 Pin)
              │
         Loxone 1-Wire Extension DATA-Klemme
              │
         GND (gemeinsame Masse ESP32 + Loxone Ext.)
```

**PN532 SPI-Verbindung:**
```
PN532 SCK   → GPIO18
PN532 MISO  → GPIO19
PN532 MOSI  → GPIO23
PN532 SS    → GPIO5
PN532 VCC   → 5V
PN532 GND   → GND
```

> **Wichtig:** GPIO4 muss als Open-Drain konfiguriert sein (geschieht automatisch).
> Der externe 4.7kΩ Pull-up-Widerstand ist zwingend erforderlich.
> GPIO34–39 nicht verwenden (nur Eingang, kein Open-Drain möglich).

## Ersteinrichtung

### 1. Firmware konfigurieren

```bash
cd /Users/jenslammert/claude/esp32_homekey
idf.py menuconfig
```

Unter **"Loxone 1-Wire Bridge"**:
- `LOXONE_ONEWIRE_GPIO`: 4 (Standard)
- `LOXONE_ACTIVE_DURATION_MS`: 3000 (3 Sekunden)
- `HOMEKEY_AP_PASSWORD`: Eigenes sicheres Passwort setzen (min. 8 Zeichen)

### 2. Flashen

```bash
./flash.sh
# oder mit explizitem Port:
./flash.sh /dev/tty.usbserial-XXXX
```

Port-Suche falls nötig:
```bash
ls /dev/tty.*
```

### 3. HomeKit einrichten

Beim ersten Start öffnet der ESP32 einen temporären WLAN-Hotspot:
- **SSID:** `HK-Setup-XXYYZZ` (letzte 3 Bytes der BT-MAC-Adresse)
- **Passwort:** wie in Kconfig gesetzt

Im Browser `http://192.168.4.1` öffnen → HomeSpan Konfiguration:
1. WLAN-Zugangsdaten eingeben (für Web-UI Zugang)
2. HomeKit-Code notieren und in Apple Home App einscannen

Nach erfolgreicher WLAN-Verbindung: IP im Serial Monitor ablesen.
Der AP-Modus öffnet sich **nach der Einrichtung nie wieder automatisch**.

### 4. Apple Device issuerId ermitteln

Gerät (iPhone/Apple Watch) an den PN532 halten. Im Serial Monitor erscheint:
```
I (XXXX) LoxoneOneWire: HomeKey tap — issuerId: a1b2c3d4
W (XXXX) LoxoneOneWire: No 1-Wire mapping for issuerId a1b2c3d4 — add via POST /loxone/mappings
```

Die `issuerId` ist für **alle Geräte derselben Apple-ID** identisch:
iPhone + Apple Watch + iPad einer Person = gleiche `issuerId`.

### 5. iButton ROM-Code bestimmen

**Option A: Bestehenden Loxone iButton wiederverwenden**

Falls du bereits physische iButtons in Loxone konfiguriert hast:
- ROM-Code aus Loxone auslesen (1-Wire Extension → iButton Konfiguration)
- Format: 8 Bytes hex, z.B. `01A2B3C4D5E6F7XX` (XX = CRC)
- Oder nur 7 Bytes eingeben — CRC wird automatisch berechnet

**Option B: Neuen virtuellen iButton anlegen**

1. Wähle 6 beliebige Bytes für die Seriennummer, z.B. `A1B2C3D4E5F6`
2. Mapping speichern (CRC wird auto-berechnet)
3. In Loxone: 1-Wire Extension → "1-Wire Suche" starten
4. Apple Device an Leser halten → Loxone erkennt den virtuellen iButton
5. ID in Loxone-Konfiguration übernehmen und Berechtigungen vergeben

### 6. Mapping konfigurieren

Über die Web-UI (HTTP, im lokalen Netzwerk):

```bash
# Mapping hinzufügen / aktualisieren
curl -X POST http://ESP32_IP/loxone/mappings \
  -H "Content-Type: application/json" \
  -d '{"issuerId":"a1b2c3d4","rom":"01A2B3C4D5E6F7","label":"Jens"}'

# Alle Mappings anzeigen
curl http://ESP32_IP/loxone/mappings

# Mapping löschen
curl -X DELETE "http://ESP32_IP/loxone/mappings?issuerId=a1b2c3d4"
```

**ROM-Format:**
- 14 hex Zeichen (7 Bytes) → CRC wird automatisch berechnet
- 16 hex Zeichen (8 Bytes inkl. CRC) → direkt übernommen
- Erstes Byte muss `01` sein (DS1990A Familie)

## Betriebsablauf

```
Person hält iPhone oder Apple Watch an Leser
               ↓
PN532 erkennt NFC-Feld (HomeKey Protokoll)
               ↓
ESP32 validiert HomeKey-Signatur (lokal, kein Internet)
               ↓
ESP32 sucht issuerId in Mapping-Tabelle (NVS Flash)
               ↓
ESP32 aktiviert iButton-Emulation auf GPIO4 (3 Sekunden)
               ↓
Loxone 1-Wire Extension scannt Bus → erkennt iButton-ROM
               ↓
Loxone prüft Berechtigung → öffnet Tür
```

## Betrieb ohne WLAN

Der 1-Wire Betrieb funktioniert vollständig ohne WLAN-Verbindung:
- HomeKey-Validierung läuft lokal (Elliptic-Curve Kryptographie auf dem ESP32)
- Mapping-Tabelle liegt im NVS-Flash (bleibt nach Reboot erhalten)
- WLAN wird nur für initiale HomeKit-Kopplung und Web-UI-Konfiguration benötigt

## Sicherheitshinweise

| Aspekt | Maßnahme |
|--------|----------|
| WLAN AP | Nur bei fehlender HomeKit-Kopplung aktiv. SSID ist MAC-basiert, Passwort in Kconfig setzen. |
| Kein Internet | HomeKey-Validierung vollständig lokal. |
| 1-Wire | Kabelgebunden, kein Funk zwischen ESP32 und Loxone. |
| Web-UI | Nur im lokalen Netzwerk erreichbar (kein HTTPS auf ESP32). Nach Konfiguration WLAN optional. |
| NVS | Mapping-Daten im Flash. Für höchste Sicherheit: Flash-Encryption in ESP-IDF aktivieren. |

## Troubleshooting

| Problem | Prüfung |
|---------|---------|
| Loxone erkennt iButton nicht | Pull-up vorhanden? GPIO4 korrekt? Gemeinsame Masse? `LOXONE_ACTIVE_DURATION_MS` erhöhen? |
| issuerId wird nicht geloggt | PN532 SPI-Pins korrekt? Stromversorgung stabil? |
| Mapping nicht gespeichert | NVS-Partition vorhanden (`idf.py build` Fehler prüfen)? |
| AP öffnet sich nach Reboot | HomeKit-Kopplung verloren → erneut koppeln |
| 1-Wire Timing-Probleme | Kabellänge < 30m halten. Keine anderen 1-Wire-Geräte am gleichen Bus-Segment. |

## iButton ROM Kurzreferenz

DS1990A ROM-Struktur (8 Bytes, LSB zuerst auf dem Bus):
```
Byte 0:    0x01           (Familiencode DS1990A — immer 0x01)
Bytes 1-6: Seriennummer   (beliebig wählen, nicht vorhersehbar wählen)
Byte 7:    CRC8           (wird von Firmware automatisch berechnet)
```

Beispiel-Eingabe (7 Bytes, CRC auto):
```
01A1B2C3D4E5F6   → Firmware berechnet CRC und speichert 01A1B2C3D4E5F6XX
```
