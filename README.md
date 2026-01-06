# C-Bus Killer: DIY 20-Channel Home Lighting Controller

Born out of **spite and desperation**.

My original Clipsal C-Bus lighting system was dying a slow, expensive death. It would work perfectly for a week or two after I sprayed contact cleaner everywhere, then rain would hit (or humidity, or just bad luck) and the whole house would go haywire — lights flickering, zones failing, total chaos. Replacing switches and modules with official Clipsal parts? Ridiculous prices.

So I sat down, wrote a lot of code, and built this beast instead.

**C-Bus Killer** is what happens when you refuse to pay proprietary hell prices and decide to take control yourself. It reuses the existing Cat5 daisy-chain wiring, supports everything the old C-Bus could do (and more), and adds reliable bidirectional app sync, physical dimming, timers, and Home Assistant integration.

It's not pretty, it's not simple, and it's definitely over-engineered for my house — but **it works great**, has been rock-solid for years, and gets me out of the Clipsal trap.

One day I might rewrite a saner, cleaner version. Until then: if it ain't broken, don't fix it.

I hope this messy but functional project helps someone else escape their own dying proprietary lighting system and feel like Tony Stark when you ask Seri to turn your lights on and off for you. 

## ⚠️ Important Disclaimers

- This was built **specifically for my own house and wiring**. It's shared as-is for inspiration or adaptation.
- **Not an instructional guide or beginner tutorial** — I'm not an expert.
- Working with 240V AC is **dangerous**. You are 100% responsible for your safety, wiring, and compliance with local electrical codes.
- No warranties, no support, no liability. Use/modify at your own risk.
- Test thoroughly. Your house may burn down if you get it wrong.

## Features at a Glance

- 20 relay channels (lights + fans)
- Up to 20 physical switches across 8 I2C MCP23017 plates
- Many-to-many switch ↔ channel mappings
- One smooth dimmable channel with zero-cross triac control (wall ↔ app sync)
- Timers (per-channel & global), EEPROM persistence
- Physical switch logic: short press = toggle, hold = dim, long hold = global timer
- Full MQTT integration (perfect for Home Assistant auto-discovery)

## Hardware Overview

- **Arduino Mega 2560** (high pin count + memory)
- **8× MCP23017** I2C I/O expanders (switch inputs + LED outputs)
- **20× SSRs** (2A for lights) + mechanical relays (10A for fans)
- **Dimming**: Only channel 5 (pin 9 PWM, pin 2 zero-cross interrupt)
- **Wiring**: Reuses existing Cat5 daisy-chain from switchboard to plates and back. 

See `docs/reference.pdf` for the full original analysis doc (very detailed).

## Software

- **Firmware**: `firmware/main/FullRewriteV2.5.3.ino` — Upload via Arduino IDE
- **Testing Sketches**: `firmware/tools/` — Various debugging helpers
- **MQTT Bridge**: `bridge/bridge.py` + `bridge/lights_config.json` (generic names included)
- Runs on Raspberry Pi, bridges Arduino serial → MQTT → Home Assistant

## Quick Setup Overview

1. Upload the Arduino sketch
2. Set up Raspberry Pi with Raspbian, Docker, Home Assistant, Mosquitto
3. Copy & configure `bridge/` files (edit JSON with your light names)
4. Run bridge.py
5. Lights auto-appear in Home Assistant via MQTT discovery

→ Full step-by-step in: `docs/raspberry-pi-setup.md`

## Simple Usage & Programming Guide

Connect Arduino via USB to computer (Arduino IDE Serial Monitor, 9600 baud).

### Physical Switch Behavior
- Short press (<950ms): Toggle lights
- Hold (≥950ms): Dim (dimmable channel only)
- Long hold (≥3s): Global timer (if off)

LEDs: Solid ON = light on | Flashing = timer/program mode

### Program Mode (Serial Commands)
Enter: `EnterProgramMode`  
Exit & save: `ExitProgramMode`

### Rename Channels
`rename <old> <new>`  
e.g. `rename CH1_4 kitchen` or `rename CH1_5 pendant`

### Map Switches ↔ Lights
`<channel(s)> <switch(es)>`  
e.g. `CH1_4 SW4_1`  
Many-to-many: `CH1_4 CH1_5 SW4_1 SW5_2`

Switches numbered by plate (SW1_1 = plate 1 switch 1)

### Timers
Global: `globaltimer 20` (minutes)  
Per light: `kitchen timer 10`

### Manual Control
ON: `pendant ON 50`  
OFF: `pendant OFF`

Other useful:  
`status` (all) or `kitchen status`  
`clearmappings` (Y/N)  
`clearalltimers`  
`systemreset` (Y/N)

Full details in `docs/reference.pdf`.

## License
MIT License — See [LICENSE](LICENSE) file.

Built by Matt (with a lot of swearing at C-Bus).  
Pull requests welcome if you make it saner!

Happy building — and good luck escaping proprietary lighting purgatory.
