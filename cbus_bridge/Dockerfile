FROM python:3.11-alpine

WORKDIR /app

COPY . /app

RUN pip install --no-cache-dir paho-mqtt pyserial

CMD ["python", "bridge.py"]
