# PC ↔ stand protocol

Physical link: USB, virtual COM port (USB CDC of the STM32H723). The baud rate
setting of the port is ignored by USB CDC (use 115200). One message per line,
ASCII, terminated by `\n` (`\r` is ignored). Commands are case-insensitive.

## Commands (PC → stand)

| Command | Description | Answer |
|---|---|---|
| `PING` | check the link | `#OK PONG` |
| `INFO` | firmware name/version, chip UID, parameter source | `#INFO fw=…;ver=…;uid=…;params=eeprom\|defaults` |
| `HELP` | short list of commands | several `#OK` lines |
| `STREAM <ms>` | telemetry period: 0 = off, 100…10000 | `#OK STREAM 1000` |
| `MODE AUTO` / `MODE MANUAL` | operating mode | `#OK MODE …` + `#EVT MODE …` |
| `STOP` | safe state: all outputs off, MANUAL | `#OK STOP` |
| `OUT K<n> 0\|1` | relay K1..K5 (MANUAL only, interlocks apply) | `#OK OUT K2 1` or `#ERR 3 interlock: no water` |
| `PWM <n> <0..100>` | PWM output PW1..PW4, % (MANUAL) | `#OK PWM 1 40` |
| `RGB <RRGGBB>` | colour of the WS2812 strip (MANUAL) | `#OK RGB FF60A0` |
| `BUZ <ms>` | buzzer, up to 5000 ms | `#OK BUZ 200` |
| `PARS` | all parameters | `#PAR …` lines + `#OK PARS 36` |
| `GET <name>` / `SET <name> <value>` | read / change a parameter (RAM) | `#PAR …` or `#ERR 2 value out of range` |
| `SAVE` / `DEFAULTS` | write parameters to EEPROM / load defaults | `#OK SAVE` |
| `TIME [YYYY-MM-DD hh:mm:ss]` | read / set the RTC | `#OK TIME …` |
| `CAL W1\|W2 DRY\|WET` | store the current sensor voltage as a calibration point | `#OK CAL W1 DRY 2790 mV` |
| `FAULT <name> 0\|1` | fault injection: T1 T2 T3 RH W1 W2 TANK F030 RS485 | `#OK FAULT TANK 1` |
| `FAULTS` | list of injected faults | `#OK FAULTS T1=0 …` |
| `MON 485\|SPI 0\|1` | bus monitor RS-485 / M8SPI | `#BUS …` lines |
| `NODES` | state of RS-485 nodes | `#NODE 1 ok=1;type=30;id=C021;err=0;v=724/1` |
| `ERRCLR` | clear the error word (it is recalculated every second) | `#OK ERRCLR` |

Error codes: `1` unknown command, `2` wrong argument/range, `3` not allowed
(mode or interlock), `4` unknown name, `5` hardware (EEPROM/RTC) error.

## Messages (stand → PC)

`#TEL` – telemetry, `key=value` pairs separated by `;`, `-` = no valid value:

```
#TEL ms=14738;time=2026-11-27 10:15:08;mode=AUTO;T1=29.30;T2=22.80;T3=20.80;T4=-;…;
TA=29.7;RH=48.6;W1=59.3;W2=56.3;LIGHT=49.3;UV=444;CO2=788;LVL=1;FULL=0;
A1=1911;…;A12=49;K=10000;P1=41;P2=0;P3=0;P4=0;RGB=000000;WAT=2;UVD=4.1;F030=1;ERR=02000000
```

| Key | Meaning |
|---|---|
| T1…T8 | DS18B20 temperatures, °C (T1 air, T2 soil, T3 water) |
| TA, RH | AM2301A temperature °C, relative humidity % |
| W1, W2 | soil moisture % after calibration |
| LIGHT, UV | light level %, UV module voltage mV |
| CO2 | ppm from the RS-485 CO₂ node |
| LVL, FULL | water above MIN electrode, tank full |
| A1…A12 | voltage at terminal inputs, mV |
| K | relays K1..K5 as a string, `10000` = K1 on |
| P1…P4 | PWM duty, % |
| WAT | watering state: 0 idle, 1 pump, 2 soak pause |
| UVD | accumulated UV dose today |
| ERR | error word, hex (see below) |

`#EVT` – events: relay switching with the reason (`#EVT K1 ON T1=25.3 < 25.5`),
alarms, mode changes, faults, keys. `#BUS 485 TX 53 14 01 00 08 00 00 01 4F` – bus monitor.

## Error word

| Bit | Meaning |
|---|---|
| 00000004 | temperature sensor (DS) not ready |
| 00000010 | an RS-485 module does not answer |
| 00004000 | some soil moisture sensors absent |
| 00008000 | all soil moisture sensors absent |
| 00010000 | tank: no water |
| 01000000 | no link with STM32F030 (M8SPI) |
| 02000000 | EEPROM image invalid, defaults are used |
| 04000000 | RTC not answering |
| 08000000 | overheat (> SAFE_TMAX) |
| 10000000 | AM2301A absent |

Low bits follow the error table of the D2.GreenHouse manual; bits 24–28 are specific to the stand firmware.
