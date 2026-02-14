# OAM-DoorController

OpenKNX Firmware und Anwendung zur OpenKNX-kompatiblen Anbindung von elektrischen Türantrieben und Sensoren.

## Inbetriebnahme

Die Inbetriebnahme geht am einfachsten mit der [OpenKNX Toolbox](https://openknx.atlassian.net/wiki/spaces/OpenKNX/pages/66295/OpenKNX-Toolbox).

## Kompatible Türantriebe und Sensoren

Aktuell wird folgender Türantrieb unterstützt:
- dormakaba CS 80 Magneo

Zudem werden folgende Sensoren unterstützt:
- Öffnungs-/Absicherungssensor BEA IXIO-DT1
- Öffnungs-/Absicherungssensor BEA IXIO-DT3
- Absicherungssensor BEA IXIO ST

Weitere Türantriebe, auch anderer Hersteller, sowie Sensoren können bei Bedarf angebunden werden.

Es werden bis zu zwei Absicherungssensoren gleichzeitig zur Absicherung der Hauptschließkante (HSK) und Nebenschließkante (NSK) unterstützt.

Eine elektromagnetische Verriegelung (24 V) kann ebenfalls angeschlossen werden.

## Kompatible Hardware

Die OpenKNX-kompatible Anbindung erfolgt über eine Steuerplatine, an welcher der Türantrieb sowie etwaige Sensoren angeschlossen werden.

Eine Funktionsbeschreibung sowie kompatible Hardware findet sich [hier](https://openknx.atlassian.net/wiki/spaces/OpenKNX/pages/8257600/Ger+te+bersicht).