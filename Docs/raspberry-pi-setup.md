# Raspberry Pi + Home Assistant Setup Guide

Quick, tested guide for a fresh Raspberry Pi (Raspbian/Raspberry Pi OS 64-bit recommended).

### 1. Install Raspberry Pi OS
- Download **Raspberry Pi OS (64-bit) with desktop** from [raspberrypi.com/software](https://www.raspberrypi.com/software/).
- Flash to SD card using **Raspberry Pi Imager**.
  - In advanced settings: Enable SSH, set a strong username/password, configure Wi-Fi, locale, and timezone.
- Boot the Pi, complete the initial setup wizard (if using monitor), then update the system:

  sudo apt update && sudo apt full-upgrade -y && sudo reboot

2. Install Dependencies
MQTT broker (Mosquitto):
Bashsudo apt install mosquitto mosquitto-clients -y
Python libraries for the bridge:

sudo apt install python3-serial python3-paho-mqtt -y

3. Install Docker & Run Home Assistant

sudo apt install docker.io docker-compose -y
sudo usermod -aG docker pi   # Then log out/in or reboot for changes to take effect

Create a folder for HA config:

mkdir ~/ha && cd ~/ha

Create docker-compose.yml (use nano or your favorite editor):

YAMLversion: '3'
services:
  homeassistant:
    container_name: home-assistant
    image: ghcr.io/home-assistant/home-assistant:stable
    volumes:
      - /home/pi/ha/config:/config
      - /etc/localtime:/etc/localtime:ro
    restart: unless-stopped
    privileged: true
    network_mode: host
    environment:
      - TZ=Australia/Sydney   # Change to your timezone (e.g., Europe/London)

Start Home Assistant:

docker-compose up -d

Access it in your browser: http://<your-pi-ip>:8123 (find IP with hostname -I).

4. Deploy the MQTT Bridge

Copy the bridge/ folder to your Pi (e.g., via SCP, USB, or Git clone: scp -r bridge/ pi@your-pi-ip:/home/pi/).
Edit bridge/lights_config.json with your light names and dimmable flags.
Test the bridge:Bashpython3 /home/pi/bridge/bridge.py
For auto-start on boot: Create a systemd service (optional—let me know if you need the exact file).

5. Connect & Test

Plug the Arduino Mega into the Pi's USB port (it should appear as /dev/ttyACM0—the bridge handles it).
In Home Assistant: Settings → Devices & Services → Add Integration → Search "MQTT" → Configure broker as localhost:1883.
Your lights should auto-discover and appear as entities (thanks to discovery in bridge.py).

Final tip: Image your SD card for a backup once everything's working!
Done—you're live with Home Assistant controlling the C-Bus Killer.
