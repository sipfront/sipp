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
#include <deque>
#include <mutex>
#include <string>
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
static std::atomic<long> mqtt_undelivered(0);

/* Control messages on their way from libmosquitto's thread to the main loop.
 * A sender that floods the topic gets its oldest messages dropped, not SIPp's
 * memory. */
static std::mutex mqtt_control_mutex;
static std::deque<std::string> mqtt_control_queue;
static std::atomic<bool> mqtt_control_pending(false);
static std::atomic<unsigned long> mqtt_control_dropped(0);
static const size_t MQTT_CONTROL_QUEUE_MAX = 256;
/* The result of the last subscribe, a MOSQ_ERR_* value. */
static std::atomic<int> mqtt_subscribe_result(0);

/* Main thread only. */
static bool mqtt_was_connected = false;
static unsigned long mqtt_failed_publishes = 0;
static unsigned long mqtt_last_failure_report = 0;

/* How often, at most, a failing publish is reported (ms). */
static const unsigned long MQTT_FAILURE_REPORT_INTERVAL = 5000;

static void on_connect(struct mosquitto *client, void *, int rc)
{
    mqtt_connack = rc;
    if (rc == 0 && mqtt_ctrl) {
        /* On every connect: the session is a clean one, the broker does
         * not remember the subscription over a reconnect. */
        mqtt_subscribe_result = mosquitto_subscribe(client, nullptr, mqtt_ctrl_topic, mqtt_sub_qos);
    }
    mqtt_connected = (rc == 0);
}

static void on_message(struct mosquitto *, void *, const struct mosquitto_message *message)
{
    if (!message->payload || message->payloadlen <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(mqtt_control_mutex);
    if (mqtt_control_queue.size() >= MQTT_CONTROL_QUEUE_MAX) {
        mqtt_control_queue.pop_front();
        mqtt_control_dropped++;
    }
    mqtt_control_queue.emplace_back((const char *)message->payload, (size_t)message->payloadlen);
    mqtt_control_pending = true;
}

static void on_disconnect(struct mosquitto *, void *, int rc)
{
    mqtt_disconnect_reason = rc;
    mqtt_connected = false;
}

static void on_publish(struct mosquitto *, void *, int)
{
    mqtt_undelivered--;
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
        WARNING("MQTT: lost the connection to the broker '%s:%d' (%s), trying to connect again",
                mqtt_host, mqtt_port, mosquitto_strerror(mqtt_disconnect_reason));
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
    if (mqtt_keepalive < 5) {
        /* libmosquitto refuses less. */
        ERROR("-mqtt_keepalive must be at least 5 seconds, not %d", mqtt_keepalive);
    }
    if (mqtt_sub_qos < 0 || mqtt_sub_qos > 2) {
        ERROR("-mqtt_sub_qos must be 0, 1 or 2, not %d", mqtt_sub_qos);
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
    mosquitto_message_callback_set(mqtt_client, on_message);
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
            ERROR("MQTT: the broker '%s:%d' refused the connection: %s",
                  mqtt_host, mqtt_port, mosquitto_connack_string(connack));
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            int reason = mqtt_disconnect_reason;
            mqtt_shutdown();
            ERROR("MQTT: no connection to the broker '%s:%d' within %d seconds%s%s",
                  mqtt_host, mqtt_port, mqtt_connect_timeout,
                  reason ? ": " : "", reason ? mosquitto_strerror(reason) : "");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    mqtt_was_connected = true;
    if (mqtt_ctrl && mqtt_subscribe_result != MOSQ_ERR_SUCCESS) {
        int result = mqtt_subscribe_result;
        mqtt_shutdown();
        ERROR("MQTT: could not subscribe to the control topic '%s': %s", mqtt_ctrl_topic, mosquitto_strerror(result));
    }
}

bool mqtt_publish(const char *topic, const std::string &payload)
{
    if (!mqtt_client || !topic) {
        return false;
    }
    mqtt_report_connection();

    int rc = mosquitto_publish(mqtt_client, nullptr, topic, (int)payload.length(),
                               payload.c_str(), mqtt_pub_qos, false);
    if (rc == MOSQ_ERR_SUCCESS) {
        mqtt_undelivered++;
        return true;
    }

    /* Counted, and said at most every few seconds: a broker that is away
     * while a load test fails must not add a warning per message. */
    mqtt_failed_publishes++;
    unsigned long now = getmilliseconds();
    if (!mqtt_last_failure_report || now - mqtt_last_failure_report >= MQTT_FAILURE_REPORT_INTERVAL) {
        mqtt_last_failure_report = now ? now : 1;
        WARNING("MQTT: could not publish to '%s': %s (%lu messages not published so far)",
                topic, mosquitto_strerror(rc), mqtt_failed_publishes);
    }
    return false;
}

void mqtt_poll()
{
    if (!mqtt_control_pending) {
        return;
    }
    std::deque<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(mqtt_control_mutex);
        messages.swap(mqtt_control_queue);
        mqtt_control_pending = false;
    }
    unsigned long dropped = mqtt_control_dropped.exchange(0);
    if (dropped) {
        WARNING("MQTT: %lu control messages dropped, they came faster than they could be run", dropped);
    }
    for (const std::string &message : messages) {
        process_control_message(message.data(), message.length());
    }
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
    while (mqtt_undelivered > 0 && std::chrono::steady_clock::now() < deadline) {
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

#endif /* USE_MQTT */
