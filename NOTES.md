# agranig developer notes towards MVP

## User stories

* as an operator, I want to load a scenario via mqtt
* as an operator, I want to receive error logs via mqtt
* as an operator, when sending 'q' I want a final report (currently erroring out with bad fd)

* DONE - as an operator, I want to configure the mqtt password as sipp cmd line args
* DONE - as an operator, I want to control sipp via mqtt
* DONE - as an operator, I want to receive stats via mqtt
* DONE - as an operator, I want to launch an idle sipp instance

## Overall sipp features to look at

* use -plugins to hook mqtt output?
* what exactly is -DUSE_GSL=1?

* use -set and/or -key to utilize variable support

* what's -ip_field doing and is it useful?

* -trace_rtt for RTT is different than stats, look if it's valuable

## Useful startup options

* use -aa by default?

## How To

### Generate mosquitto password

```
sudo mosquitto_passwd -c /etc/mosquitto/passwd sipp
```

### Setting call rate via MQTT

```
mosquitto_pub -h localhost -u sipp -P $password -t /sipp/ctrl -m "cset rate 10"
```

