.. _variant_sidewalk_location:

Location demo
#############

This sample variant demonstrates how to report the device location to the Sidewalk cloud without any user interaction.
The application starts Sidewalk automatically and, once the device is registered and time synced, it periodically runs a location scan and sends the result to the cloud.

The variant has no shell and no button actions.
Everything it does is driven by the Sidewalk status callback and a periodic timer.

Source file setup
*****************

The application consists of the following source file:

* :file:`src/location/app.c` -- The application file that registers Sidewalk callbacks, initializes the location library, and requests a periodic location uplink.

The application performs the following steps:

1. Starts the Sidewalk thread, and sends the ``sidewalk_event_platform_init`` and ``sidewalk_event_autostart`` events.
   The Sidewalk stack is initialized with the Bluetooth LE and LoRa link mask, and starts advertising for registration.
#. Waits in the ``on_status_changed`` callback until the Sidewalk state is ready, the device is registered, and the time is synced.
#. Initializes the Sidewalk location library with ``sid_location_init()``.
#. Starts a periodic timer, and calls ``sid_location_run()`` with the ``SID_LOCATION_SCAN_AND_SEND`` run type on every timer expiration.
#. Reports the result of each location run in the ``on_update`` callback of the location library.

A location run may take longer than the configured interval, for example when the scan results are fragmented over LoRa.
In that case, the application skips the cycle and logs a warning instead of starting a second run in parallel.

Requirements
************

The variant requires a radio that provides the Wi-Fi scanning capability for location level 3.
Use one of the following setups:

* The `nRF54L15 DK`_ or the `nRF54LM20 DK`_ with the :ref:`nRF Sidewalk EB <nrf_sidewalk_eb>` shield.
* The `nRF54L15 DK`_ with the ``semtech_lr1110mb1xxs`` shield.

For details on the supported location methods and hardware, see :ref:`location_services`.

.. note::
   With the Semtech SX1262 shield, only the Bluetooth LE network location (level 1) is available.
   In that case, set ``CONFIG_SID_END_DEVICE_LOCATION_EFFORT_L1=y``.

Configuration options
*********************

The Location demo application variant uses the ``OVERLAY_CONFIG="overlay-location.conf"`` configuration.
The sample variant supports the following Kconfig options:

.. include:: ../../includes/include_kconfig_common.txt

* ``CONFIG_SID_END_DEVICE_LOCATION_INTERVAL_S`` -- The period, in seconds, between consecutive location scan and uplink attempts.
  It is set to ``60`` by default.

* ``CONFIG_SID_END_DEVICE_LOCATION_EFFORT`` -- The effort mode requested for every location run.

   * ``CONFIG_SID_END_DEVICE_LOCATION_EFFORT_L1`` -- Sidewalk network location over Bluetooth LE.

   * ``CONFIG_SID_END_DEVICE_LOCATION_EFFORT_L3`` -- Wi-Fi scan.
     This is the default option.

   * ``CONFIG_SID_END_DEVICE_LOCATION_EFFORT_L4`` -- GNSS scan.

* ``CONFIG_SID_END_DEVICE_LOCATION_MANAGE_EFFORT`` -- Allows the location library to step down to a lower effort mode when the requested one is unavailable or fails.
  It is enabled by default.

Building and running
********************

.. include:: ../../includes/include_building_and_running.txt

To build the Location demo variant for the `nRF54L15 DK`_ with the :ref:`nRF Sidewalk EB <nrf_sidewalk_eb>` shield, run the following command in the project directory:

.. code-block:: console

   $ west build -b nrf54l15dk/nrf54l15/cpuapp --shield nrf_sidewalk_eb -- -DOVERLAY_CONFIG="overlay-location.conf"

Testing
=======

Before you start, make sure that positioning is activated for your device in the AWS IoT Core console, and that a destination is configured.
For more details, see :ref:`location_services_troubleshooting`.

After successfully building the sample and flashing the manufacturing data, complete the following steps:

#. Connect to the device UART to see the logs.

#. Register the device.
   The application starts Sidewalk automatically, so the device advertises over Bluetooth LE right after the boot.
   Register it with your Sidewalk gateway as described in :ref:`setting_up_sidewalk_prototype`.

   When the device is registered and time synced, you should see the following logs:

   .. code-block:: console

      <inf> app: Device Is registered, Time Sync Success, Link status: {BLE: Up, FSK: Down, LoRa: Down}
      <inf> app: Location initialized, uplink every 60 s in effort mode 3

#. Wait for the location uplinks.
   The first location run starts immediately after the location library is initialized, and is repeated every minute:

   .. code-block:: console

      <inf> app: Location scan and send started in effort mode 3
      <inf> app: Location update status 4, err 0 (SID_ERROR_NONE), effort mode 3, link type 3
      <inf> app: Location payload
                 01 02 03 04 05 06 07 08 |........

   .. note::
      A Wi-Fi or a GNSS scan needs more time than a Bluetooth LE uplink.
      Depending on the signal conditions, it may take up to a few minutes to deliver the location data.

#. Verify the resolved position in the AWS IoT Core console, in the device details of your Sidewalk device.
