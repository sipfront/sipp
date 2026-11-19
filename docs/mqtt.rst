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

``-mqtt_rttstats_topic`` (default ``/sipp/rttstats``), with ``-trace_rtt``
    The response times of the interval, per timer (``rtd``): how many there
    were and, in milliseconds, their minimum, mean, median, 90th, 95th and
    99th percentile and maximum. An interval without a response time
    publishes nothing. ``Scenario`` says whose timers they are: ``main``,
    ``rx`` (the scenario for the calls that come in, in mixed mode) or
    ``ooc`` (the out-of-call scenario); each has a message of its own. The ``-trace_rtt`` file, which lists every single
    time, is not written: a load test has thousands of them a second, and
    a message that listed them would outgrow what brokers take.

    .. code-block:: json

       {"CurrentTime":"2026-11-13 20:47:43.814747",
        "ElapsedTime":"00:00:01:027000", "Scenario":"main",
        "ResponseTimes":[
          {"Name":"1","Count":"49","Min":"3.488","Mean":"4.259","P50":"4.039",
           "P90":"5.190","P95":"5.328","P99":"7.680","Max":"7.680"}]}

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

Errors
------

With ``-mqtt_stats``, ``-trace_err`` publishes the errors and warnings to
``-mqtt_error_topic`` (default ``/sipp/errors``) instead of writing them to
the error log. A fatal error is also printed to stderr, as always.

.. code-block:: json

   {"CurrentTime":"2026-11-13 16:59:16.818746",
    "Content":"Call-Id: 1-5240@127.0.0.1, receive timeout on message ...",
    "Fatal":"0", "Total":"1", "Suppressed":"0",
    "ErrnoString":"", "Errno":"0"}

A load test that fails has an error per call. So at most
``-mqtt_error_rate`` errors (2 by default, 0 for no limit) are published a
second; the others are counted, and the next one published says how many
were held back since the one before it in ``Suppressed``. ``Total`` counts
all of them. A fatal error is always published.

``Content`` is cut after ``-mqtt_error_max_len`` bytes (4096 by default, 0
for no limit), which leaves an unexpected SIP message readable.

SIP messages
------------

With ``-mqtt_stats``, ``-trace_msg`` publishes every SIP message sent and
received to ``-mqtt_message_topic`` (default ``/sipp/messages``), next to
writing it to its log; ``-trace_shortmsg`` alone publishes the first line of
each. This is one MQTT message per SIP message: for following single calls,
not for load.

.. code-block:: json

   {"CurrentTime":"2026-11-13 16:59:16.818746", "CallId":"1-5240@127.0.0.1",
    "Direction":"recv", "Transport":"UDP", "Type":"",
    "Message":"SIP/2.0 200 OK", "Size":"512"}

``Direction`` is ``send`` or ``recv``; ``Type`` is ``control`` for a message
on the 3PCC control connection.
