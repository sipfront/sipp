/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 *  Author : Sipfront GmbH
 */

/* The connection to the MQTT broker: see mqtt.hpp for how it is meant to
 * work. Everything here that touches SIPp (WARNING, ERROR, the globals) runs
 * in the main thread; the callbacks, which libmosquitto calls from its own
 * thread, only write the atomics below. */

#ifdef USE_MQTT

#include <atomic>
#include <chrono>
#include <thread>

#include <mosquitto.h>

#include "sipp.hpp"
#include "mqtt.hpp"

/* MOSQ_OPT_TLS_ALPN came with libmosquitto 1.6. */
#if LIBMOSQUITTO_VERSION_NUMBER < 1006000
#error "libmosquitto 1.6 or newer is required"
#endif

static struct mosquitto *mqtt_client = nullptr;

/* Written by libmosquitto's thread, read by the main thread. */
static std::atomic<bool> mqtt_connected(false);
/* The CONNACK code of the last answer to a connect, -1 before the first. */
static std::atomic<int> mqtt_connack(-1);
/* The reason of the last disconnect (a MOSQ_ERR_* value), 0 for none. */
static std::atomic<int> mqtt_disconnect_reason(0);
/* Messages handed to libmosquitto that it has not reported as delivered:
 * sent for QoS 0, acknowledged by the broker for QoS 1 and 2. */
static MqttPending mqtt_pending;

/* Main thread only. */
static bool mqtt_was_connected = false;
static unsigned long mqtt_failed_publishes = 0;
static unsigned long mqtt_last_failure_report = 0;

/* How often, at most, a failing publish is reported (ms). */
static const unsigned long MQTT_FAILURE_REPORT_INTERVAL = 5000;

static void on_connect(struct mosquitto *, void *, int rc)
{
    if (rc == 0 && mqtt_pub_qos == 0) {
        /* What libmosquitto had not written when the connection went is
         * gone with it, and no callback says so: nothing waits now, or
         * what was lost would count against -mqtt_max_queued for good.
         * With QoS 1 and 2 it keeps the messages and reports them when
         * the broker has them. */
        mqtt_pending.reset();
    }
    mqtt_connack = rc;
    mqtt_connected = (rc == 0);
}

static void on_disconnect(struct mosquitto *, void *, int rc)
{
    mqtt_disconnect_reason = rc;
    mqtt_connected = false;
}

static void on_publish(struct mosquitto *, void *, int mid)
{
    /* By its id: a message that is not waited for (one counted off in
     * on_connect) takes no other message's place. */
    mqtt_pending.delivered(mid);
}

int MqttPending::publish(const std::function<int(int *)> &send, const std::function<bool(int)> &kept)
{
    /* No lock while send runs: libmosquitto's thread may report a
     * delivery with locks of its own held that send wants. What it
     * reports meanwhile, this message perhaps, whose id is not known here
     * yet, is put aside in "early". */
    {
        std::lock_guard<std::mutex> lock(mutex);
        sending++;
    }
    int mid = 0;
    int rc = send(&mid);
    std::lock_guard<std::mutex> lock(mutex);
    bool done = early.erase(mid) > 0;
    if (kept(rc) && !done) {
        waiting.insert(mid);
    }
    if (--sending == 0) {
        /* Deliveries of messages nobody waits for. */
        early.clear();
    }
    return rc;
}

void MqttPending::delivered(int mid)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!waiting.erase(mid) && sending > 0) {
        early.insert(mid);
    }
}

void MqttPending::reset()
{
    std::lock_guard<std::mutex> lock(mutex);
    waiting.clear();
}

size_t MqttPending::count()
{
    std::lock_guard<std::mutex> lock(mutex);
    return waiting.size();
}

bool mqtt_publish_kept(int rc, int qos)
{
    /* MOSQ_ERR_NO_CONN for QoS 1 and 2 is the connection that went between
     * our look at it and the call: libmosquitto says no, and has put the
     * message with those it sends when the broker is back. */
    return rc == MOSQ_ERR_SUCCESS || (rc == MOSQ_ERR_NO_CONN && qos > 0);
}

bool mqtt_active()
{
    return mqtt_client != nullptr;
}

/* A change of the connection's state is said once, from the main thread. */
static void mqtt_report_connection()
{
    bool connected = mqtt_connected;
    if (connected == mqtt_was_connected) {
        return;
    }
    mqtt_was_connected = connected;
    if (connected) {
        WARNING("MQTT: connected to the broker '%s:%d' again", mqtt_host, mqtt_port);
    } else {
        WARNING("MQTT: lost the connection to the broker '%s:%d' (%s), trying to connect again", mqtt_host, mqtt_port,
                mosquitto_strerror(mqtt_disconnect_reason));
    }
}

void mqtt_setup()
{
    int rc;

    if (mqtt_client) {
        return;
    }
    if (mqtt_pub_qos < 0 || mqtt_pub_qos > 2) {
        ERROR("-mqtt_pub_qos must be 0, 1 or 2, not %d", mqtt_pub_qos);
    }
    if (mqtt_max_queued < 0) {
        ERROR("-mqtt_max_queued must be 0 (no limit) or more, not %d", mqtt_max_queued);
    }
    if (mqtt_keepalive < 5) {
        /* libmosquitto refuses less. */
        ERROR("-mqtt_keepalive must be at least 5 seconds, not %d", mqtt_keepalive);
    }

    mosquitto_lib_init();
    mqtt_client = mosquitto_new(mqtt_client_id, true, nullptr);
    if (!mqtt_client) {
        ERROR("MQTT: could not create the client: %s", strerror(errno));
    }

    if (mqtt_user) {
        rc = mosquitto_username_pw_set(mqtt_client, mqtt_user, mqtt_pass);
        if (rc != MOSQ_ERR_SUCCESS) {
            ERROR("MQTT: could not set the credentials of user '%s': %s", mqtt_user, mosquitto_strerror(rc));
        }
    }
    if (mqtt_ca_file) {
        rc = mosquitto_tls_set(mqtt_client, mqtt_ca_file, nullptr, nullptr, nullptr, nullptr);
        if (rc != MOSQ_ERR_SUCCESS) {
            ERROR("MQTT: could not use the CA file '%s': %s", mqtt_ca_file, mosquitto_strerror(rc));
        }
    }
    if (mqtt_tls_alpn) {
        rc = mosquitto_string_option(mqtt_client, MOSQ_OPT_TLS_ALPN, mqtt_tls_alpn);
        if (rc != MOSQ_ERR_SUCCESS) {
            ERROR("MQTT: could not set the TLS ALPN '%s': %s", mqtt_tls_alpn, mosquitto_strerror(rc));
        }
    }

    mosquitto_connect_callback_set(mqtt_client, on_connect);
    mosquitto_disconnect_callback_set(mqtt_client, on_disconnect);
    mosquitto_publish_callback_set(mqtt_client, on_publish);
    /* A lost connection is made again after 1 s, then 2, 4, ... up to 30. */
    mosquitto_reconnect_delay_set(mqtt_client, 1, 30, true);

    rc = mosquitto_connect_async(mqtt_client, mqtt_host, mqtt_port, mqtt_keepalive);
    if (rc != MOSQ_ERR_SUCCESS && rc != MOSQ_ERR_ERRNO) {
        ERROR("MQTT: could not connect to the broker '%s:%d': %s", mqtt_host, mqtt_port, mosquitto_strerror(rc));
    }
    /* On MOSQ_ERR_ERRNO (the name does not resolve yet, the port is closed)
     * the thread keeps trying, and the wait below decides. */
    rc = mosquitto_loop_start(mqtt_client);
    if (rc != MOSQ_ERR_SUCCESS) {
        ERROR("MQTT: could not start the network thread: %s", mosquitto_strerror(rc));
    }

    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(mqtt_connect_timeout);
    while (!mqtt_connected) {
        int connack = mqtt_connack;
        if (connack > 0) {
            /* The broker answered and said no: waiting will not change it. */
            mqtt_shutdown();
            ERROR("MQTT: the broker '%s:%d' refused the connection: %s", mqtt_host, mqtt_port,
                  mosquitto_connack_string(connack));
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            int reason = mqtt_disconnect_reason;
            mqtt_shutdown();
            ERROR("MQTT: no connection to the broker '%s:%d' within %d seconds%s%s", mqtt_host, mqtt_port,
                  mqtt_connect_timeout, reason ? ": " : "", reason ? mosquitto_strerror(reason) : "");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    mqtt_was_connected = true;
}

bool mqtt_publish(const char *topic, const std::string &payload)
{
    if (!mqtt_client || !topic) {
        return false;
    }
    mqtt_report_connection();

    /* libmosquitto keeps a QoS 1 or 2 message until the broker has it,
     * without a limit of its own, and takes one while there is no
     * connection too (saying that it did not, or that it did, by its
     * version). Kept for a broker that is away, or slower than the run,
     * that is memory the run needs. So nothing is handed over while the
     * connection is away, and while -mqtt_max_queued messages wait for the
     * broker the new one is not either. */
    const char *refused = nullptr;
    if (!mqtt_connected) {
        refused = mosquitto_strerror(MOSQ_ERR_NO_CONN);
    } else if (mqtt_max_queued > 0 && mqtt_pending.count() >= (size_t)mqtt_max_queued) {
        refused = "too many messages wait for the broker (-mqtt_max_queued)";
    } else {
        /* What libmosquitto has taken is waited for by its id, whatever
         * it said: -mqtt_max_queued and the exit count what it holds. */
        int rc = mqtt_pending.publish(
            [&](int *mid) {
                return mosquitto_publish(mqtt_client, mid, topic, (int)payload.length(), payload.c_str(), mqtt_pub_qos,
                                         false);
            },
            [](int result) { return mqtt_publish_kept(result, mqtt_pub_qos); });
        if (mqtt_publish_kept(rc, mqtt_pub_qos)) {
            return true;
        }
        refused = mosquitto_strerror(rc);
    }

    /* Counted, and said at most every few seconds: a broker that is away
     * while a load test fails must not add a warning per message. */
    mqtt_failed_publishes++;
    unsigned long now = getmilliseconds();
    if (!mqtt_last_failure_report || now - mqtt_last_failure_report >= MQTT_FAILURE_REPORT_INTERVAL) {
        mqtt_last_failure_report = now ? now : 1;
        WARNING("MQTT: could not publish to '%s': %s (%lu messages not published so far)", topic, refused,
                mqtt_failed_publishes);
    }
    return false;
}

void mqtt_shutdown()
{
    if (!mqtt_client) {
        return;
    }

    /* What is queued gets its chance: until everything is delivered or
     * -mqtt_shutdelay is over. A connection that is gone just now does not
     * end the wait: the thread connects again and sends what is left. */
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(mqtt_shutdelay > 0 ? mqtt_shutdelay : 0);
    while (mqtt_pending.count() > 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    /* The thread sends the DISCONNECT and reports it; give it a moment for
     * that, then stop it wherever it is (it may be waiting to connect again). */
    if (mosquitto_disconnect(mqtt_client) == MOSQ_ERR_SUCCESS) {
        auto sent = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while (mqtt_connected && std::chrono::steady_clock::now() < sent) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    mosquitto_loop_stop(mqtt_client, true);
    mosquitto_destroy(mqtt_client);
    mqtt_client = nullptr;
    mosquitto_lib_cleanup();
    mqtt_connected = false;
    mqtt_was_connected = false;
}

#ifdef GTEST
#include "gtest/gtest.h"

static bool kept_qos1(int rc)
{
    return mqtt_publish_kept(rc, 1);
}

TEST(mqtt, PublishKept)
{
    EXPECT_TRUE(mqtt_publish_kept(MOSQ_ERR_SUCCESS, 0));
    EXPECT_TRUE(mqtt_publish_kept(MOSQ_ERR_SUCCESS, 1));
    /* without a connection a QoS 0 message is gone, a QoS 1 or 2 one is kept */
    EXPECT_FALSE(mqtt_publish_kept(MOSQ_ERR_NO_CONN, 0));
    EXPECT_TRUE(mqtt_publish_kept(MOSQ_ERR_NO_CONN, 1));
    EXPECT_TRUE(mqtt_publish_kept(MOSQ_ERR_NO_CONN, 2));
    EXPECT_FALSE(mqtt_publish_kept(MOSQ_ERR_PAYLOAD_SIZE, 1));
}

TEST(mqtt, Pending)
{
    MqttPending pending;
    auto send = [](int id, int rc) {
        return [=](int *mid) {
            *mid = id;
            return rc;
        };
    };

    EXPECT_EQ(MOSQ_ERR_SUCCESS, pending.publish(send(1, MOSQ_ERR_SUCCESS), kept_qos1));
    EXPECT_EQ(MOSQ_ERR_SUCCESS, pending.publish(send(2, MOSQ_ERR_SUCCESS), kept_qos1));
    EXPECT_EQ(2u, pending.count());
    /* what libmosquitto did not take is not waited for */
    EXPECT_EQ(MOSQ_ERR_PAYLOAD_SIZE, pending.publish(send(3, MOSQ_ERR_PAYLOAD_SIZE), kept_qos1));
    EXPECT_EQ(2u, pending.count());
    /* what it took while it said there is no connection is */
    EXPECT_EQ(MOSQ_ERR_NO_CONN, pending.publish(send(4, MOSQ_ERR_NO_CONN), kept_qos1));
    EXPECT_EQ(3u, pending.count());

    /* a delivery nobody waits for takes no other message's place */
    pending.delivered(3);
    pending.delivered(99);
    EXPECT_EQ(3u, pending.count());
    pending.delivered(4);
    pending.delivered(4);
    EXPECT_EQ(2u, pending.count());
    pending.delivered(1);
    pending.delivered(2);
    EXPECT_EQ(0u, pending.count());

    /* delivered from inside the call, before the id is known here */
    pending.publish(
        [&](int *mid) {
            *mid = 5;
            pending.delivered(5);
            return (int)MOSQ_ERR_SUCCESS;
        },
        kept_qos1);
    EXPECT_EQ(0u, pending.count());
    /* and that delivery is not held against the next message of the id */
    pending.publish(send(5, MOSQ_ERR_SUCCESS), kept_qos1);
    EXPECT_EQ(1u, pending.count());

    /* counted off, a late delivery of it is nothing */
    pending.reset();
    EXPECT_EQ(0u, pending.count());
    pending.delivered(5);
    pending.publish(send(6, MOSQ_ERR_SUCCESS), kept_qos1);
    EXPECT_EQ(1u, pending.count());
}
#endif /* GTEST */

#endif /* USE_MQTT */
