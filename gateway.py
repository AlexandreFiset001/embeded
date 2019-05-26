#!/usr/bin/python
import serial #include the module for the serial port API
import paho.mqtt.client as mqtt #include the module for mqtt client publisher API
from threading import Thread, Semaphore #including all the modules to have a multi-threading API

# shared variables
buff = {}
i_writter = 0
i_reader = 0
buff_size = 100
buff_sem = Semaphore()

#defenitions of thread function
def serial_connect :
    ser = serial.Serial(
        port='/dev/ttyUSB0',\
        baudrate=115200,\
        parity=serial.PARITY_NONE,\
        stopbits=serial.STOPBITS_ONE,\
        bytesize=serial.EIGHTBITS,\
        timeout=0)
    print("connected to: " + ser.portstr)
    while True:
        line = ser.readline();
        if line:
            print("recived from"+ser.portstr " : "+line)
    ser.close()

def mqtt_publisher :
    broker_address="192.168.1.184"
    # if we want to use the online mosquitto publisher provided by eclipse
    #broker_address="iot.eclipse.org"
    print("creating new instance of publisher")
    client = mqtt.Client("P1") #create new instance
    print("connecting to broker")
    client.connect(broker_address) #connect to broker
    while True:
        print("Publishing message to topic : "+topic_name)
        client.publish(topic_name,msg)


# creation of thread
t = Thread(target = serial_connect, args = ())
t.start()
t = Thread(target = mqtt_publisher, args = ())
t.start()
