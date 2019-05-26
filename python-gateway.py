#!/usr/bin/python
import serial #include the module for the serial port API
import paho.mqtt.client as mqtt #include the module for mqtt client publisher API
from threading import Thread, Semaphore #including all the modules to have a multi-threading API

# shared variables
ser = serial.Serial(
    port='/dev/ttyUSB0',\ #connect to the ttyUSB0 serial port
    baudrate=115200,\
    parity=serial.PARITY_NONE,\
    stopbits=serial.STOPBITS_ONE,\
    bytesize=serial.EIGHTBITS,\
    timeout=0)
print("connected to: " + ser.portstr)


#defenitions of thread function

def mqtt_subscriber :
	## I could not implement this function since I was not able to find how to connect to the log messages
    

def mqtt_publisher :
    #use a local MQTT mosiquito server
    broker_address="127.0.0.1"
    # if we want to use the online mosquitto publisher provided by eclipse
    #broker_address="iot.eclipse.org"
    print("creating new instance of publisher")
    client = mqtt.Client("P1") #create new instance
    print("connecting to broker")
    client.connect(broker_address) #connect to broker
    while True:
		line = ser.readline()
		temp = line.split(" ")
        print("Publishing message to topic : "+temp[0]+" "+temp[1])
        client.publish(temp[0],temp[1])


# creation of thread
t = Thread(target = serial_connect, args = ())
t.start()
t = Thread(target = mqtt_publisher, args = ())
t.start()
