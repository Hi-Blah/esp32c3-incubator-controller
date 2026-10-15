# Hardware, BOM and wiring

![Signal wiring](../assets/wiring-signals.svg)

![DC power wiring](../assets/wiring-power.svg)

These are connection guides, not physical header-position drawings. Verify each board's printed labels and the MOS module topology.

| Component | Quantity | Required characteristics |
|---|---:|---|
| ESP32-C3 SuperMini | 1 | 4 MB Flash, USB data interface; verify the actual variant |
| MOS trigger switch module | 2 | Nonisolated low-side DC switch, 3.3 V-compatible input, suitable load and PWM rating |
| LED light source | 2 circuits | Constant-voltage type matching the supply; current limited by its intended resistor/driver |
| DC supply | 1 | Load-rated 24 V example; sufficient current and thermal headroom |
| DHT11 module | 1 optional | 3.3 V-compatible supply and DATA; the firmware uses a bounded RMT DHT11 decoder |
| SSD1306 I2C OLED | 1 optional | 128×64, 0x3c or 0x3d, 3.3 V-compatible |
| LDR AO module | 2 optional | VCC/GND/AO/DO type; powered at 3.3 V; AO never exceeds 3.3 V |
| Buttons | 1 optional | Page button to GND; internal pull-up |
| Fuses, terminals, wire, enclosure | As required | Rated for measured current; insulated strain relief and separate load returns |
| Input pull-down resistors | 2 as required | About 10 kΩ at MOS control inputs if the module does not already provide a suitable pull-down |

| Signal | GPIO / connection |
|---|---|
| MOS control 1 / 2 | GPIO4 / GPIO7 |
| OLED SDA / SCL | GPIO6 / GPIO5 |
| DHT11 DATA | GPIO21; USB CDC must be enabled |
| LDR AO 1 / 2 | GPIO0 / GPIO1, ADC1; DO unused |
| Page button | GPIO10 to GND |
| Optional channel buttons | GPIO0 / GPIO1 only when photo support is compiled out |

USB 5 V powers the ESP32. A separate 24 V supply powers the lights. With the verified nonisolated MOS topology, join signal ground to supply negative at a suitable star point. Keep high-current returns out of breadboards and ESP32 traces. MOS OUT- is a switched return: never join it directly to common ground or the other channel's OUT-. Use branch fuses and verify DC+/DC-/OUT+/OUT- labels.

Example sizing: two 24 V, 20 W/m strips at 4.9 m each require about 98 W / 4.08 A per channel and 196 W / 8.17 A total. A 200 W supply would have almost no headroom; two pairs of output wires do not double its rating. Actual current, wire, fuse and enclosure temperature must be measured. Manufacturer headline current/PWM limits are not a passed thermal test.

Do not short strip resistors to test leakage: they limit current. Off-state glow can result from leakage or an unintended return path; software OFF is not electrical isolation. Never connect 24 V or a 5 V sensor output to an ESP32 input.

INA226 is not implemented. An R100 shunt is 0.1 Ω: the INA226 ±81.92 mV shunt range corresponds to about 0.819 A, so that board is unsuitable as-is for a roughly 4 A channel. See the [TI datasheet](https://www.ti.com/lit/ds/symlink/ina226.pdf). Hardware fuses remain necessary.

