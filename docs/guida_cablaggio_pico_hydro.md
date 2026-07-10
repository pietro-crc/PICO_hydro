# PICO Hydro — guida di cablaggio

Schema principale: [schema_elettrico_pico_hydro.svg](schema_elettrico_pico_hydro.svg)

## Pinout verificato nel firmware

| Funzione | GPIO Pico | Pin fisico Pico | Collegamento |
|---|---:|---:|---|
| LCD SDA | GP0 | 1 | PCF8574 SDA tramite level shifter se LCD a 5 V |
| LCD SCL | GP1 | 2 | PCF8574 SCL tramite level shifter se LCD a 5 V |
| Livello | GP2 | 4 | Uscita digitale, massimo 3,3 V |
| WS2812 DIN | GP3 | 5 | 74AHCT125, poi 330 Ω verso DIN |
| Flussometro | GP4 | 6 | Divisore 10 kΩ / 20 kΩ se uscita a 5 V |
| DHT11 DATA | GP5 | 7 | Pull-up 4,7 kΩ verso 3,3 V |
| VEML7700 SDA | GP6 | 9 | I²C1 a 3,3 V |
| VEML7700 SCL | GP7 | 10 | I²C1 a 3,3 V |
| DS18B20 DATA | GP8 | 11 | Due sonde sullo stesso bus, pull-up 4,7 kΩ |
| ESP8266 RX | GP12 TX | 16 | Pico TX verso ESP RX |
| ESP8266 TX | GP13 RX | 17 | ESP TX verso Pico RX |
| Relè | GP15 | 20 | Base NPN tramite 1 kΩ; 10 kΩ base-GND |
| TDS analogico | GP26/ADC0 | 31 | AO tramite 1 kΩ; 100 nF verso AGND |
| Massa analogica | AGND | 33 | Ritorno TDS |
| 3,3 V Pico | 3V3 OUT | 36 | Sensori e pull-up leggeri |
| Alimentazione Pico | VSYS | 39 | 5 V esterni tramite Schottky consigliato |

Il LED di stato è quello integrato sul Pico, GP25: non richiede cablaggio.

## Alimentazione consigliata

- Linea `+5V_ELETTRONICA`: Pico VSYS, LCD, WS2812, modulo relè e TDS se previsto dal suo produttore.
- Linea `+3V3_ESP`: regolatore separato da almeno 500 mA per ESP8266, con 100 µF e 100 nF vicino al modulo.
- Linea `3V3 Pico`: DHT11, VEML7700, DS18B20 e pull-up. Non usarla per ESP8266 o relè.
- Alimentazione pompa separata e dimensionata sulla targhetta della pompa. Collegare soltanto la massa in comune se il circuito di comando lo richiede.
- Unire le masse in un punto stella. Tenere i ritorni di pompa, relè e WS2812 lontani dalla massa analogica TDS.

## Componenti di interfaccia

| Riferimento | Componente | Uso |
|---|---|---|
| Q1 | BC337 o 2N2222 | Pilotaggio ingresso modulo relè |
| R1 | 1 kΩ | GP15 verso base Q1 |
| R2 | 10 kΩ | Base Q1 verso GND |
| R3/R4 | 10 kΩ / 20 kΩ | Riduzione 5 V → circa 3,3 V per flusso o livello |
| R5 | 330 Ω | Serie sul dato WS2812 |
| R6 | 4,7 kΩ | Pull-up DHT11 |
| R7 | 4,7 kΩ | Pull-up bus DS18B20 |
| R8 | 1 kΩ | Protezione serie ingresso TDS |
| C1 | 1000 µF, ≥6,3 V | Vicino all’ingresso della striscia WS2812 |
| C2 | 100 µF, ≥6,3 V | Vicino all’ESP8266 |
| C3/C4 | 100 nF ceramici | ESP8266 e filtro ADC TDS |
| U1 | Regolatore 3,3 V, ≥500 mA | Alimentazione ESP8266 |
| U2 | 74AHCT125 | Conversione dato WS2812 3,3 V → 5 V |
| U3 | Level shifter I²C BSS138 | LCD a 5 V ↔ Pico a 3,3 V |

## Controlli prima della saldatura definitiva

1. Identificare con certezza `VCC`, `GND`, `IN/OUT`, `TX/RX`, `SDA/SCL` su ogni modulo: l’ordine dei pin cambia tra produttori.
2. Misurare a banco l’uscita del sensore livello e del flussometro. Se supera 3,3 V, montare il divisore.
3. Misurare il massimo di `AO` del TDS: non deve mai superare 3,3 V.
4. Verificare se il backpack LCD porta SDA/SCL a 5 V. In tal caso il level shifter è obbligatorio.
5. Controllare continuità di tutte le masse e assenza di corto tra 5 V, 3,3 V e GND.
6. Accendere inizialmente senza Pico, ESP8266, pompa e sonde inseriti; verificare prima le tensioni sui connettori.
7. Inserire il Pico e provare i sensori; collegare pompa e relè soltanto come ultimo passaggio.

## Assunzioni da verificare sul materiale reale

- Il relè è un modulo con pin `VCC/GND/IN`, pilotato dall’NPN già montato, non una bobina nuda.
- Il sensore livello fornisce un’uscita digitale e nel firmware acqua presente corrisponde a livello HIGH.
- Il modulo TDS ha uscita analogica compatibile con un ADC da 3,3 V.
- LCD e WS2812 sono alimentati a 5 V.
- La pompa è in bassa tensione DC. Per tensione di rete serve progettazione e contenitore certificati separati dalla logica.

## Fonti tecniche

- Raspberry Pi Pico pinout e alimentazione: https://www.raspberrypi.com/documentation/microcontrollers/pico-series.html
- Raspberry Pi Pico datasheet: https://datasheets.raspberrypi.com/pico/pico-datasheet.pdf
- Alimentazione ESP8266: https://docs.espressif.com/projects/esp-faq/en/latest/hardware-related/hardware-design.html
