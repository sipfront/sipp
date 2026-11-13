MQTT
====

SIPp can publish what a run measures to an MQTT broker while it runs, for a
test that is started and watched from somewhere else. It needs libmosquitto
at build time (``cmake . -DUSE_MQTT=1``).

The connection
--------------

One connection is made before the traffic starts and closed after the last
statistics went out.

* A broker that cannot be reached within ``-mqtt_connect_timeout`` seconds
  (10 by default), or that refuses the connection, is a fatal error: SIPp
  does not start a run nobody can see.
* A connection lost later is made again by itself, after 1, 2, 4 ... up to
  30 seconds. SIPp goes on meanwhile; what it cannot publish is counted and
  said in a warning, at most once every five seconds.
* On exit SIPp waits for the broker to take what is still queued, for
  ``-mqtt_shutdelay`` seconds at most (3 by default).

================================  ==========================================
Option                            Meaning
================================  ==========================================
``-mqtt_host``, ``-mqtt_port``    The broker. Default ``localhost``, 1883.
``-mqtt_user``, ``-mqtt_pass``    Credentials, if the broker wants them.
``-mqtt_ca_file``                 CA certificates to check the broker's
                                  certificate against; turns TLS on.
``-mqtt_tls_alpn``                The TLS ALPN protocol to ask for.
``-mqtt_client_id``               The client id. Default is a random one.
``-mqtt_keepalive``               Seconds, 5 at least. Default 5.
``-mqtt_pub_qos``                 0, 1 or 2. Default 1.
================================  ==========================================

Statistics
----------

``-mqtt_stats 1`` publishes three messages every ``-fd`` interval and once
more at the end. The ``-trace_stat`` file is not written then.

All values are strings. ``CurrentTime`` is ``YYYY-MM-DD HH:MM:SS.uuuuuu`` in
local time in every message, whatever ``-rfc3339`` and ``-stat_delimiter``
are set to.

``-mqtt_stats_topic`` (default ``/sipp/stats``)
    The columns of the :doc:`statistics file <statistics>` as one object:
    the column names are the keys. ``StartTime`` and ``LastResetTime`` have
    the format of ``CurrentTime``.

    .. code-block:: json

       {"StartTime":"2026-11-13 18:45:49.419844",
        "CurrentTime":"2026-11-13 18:45:53.954387",
        "ElapsedTime(C)":"00:00:04", "TargetRate":"5.000",
        "CallRate(C)":"4.411", "TotalCallCreated":"20", "CurrentCall":"0",
        "SuccessfulCall(C)":"20", "FailedCall(C)":"0",
        "ResponseTime1(C)":"00:00:00:004000", "...":"..."}

``-mqtt_countstats_topic`` (default ``/sipp/countstats``)
    What ``-trace_counts`` counts, one object per message of the scenario,
    in its order. ``Type`` is ``sendreq``, ``sendres``, ``recvreq``,
    ``recvres``, ``pause``, ``nop``, ``recvcmd`` or ``sendcmd``.

    .. code-block:: json

       {"CurrentTime":"2026-11-13 18:45:53.954651",
        "ElapsedTime":"00:00:04:534000",
        "Counts":[
          {"Type":"sendreq","Name":"INVITE","Sent":"20","Retrans":"0","Timeout":"0"},
          {"Type":"recvres","Name":"180","Recv":"20","Retrans":"0","Timeout":"0","Unexp":"0"},
          {"Type":"pause","Sessions":"0","Unexp":"0"}]}

``-mqtt_codestats_topic`` (default ``/sipp/codestats``)
    The response codes of the unexpected messages since the last interval,
    as numbers.

    .. code-block:: json

       {"CurrentTime":"2026-11-13 18:45:53.954701",
        "ElapsedTime":"00:00:04:534000", "ErrorCodes":[503,503,486]}

Remote control
--------------

``-mqtt_ctrl 1`` makes SIPp subscribe to ``-mqtt_ctrl_topic`` (default
``/sipp/ctrl``, QoS ``-mqtt_sub_qos``) and take each message as the
:doc:`control socket <controlling>` takes a datagram: a key as on the
keyboard, such as ``q``, or ``c`` followed by a command, such as
``cset rate 10`` or ``cset limit 500``.

The messages are run by the main loop on its next pass, in the order they
came. Should they come faster than they can be run, the oldest of more than
256 waiting are dropped and a warning says how many. A message longer than
the control socket could carry (65535 bytes) is dropped the same way.

SIPp does not start before the broker has granted the subscription: a
refusal, or no answer within ``-mqtt_connect_timeout``, is an error. A
refusal after a reconnect during the run is a warning.
