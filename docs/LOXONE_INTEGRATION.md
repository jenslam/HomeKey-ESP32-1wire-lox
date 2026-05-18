# Loxone 1-Wire Integration — Setup-Anleitung

## Überblick

Dieses Projekt erweitert HomeKey-ESP32 um eine direkte Loxone 1-Wire Integration.
Nach erfolgreicher Apple HomeKey Authentifizierung (iPhone, Apple Watch) emuliert der
ESP32 einen Dallas DS1990A iButton auf dem Loxone 1-Wire Bus. Loxone übernimmt die
Zugangssteuerung wie gewohnt — keine Änderung der Loxone-Konfiguration nötig.

**ROM-Code-Ableitung:** Der virtuelle iButton-ROM wird deterministisch aus der HomeKey
`issuerId` abgeleitet: `[0x01, issuerId[0..5], CRC8]`. Kein manuelles Mapping nötig.
Gleiche Apple ID → gleicher ROM → Loxone-Konfiguration überlebt ESP32-Reflash.

**Vorteile gegenüber MQTT:**
- Kabelgebunden → keine Verbindungsabbrüche
- Kein MQTT-Broker nötig
- Gleiche Logik wie physische iButtons
- HomeKey-Validierung vollständig lokal (kein Internet)

## Hardware

| Komponente | Beschreibung |
|------------|-------------|
| ESP32 WROOM NodeMCU | Mikrocontroller |
| PN532 NFC Reader | Liest Apple HomeKey via NFC |
| 4.7 kΩ Widerstand | Pull-up für 1-Wire Bus (GPIO27 → 3.3V) |
| 100 µF Elko | Zwischen 3.3V und GND, stabilisiert Spannungsregler |
| Loxone 1-Wire Extension | Liest den emulierten iButton |
| 5V Netzteil (≥1A) | Dedizierte Stromversorgung am Verbauort |

## Verdrahtung

Schaltplan: [`docs/wiring/homekey-loxone-wiring.svg`](wiring/homekey-loxone-wiring.svg)

**1-Wire Verbindung:**
```
ESP32 GPIO27 ──┬── 4.7 kΩ ──► 3.3V (ESP32 Pin)
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

**Stabilisierungskondensator:**
```
ESP32 3V3-Pin ──[+ 100µF -]── GND-Pin
```
(langer Pin des Elkos an 3V3, kurzer an GND)

> **Wichtig:** GPIO27 muss als Open-Drain konfiguriert sein (geschieht automatisch).
> Der externe 4.7 kΩ Pull-up-Widerstand ist zwingend erforderlich.
> GPIO34–39 nicht verwenden (nur Eingang, kein Open-Drain möglich).

## Ersteinrichtung

### 1. Firmware flashen

```bash
export PATH="$HOME/.bun/bin:$PATH"
. ~/esp/esp-idf/export.sh     # ESP-IDF v5.5.4

# Build + Flash (USB-Kabel angeschlossen):
idf.py -p /dev/tty.SLAB_USBtoUART flash

# Oder Convenience-Script:
./flash.sh
```

> **NodeMCU + 5V PSU:** NodeMCU-Boards können beim Booten ohne USB-Verbindung
> Probleme mit der WiFi-Initialisierung haben (bekanntes arduino-esp32 Problem,
> behoben in Version ≥3.3.8). Dieses Fork verwendet 3.3.8.
> Zusätzlich: 100 µF Elko zwischen 3.3V und GND des ESP32 verbessert die
> Spannungsstabilität beim WiFi-Verbindungsaufbau.

### 2. HomeKit einrichten

Beim ersten Start öffnet der ESP32 einen temporären WLAN-Hotspot:
- **SSID:** `HK-Setup-XXYYZZ` (letzte 3 Bytes der BT-MAC-Adresse)
- **Passwort:** wie in `HOMEKEY_AP_PASSWORD` gesetzt (Standard: `changeme1` — **ändern!**)

Im Browser `http://192.168.4.1` öffnen → HomeSpan Konfiguration:
1. WLAN-Zugangsdaten eingeben
2. HomeKit-Code notieren und in Apple Home App einscannen

Nach erfolgreicher WLAN-Verbindung: IP über Router-DHCP-Liste oder Serial Monitor ermitteln.

### 3. Loxone-Parameter konfigurieren (Web UI)

Im Browser `http://ESP32_IP` öffnen → **Loxone** Seite:

| Einstellung | Standard | Beschreibung |
|-------------|----------|-------------|
| Enable | ✓ | 1-Wire Emulation aktivieren |
| GPIO Pin | 27 | GPIO für 1-Wire Bus |
| Active Duration | 3000 ms | Wie lange iButton nach Tap sichtbar ist |

Die Active Duration sollte mindestens 2× das Loxone-Poll-Intervall betragen (~1 Sekunde).
3000 ms = 2–3 Loxone-Zyklen, empfohlener Standardwert.

### 4. issuerId und ROM-Code ermitteln

Apple Device an den PN532 halten. Im Serial Monitor oder Web UI Logs erscheint:
```
I LoxoneOneWire: HomeKey tap — issuerId: f2963548...
I LoxoneOneWire: issuerId: f2963548... → ROM: 01F29635484B2E48AA
I LoxoneOneWire: Add to Loxone (if new): 01F29635484B2E48AA
```

Die `issuerId` ist für **alle Geräte derselben Apple-ID** identisch:
iPhone + Apple Watch + iPad einer Person = gleiche `issuerId` = gleicher ROM.

### 5. Loxone konfigurieren

1. **1-Wire Suche starten:** Loxone Config → 1-Wire Extension → "1-Wire Suche"
2. **Tap ausführen:** Apple Device an PN532 halten während Suche läuft
3. **Gerät erscheint:** ROM `01F296...` taucht in den Suchergebnissen auf
4. **Zugangs-Baustein zuordnen:** 1-Wire Gerät dem Zugangs-Baustein zuweisen
5. **Fertig:** Nächster Tap öffnet die Tür

> Der iButton-Emulator erzeugt eine saubere `0 → 1 → 0` Zustandsänderung:
> der virtuelle iButton erscheint nach dem Tap für `Active Duration` Millisekunden
> auf dem Bus und verschwindet dann wieder. Der Loxone Zugangs-Baustein erkennt
> diese Flanke und triggert die konfigurierte Aktion.

## Betriebsablauf

```
Person hält iPhone oder Apple Watch an PN532-Leser
               ↓
ESP32 validiert HomeKey-Signatur (lokal, kein Internet)
               ↓
ROM = [0x01] + issuerId[0..5] + [CRC8]
               ↓
ESP32 aktiviert iButton-Emulation auf GPIO27
               ↓
Loxone 1-Wire Extension scannt Bus → erkennt iButton-ROM (1→Zustand)
               ↓
esp_timer nach Active Duration → iButton verschwindet (→0-Zustand)
               ↓
Loxone Zugangs-Baustein erkennt Flanke → öffnet Tür
```

## Betrieb ohne WLAN

Der 1-Wire Betrieb funktioniert vollständig ohne WLAN-Verbindung:
- HomeKey-Validierung läuft lokal (Elliptic-Curve Kryptographie auf dem ESP32)
- ROM-Ableitung ist deterministisch (keine Netzwerkabhängigkeit)
- WLAN wird nur für initiale HomeKit-Kopplung und Web-UI-Konfiguration benötigt

## Sicherheitshinweise

| Aspekt | Maßnahme |
|--------|----------|
| WLAN AP | Nur aktiv wenn keine HomeKit-Kopplung vorhanden. Passwort in Kconfig setzen. |
| HomeKey | Kryptographische Authentifizierung, fälschungssicher |
| 1-Wire | Kabelgebunden, kein Funk zwischen ESP32 und Loxone |
| Web-UI | Nur im lokalen Netzwerk erreichbar (kein HTTPS). Nach Konfiguration WLAN optional. |
| ROM-Determinismus | Gleiche Apple-ID = gleicher ROM. Für mehrere Personen: jede Person hat eigene Apple-ID = eigenen ROM = eigene Loxone-Berechtigung. |

## Troubleshooting

| Problem | Prüfung |
|---------|---------|
| Loxone erkennt iButton nicht | Pull-up 4.7 kΩ vorhanden? GPIO27 korrekt konfiguriert? Gemeinsame Masse? |
| Zustandsänderung triggert nicht | `Active Duration` erhöhen (5000+ ms). Loxone-Polling-Intervall prüfen. |
| issuerId wird nicht geloggt | PN532 SPI-Pins korrekt? Stromversorgung stabil? |
| ESP32 verbindet ohne USB nicht | 100 µF Elko an 3.3V/GND? arduino-esp32 ≥3.3.8 (dieser Fork)? |
| AP öffnet sich nach Reboot | HomeKit-Kopplung verloren → erneut koppeln |
| 1-Wire Timing-Probleme | Kabellänge < 30 m halten. Nur einen Bus-Teilnehmer gleichzeitig aktiv. |

## iButton ROM Kurzreferenz

DS1990A ROM-Struktur (8 Bytes):
```
Byte 0:    0x01              (Familiencode DS1990A — immer 0x01)
Bytes 1-6: issuerId[0..5]    (erste 6 Bytes der HomeKey issuerId)
Byte 7:    CRC8              (Dallas CRC, automatisch berechnet)
```

Beispiel:
```
issuerId:  f2 96 35 48 4b 2e 48 ...
ROM:       01 f2 96 35 48 4b 2e AA   (AA = berechnetes CRC8)
```
