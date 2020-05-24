# agranig developer notes towards MVP

## User stories

* as an operator, I want to launch an idle sipp instance
* as an operator, I want to load a scenario via mqtt
* as an operator, I want to control sipp via mqtt
* as an operator, I want to receive stats via mqtt

* as an operator, I want to configure the mqtt password as sipp cmd line args

## Overall sipp features to look at

* use -plugins to hook mqtt output?
* what exactly is -DUSE_GSL=1?

* use -set and/or -key to utilize variable support

* what's -ip_field doing and is it useful?

* -trace_rtt for RTT is different than stats, look if it's valuable

## Useful startup options

* use -aa by default?

## TODO

* properly connect to mqtt in setup_mqtt_socket, see error log in /var/log/mosquitto/mosquitto.log:

```
1590313009: Socket error on client mosq/e8l]uEs=E<vrMohP1r, disconnecting.
1590313050: New connection from 127.0.0.1 on port 1883.
1590313050: New client connected from 127.0.0.1 as mosq/9nVhLKM<5HrTG74WoO (c1, k3).
1590313054: Client mosq/9nVhLKM<5HrTG74WoO has exceeded timeout, disconnecting.
```
