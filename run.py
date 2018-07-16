# coding=utf-8

import serial

wifi_ssid = "WikS_mobileH"
wifi_passwd = "***"  # TODO put here your WiFi password

usb_as_serial = '/dev/ttyACM0'

ser = serial.Serial(usb_as_serial, 115200, timeout=2)
ser.flushInput()
ser.flushOutput()

while True:
    data_raw = ser.readline()
    if len(data_raw) > 0:
        print(data_raw)
        if "wifi credientals" in data_raw:
            toprint = "wifi:" + wifi_ssid + ":" + wifi_passwd + ":wifi"
            print toprint
            ser.write(toprint)

# ---
# run from Terminal in this way (as user allowed to use mentioned serial-port):
# $ python run.py
