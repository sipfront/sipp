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

### Load test MQTT broker

https://github.com/etactica/mqtt-malaria

### Generate mosquitto password

```
sudo mosquitto_passwd -c /etc/mosquitto/passwd sipp
```

### Setting call rate via MQTT

```
mosquitto_pub -h localhost -u sipp -P $password -t /sipp/ctrl -m "cset rate 10"
```

### Mosquitto config (without ssl)

```
cat /etc/mosquitto/conf.d/default.conf
```
```
allow_anonymous false
password_file /etc/mosquitto/passwd

listener 1883 localhost

listener 8083
protocol websockets
```

### Test MQTT over websocket

https://www.eclipse.org/paho/clients/js/utility/

## Docker Howto

### Building and testing locally

```
./agranig-build-local.sh

docker run -it -p 5060/udp -p 5060-5061/tcp -e SFC_CREDENTIALS_API='https://api.sipfront.com' -e SFC_SESSION_UUID='ecabb78c-5900-4c38-a63b-4e54ecadf642' -e SFC_TARGET='18.185.42.196' -e SFC_SCENARIO='uac-register.xml' -e SFC_CREDENTIALS_CALLER=1 -e SFC_CREDENTIALS_CALLER_SEQ='SEQUENTIAL' sipfront-sipp:latest /bin/sipfront-run
```

### Building and pushing remotely

```
./agranig-build.sh
```


