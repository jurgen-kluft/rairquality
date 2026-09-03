#include "rairquality/c_airquality.h"

#include "rcore/c_app.h"
#include "rcore/c_gpio.h"
#include "rcore/c_timer.h"
#include "rcore/c_log.h"
#include "rcore/c_str.h"
#include "rcore/c_system.h"
#include "rcore/c_wire.h"
#include "ccore/c_random.h"

#include "rwifi/c_wifi_mgr.h"
#include "rwifi/c_tcp_client.h"

#include "rhome/c_msg.h"
#include "rhome/c_sensor.h"

#include "lib_bh1750/c_bh1750.h"
#include "lib_bme280/c_bme280.h"
#include "lib_scd41/c_scd41.h"
#include "lib_rd03d/c_rd03d.h"

#include "lib_wcs/c_lcd.h"

#define ENABLE_BH1750
#define ENABLE_BME280
#define ENABLE_SCD41
#define ENABLE_RD03D

namespace ncore
{
    struct bme280_data_t
    {
        i32 temperature;
        i32 pressure;
        i32 humidity;

        void reset()
        {
            temperature = 0;
            pressure    = 0;
            humidity    = 0;
        }
    };

    struct bh1750_data_t
    {
        i32 lux;

        void reset() { lux = 0; }
    };

    struct scd41_data_t
    {
        i32 co2;
        i32 temperature;
        i32 humidity;

        void reset()
        {
            co2         = 0;
            temperature = 0;
            humidity    = 0;
        }
    };

    struct rd03d_data_t
    {
        u32 DetectionBits[3];
        u8  Detected[3];
        u8  LastSendDetected[3];

        void reset()
        {
            for (s8 i = 0; i < 3; ++i)
            {
                DetectionBits[i]    = 0;
                Detected[i]         = 4;  // Unknown state
                LastSendDetected[i] = 3;
            }
        }
    };

    struct state_app_t
    {
        nnet::msg_t gSensorPacket;  // Sensor packet for sending data

        nnet::wifi_manager_t gWifiManager;  // WiFi manager for network connectivity
        nnet::wifi_config_t  gWifiConfig;   // WiFi configuration
        nnet::tcp_client_t   gTcpClient;    // TCP client for server communication
        nnet::config_t       gTcpConfig;    // TCP client configuration

        xor_random_t gRandom;  // XORShift random number generator for any randomization needs

        bme280_data_t gCurrentBme;
        bme280_data_t gLastSendBme;

        bh1750_data_t gCurrentBh;
        bh1750_data_t gLastSendBh;

        scd41_data_t gCurrentScd;
        scd41_data_t gLastSendScd;

        rd03d_data_t gCurrentRd03d;
    };
    state_app_t gAppState;

    void read_bh1750(state_t* state)
    {
#ifdef ENABLE_BH1750
        // TODO whenever a sensor cannot be read (faulty?) we need to know so that we can
        //      send a 'state' packet that indicates the sensor is not working.

        // Read the BH1750 sensor data
        u16        lux          = 0;
        const bool valid_bh1750 = nsensors::updateBH1750(lux);
        if (valid_bh1750)
        {
            gAppState.gCurrentBh.lux = lux;
        }
#endif
    }

    void send_bh1750(state_t* state)
    {
#ifdef ENABLE_BH1750
        const i32 lux = gAppState.gCurrentBh.lux;
        if (gAppState.gLastSendBh.lux != lux)
        {
            gAppState.gLastSendBh.lux = lux;

            nlog::printf("Light: %d lx\n", va_t(lux));

            nnet::msg_init(gAppState.gSensorPacket, nnet::MSG_TYPE_SENSOR_DATA, state->MACAddress);
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_LIGHT, (u16)lux);

            // Send the sensor data to the server
            nnet::send(gAppState.gTcpClient, gAppState.gSensorPacket.Data, gAppState.gSensorPacket.Size);
        }
#endif
    }

    void read_bme280(state_t* state)
    {
#ifdef ENABLE_BME280
        // Read the BME280 sensor data
        f32        bme_temp     = 0.0f;
        f32        bme_pres     = 0.0f;
        f32        bme_humi     = 0.0f;
        const bool valid_bme280 = nsensors::updateBME280(bme_pres, bme_temp, bme_humi);
        if (valid_bme280)
        {
            gAppState.gCurrentBme.temperature = static_cast<s8>(bme_temp);   // Temperature to one signed byte (°C)
            gAppState.gCurrentBme.pressure    = static_cast<u16>(bme_pres);  // Pressure to unsigned short (hPa)
            gAppState.gCurrentBme.humidity    = static_cast<u8>(bme_humi);   // Humidity to one unsigned byte (%)
        }
#endif
    }

    void send_bme280(state_t* state)
    {
#ifdef ENABLE_BME280
        // Write a custom (binary-format) network message
        nnet::msg_init(gAppState.gSensorPacket, nnet::MSG_TYPE_SENSOR_DATA, state->MACAddress);

        const s8  temperature = gAppState.gCurrentBme.temperature;
        const u16 pressure    = gAppState.gCurrentBme.pressure;
        const u8  humidity    = gAppState.gCurrentBme.humidity;
        if (gAppState.gLastSendBme.temperature != temperature)
        {
            gAppState.gLastSendBme.temperature = temperature;
            nlog::printf("Temperature: %d °C\n", va_t((s32)temperature));
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_TEMPERATURE, (u16)temperature);
        }
        if (gAppState.gLastSendBme.pressure != pressure)
        {
            gAppState.gLastSendBme.pressure = pressure;
            nlog::printf("Pressure: %d hPa\n", va_t((u32)pressure));
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_PRESSURE, (u16)pressure);
        }
        if (gAppState.gLastSendBme.humidity != humidity)
        {
            gAppState.gLastSendBme.humidity = humidity;
            nlog::printf("Humidity: %d %%\n", va_t((u32)humidity));
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_HUMIDITY, (u16)humidity);
        }

        // Send the sensor data to the server
        if (gAppState.gSensorPacket.Size > 0)
        {
            nnet::send(gAppState.gTcpClient, gAppState.gSensorPacket.Data, gAppState.gSensorPacket.Size);
        }

#endif
    }

    void read_scd41(state_t* state)
    {
#ifdef ENABLE_SCD41
        // Read the SCD41 sensor data
        f32        scd_humi    = 0.0f;  // Initialize humidity value for SCD41
        f32        scd_temp    = 0.0f;  // Initialize temperature value for SCD41
        u16        scd_co2     = 0;     // Initialize CO2 value
        const bool valid_scd41 = nsensors::updateSCD41(scd_humi, scd_temp, scd_co2);

        if (valid_scd41)
        {
            gAppState.gCurrentScd.co2         = scd_co2;
            gAppState.gCurrentScd.temperature = static_cast<s8>(scd_temp);
            gAppState.gCurrentScd.humidity    = static_cast<u8>(scd_humi);
        }
#endif
    }

    void send_scd41(state_t* state)
    {
#ifdef ENABLE_SCD41
        // Write a custom (binary-format) network message
        nnet::msg_init(gAppState.gSensorPacket, nnet::MSG_TYPE_SENSOR_DATA, state->MACAddress);

        const i32 co2         = gAppState.gCurrentScd.co2;
        const i32 temperature = gAppState.gCurrentScd.temperature;
        const i32 humidity    = gAppState.gCurrentScd.humidity;

        if (gAppState.gLastSendScd.co2 != co2)
        {
            gAppState.gLastSendScd.co2 = co2;
            nlog::printf("SCD CO2: %d ppm\n", va_t(co2));
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_CO2, co2);
        }
        if (gAppState.gLastSendScd.temperature != temperature)
        {
            gAppState.gLastSendScd.temperature = temperature;
            nlog::printf("SCD Temperature: %d °C\n", va_t(temperature));
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_TEMPERATURE, temperature);
        }
        if (gAppState.gLastSendScd.humidity != humidity)
        {
            gAppState.gLastSendScd.humidity = humidity;
            nlog::printf("SCD Humidity: %d %%\n", va_t(humidity));
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_HUMIDITY, humidity);
        }

        if (gAppState.gSensorPacket.Size > 0)
        {
            nnet::send(gAppState.gTcpClient, gAppState.gSensorPacket.Data, gAppState.gSensorPacket.Size);
        }
#endif
    }

    void read_rd03d(state_t* state)
    {
#ifdef ENABLE_RD03D
        if (nsensors::updateRD03D())
        {
            for (s8 i = 0; i < 3; ++i)
            {
                nsensors::nrd03d::target_t tgt;
                if (nsensors::getRD03DTarget(i, tgt))
                {
                    gAppState.gCurrentRd03d.DetectionBits[i] = (gAppState.gCurrentRd03d.DetectionBits[i] << 1) | 1;
                    // nlog::printf("T%d: %d, %d\n", va_t(i), va_t(tgt[i].x), va_t(tgt[i].y));
                }
                else
                {
                    gAppState.gCurrentRd03d.DetectionBits[i] = (gAppState.gCurrentRd03d.DetectionBits[i] << 1) | 0;
                }

                u8 detected = gAppState.gCurrentRd03d.Detected[i] & 3;  // Current detection state

                const bool dseen = (gAppState.gCurrentRd03d.DetectionBits[i] & 0x3F) == 0x3F;
                if (dseen)
                {
                    // Too transition from no-presence to presence we must have seen 3 detections in a row (300 ms)
                    detected = ((detected << 1) | 1);
                }
                else
                {
                    const bool dnone = (gAppState.gCurrentRd03d.DetectionBits[i] & 0x3FFFFFFF) == 0;
                    if (dnone)
                    {
                        // To transition from presence to no-presence we must have seen 30 no-detections in a row (3 seconds)
                        detected = ((detected << 1) | 0);
                    }
                }
                gAppState.gCurrentRd03d.Detected[i] = detected;

                nlog::printf("T%d detection: %s\n", va_t(i), va_t((detected != 0) ? "PRESENCE" : "ABSENCE"));
            }
        }
#endif
    }

    void send_rd03d(state_t* state)
    {
#ifdef ENABLE_RD03D
        // Write a custom (binary-format) network message
        nnet::msg_init(gAppState.gSensorPacket, nnet::MSG_TYPE_SENSOR_DATA, state->MACAddress);

        for (s8 i = 0; i < 3; ++i)
        {
            const u8 detected = gAppState.gCurrentRd03d.Detected[i];
            if (gAppState.gCurrentRd03d.LastSendDetected[i] != detected)
            {
                gAppState.gCurrentRd03d.LastSendDetected[i] = detected;
                msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_PRESENCE1 + i, detected);
            }
        }

        if (gAppState.gSensorPacket.Size > 0)
        {
            nnet::send(gAppState.gTcpClient, gAppState.gSensorPacket.Data, gAppState.gSensorPacket.Size);
        }
#endif
    }

}  // namespace ncore

namespace ncore
{
    namespace napp
    {
#define SDA_PIN 21
#define SCL_PIN 22

#define RD03D_RX_PIN 16
#define RD03D_TX_PIN 17

        void wakeup(state_t* state, ncore::nwakeup::reason_t reason)
        {
            // ...
        }

        void presetup(state_t* state)
        {
            // ...
        }

        void setup(state_t* state)
        {
            nnet::init_wifi_config(gAppState.gWifiConfig, WIFI_SSID(), WIFI_PASSWORD());
            nnet::setup(gAppState.gWifiManager, &gAppState.gWifiConfig);
            nnet::activate(gAppState.gWifiManager);

            void* socket = nnet::setup_default(&gAppState.gTcpConfig);
            nnet::setup(gAppState.gTcpClient, &gAppState.gTcpConfig, socket, SENSOR_SERVER_IP(), SENSOR_SERVER_TCPPORT());
            nnet::connect(gAppState.gTcpClient);

            nsensors::initBME280();
            nsensors::initBH1750();
            nsensors::initSCD41();
            nsensors::initRD03D(RD03D_RX_PIN, RD03D_TX_PIN);
        }

        static u64 toggle_lcd_fill_time = 0;

        void tick(state_t* state)
        {
            nlog::println("Tick");

            // Network tick handles WiFi state machine and TCP client state machine, ensuring TCP only runs when WiFi is connected
            nnet::tick_tcp_client(&gAppState.gWifiManager, gAppState.gTcpClient);

            if (ntimer::millis() - toggle_lcd_fill_time > 2000)
            {
                // Fill a random area with a random color every 2 seconds
                u16 color = (u16)g_random_u32(&gAppState.gRandom, 16);  // Random RGB565 color
                u16 sx    = (u16)g_random_u32_range(&gAppState.gRandom, 0, nlcd::width() - 51);
                u16 sy    = (u16)g_random_u32_range(&gAppState.gRandom, 0, nlcd::height() - 51);
                u16 ex    = sx + (g_random_u32_range(&gAppState.gRandom, 0, 40) + 10);  // Random width between 10 and 50 pixels
                u16 ey    = sy + (g_random_u32_range(&gAppState.gRandom, 0, 40) + 10);  // Random height between 10 and 50 pixels

                nlcd::draw_rectangle(sx, sy, ex, ey, color);
                toggle_lcd_fill_time = ntimer::millis();
            }
        }

    }  // namespace napp
}  // namespace ncore