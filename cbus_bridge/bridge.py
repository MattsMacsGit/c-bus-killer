#!/usr/bin/env python3
import serial
import json
import time
import threading
import paho.mqtt.client as mqtt
import logging
from pathlib import Path

SERIAL_PORT = "/dev/ttyACM0"
MQTT_BASE = "home/lights"
MQTT_DISCOVERY_PREFIX = "homeassistant"

cfg = json.load(open(Path(__file__).with_name("lights_config.json")))
lights = [x["name"].lower() for x in cfg]
dimmable = {x["name"].lower(): x["dimmable"] for x in cfg}

# These will appear as fan entities in HA
FANS = {"livingroomfans", "mainbedroomfan", "bedroom2", "bedroom3"}

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(message)s")

ser = None

def connect():
    global ser
    while True:
        try:
            ser = serial.Serial(SERIAL_PORT, 9600, timeout=2)
            ser.setDTR(False)
            time.sleep(0.3)
            ser.flushInput()
            logging.info("Serial connected – no reset")
            return
        except Exception as e:
            logging.warning(f"Serial failed: {e}")
            time.sleep(5)

connect()

def send(cmd):
    global ser
    try:
        ser.write((cmd + "\n").encode())
    except:
        connect()

client = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION2)
client.connect("localhost", 1883, 60)
client.loop_start()

cache = {"pendant": 70}
discovery_sent = set()

def send_discovery(name):
    entity_id = name.lower()
    if entity_id in discovery_sent:
        return
    discovery_sent.add(entity_id)
    # Clean friendly name
    friendly_name = (
        name.replace("bedroom", "Bedroom ")
        .replace("livingroom", "Living Room ")
        .replace("wir", "WIR")
        .replace("peepalace", "Peep Palace")
        .title()
        .replace("Fans", " Fans")
    )
    if entity_id in FANS:
        # Fan entity
        config_topic = f"{MQTT_DISCOVERY_PREFIX}/fan/{entity_id}/config"
        payload = {
            "name": friendly_name,
            "command_topic": f"{MQTT_BASE}/{entity_id}/set/on",
            "state_topic": f"{MQTT_BASE}/{entity_id}/state/on",
            "payload_on": "1",
            "payload_off": "0",
            "unique_id": f"fan_{entity_id}",
            "device": {"identifiers": ["arduino_lights"], "name": "Arduino Lights Controller"},
        }
    elif dimmable.get(entity_id, False):
        # Dimmable light (pendant)
        config_topic = f"{MQTT_DISCOVERY_PREFIX}/light/{entity_id}/config"
        payload = {
            "name": friendly_name,
            "command_topic": f"{MQTT_BASE}/{entity_id}/set/on",
            "state_topic": f"{MQTT_BASE}/{entity_id}/state/on",
            "brightness_command_topic": f"{MQTT_BASE}/{entity_id}/set/brightness",
            "brightness_state_topic": f"{MQTT_BASE}/{entity_id}/state/brightness",
            "brightness_scale": 100,
            "payload_on": "1",
            "payload_off": "0",
            "on_command_type": "brightness",  # Remember last brightness
            "unique_id": f"light_{entity_id}",
            "device": {"identifiers": ["arduino_lights"], "name": "Arduino Lights Controller"},
        }
    else:
        # Regular light
        config_topic = f"{MQTT_DISCOVERY_PREFIX}/light/{entity_id}/config"
        payload = {
            "name": friendly_name,
            "command_topic": f"{MQTT_BASE}/{entity_id}/set/on",
            "state_topic": f"{MQTT_BASE}/{entity_id}/state/on",
            "payload_on": "1",
            "payload_off": "0",
            "unique_id": f"light_{entity_id}",
            "device": {"identifiers": ["arduino_lights"], "name": "Arduino Lights Controller"},
        }
    client.publish(config_topic, json.dumps(payload), retain=True)
    logging.info(f"Sent discovery for {friendly_name}")

def on_msg(c, u, m):
    if "/set/" not in m.topic:
        return
    try:
        name = m.topic.split("/")[2].lower()
        if name not in lights:
            return
        if "brightness" in m.topic and name == "pendant":
            b = max(0, min(100, int(m.payload)))
            cache["pendant"] = b
            send("pendant off" if b == 0 else f"pendant on {b}")
        elif m.topic.endswith("/on"):
            payload = m.payload.decode()
            on = payload in ["1", "true", "on"] or payload.strip() in ['{"on":true}', '{"on": true}']
            if name == "pendant":
                b = 0 if not on else cache["pendant"] or 100
                send("pendant off" if not on else f"pendant on {b}")
            else:
                send(f"{name} {'on' if on else 'off'}")
    except Exception as e:
        logging.error(f"MQTT error: {e}")

client.on_message = on_msg
client.subscribe("home/lights/+/set/#")

def reader():
    buf = b""
    global ser
    while True:
        try:
            if ser.in_waiting:
                buf += ser.read(ser.in_waiting)
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.decode(errors="ignore").strip()
                    if not line or "error" in line.lower():
                        continue
                    p = line.split()
                    if len(p) < 2:
                        continue
                    n = p[0].lower()
                    if n not in lights:
                        continue
                    on = p[1].lower() == "on"
                    bri = int(p[2]) if len(p) > 2 and p[2].isdigit() else (100 if on else 0)
                    # Send discovery on first state update
                    send_discovery(n)
                    if n == "pendant":
                        cache["pendant"] = bri if on else 0
                        # Always publish brightness first – force 0 on off
                        client.publish(f"{MQTT_BASE}/{n}/state/brightness", str(cache["pendant"]), retain=True)
                        # Then simple ON/OFF state
                        client.publish(f"{MQTT_BASE}/{n}/state/on", "1" if (on and cache["pendant"] > 0) else "0", retain=True)
                    else:
                        client.publish(f"{MQTT_BASE}/{n}/state/on", "1" if on else "0", retain=True)
        except:
            connect()
        time.sleep(0.01)

threading.Thread(target=reader, daemon=True).start()

logging.info("Bridge running with HA MQTT auto-discovery – lights + pendant dimmer + 4 fans")

while True:
    time.sleep(10)
