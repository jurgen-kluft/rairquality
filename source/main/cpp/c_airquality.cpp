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

#include "lib_guition/c_lcd.h"

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
        u8  LastSendPresence;

        void reset()
        {
            for (s8 i = 0; i < 3; ++i)
            {
                DetectionBits[i] = 0;
                Detected[i]      = 0;
            }
            LastSendPresence = 0xff;
        }
    };

    struct state_app_t
    {
        nnet::msg_t gSensorPacket;  // Sensor packet for sending data

        nnet::wifi_manager_t* gWifiManager;  // WiFi manager for network connectivity
        nnet::tcp_client_t    gTcpClient;    // TCP client for server communication
        nnet::config_t        gTcpConfig;    // TCP client configuration

        xor_random_t gRandom;  // XORShift random number generator for any randomization needs

        bme280_data_t gCurrentBme;
        bh1750_data_t gCurrentBh;
        scd41_data_t  gCurrentScd;
        rd03d_data_t  gCurrentRd03d;

        ntimer::periodic_task_t gReadBme280Task;
        ntimer::periodic_task_t gReadBh1750Task;
        ntimer::periodic_task_t gReadScd41Task;
        ntimer::periodic_task_t gSendEnvironmentTask;
        ntimer::periodic_task_t gProcessRd03dTask;

        void reset()
        {
            gCurrentBme.reset();
            gCurrentBh.reset();
            gCurrentScd.reset();
            gCurrentRd03d.reset();
        }
    };
    state_app_t gAppState;

    void read_bh1750(void* user, u64 now_ms)
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

    void read_bme280(void* user, u64 now_ms)
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

    void read_scd41(void* user, u64 now_ms)
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

    void send_environment(void*, u64)
    {
        nnet::msg_init(gAppState.gSensorPacket, nnet::MSG_TYPE_SENSOR_DATA, nnet::get_mac_address(gAppState.gWifiManager));
        msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_TEMPERATURE, gAppState.gCurrentBme.temperature);
        msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_HUMIDITY, gAppState.gCurrentBme.humidity);
        msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_PRESSURE, gAppState.gCurrentBme.pressure);
        msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_CO2, gAppState.gCurrentScd.co2);
        msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_LIGHT, gAppState.gCurrentBh.lux);
        nnet::send(gAppState.gTcpClient, gAppState.gSensorPacket.Data, gAppState.gSensorPacket.Size);
    }

    void process_rd03d(void*, u64)
    {
#ifdef ENABLE_RD03D
        if (!nsensors::updateRD03D())
        {
            return;
        }

        for (s8 i = 0; i < 3; ++i)
        {
            nsensors::nrd03d::target_t target;
            const bool                 target_detected = nsensors::getRD03DTarget(i, target);
            gAppState.gCurrentRd03d.DetectionBits[i]   = (gAppState.gCurrentRd03d.DetectionBits[i] << 1) | (target_detected ? 1 : 0);

            const bool detected_for_six_samples  = (gAppState.gCurrentRd03d.DetectionBits[i] & 0x3f) == 0x3f;
            const bool absent_for_thirty_samples = (gAppState.gCurrentRd03d.DetectionBits[i] & 0x3fffffff) == 0;
            if (detected_for_six_samples)
            {
                gAppState.gCurrentRd03d.Detected[i] = 1;
            }
            else if (absent_for_thirty_samples)
            {
                gAppState.gCurrentRd03d.Detected[i] = 0;
            }
        }

        u8 presence = 0;
        for (s8 i = 0; i < 3; ++i)
        {
            presence |= gAppState.gCurrentRd03d.Detected[i] << i;
        }

        if (presence != gAppState.gCurrentRd03d.LastSendPresence)
        {
            nnet::msg_init(gAppState.gSensorPacket, nnet::MSG_TYPE_SENSOR_DATA, nnet::get_mac_address(gAppState.gWifiManager));
            msg_write_sensor(gAppState.gSensorPacket, SENSOR_ID_BINARY, presence);
            if (nnet::send(gAppState.gTcpClient, gAppState.gSensorPacket.Data, gAppState.gSensorPacket.Size))
            {
                gAppState.gCurrentRd03d.LastSendPresence = presence;
            }
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
            gAppState.reset();

            gAppState.gWifiManager = nnet::create_wifi_mgr(WIFI_SSID(), WIFI_PASSWORD());
            nnet::activate(gAppState.gWifiManager);

            void* socket = nnet::setup_default(&gAppState.gTcpConfig);
            nnet::setup(gAppState.gTcpClient, &gAppState.gTcpConfig, socket);
            nnet::connect(gAppState.gTcpClient, SENSOR_SERVER_IP(), SENSOR_SERVER_TCPPORT());

            nsensors::initBME280();
            nsensors::initBH1750();
            nsensors::initSCD41();
            nsensors::initRD03D(RD03D_RX_PIN, RD03D_TX_PIN);

            ntimer::init_periodic_task(&gAppState.gReadBme280Task, 1000, nullptr, read_bme280);
            ntimer::init_periodic_task(&gAppState.gReadBh1750Task, 1000, nullptr, read_bh1750);
            ntimer::init_periodic_task(&gAppState.gReadScd41Task, 1000, nullptr, read_scd41);
            ntimer::init_periodic_task(&gAppState.gSendEnvironmentTask, 10000, nullptr, send_environment);
            ntimer::init_periodic_task(&gAppState.gProcessRd03dTask, 100, nullptr, process_rd03d);
            ntimer::mark_periodic_task(&gAppState.gSendEnvironmentTask, ntimer::millis());
        }

        static u64 toggle_lcd_fill_time = 0;

        void tick(state_t* state)
        {
            nlog::println("Tick");

            const u64 now_ms = ntimer::millis();
            ntimer::tick_periodic_task(&gAppState.gReadBme280Task, now_ms);
            ntimer::tick_periodic_task(&gAppState.gReadBh1750Task, now_ms);
            ntimer::tick_periodic_task(&gAppState.gReadScd41Task, now_ms);
            ntimer::tick_periodic_task(&gAppState.gSendEnvironmentTask, now_ms);
            ntimer::tick_periodic_task(&gAppState.gProcessRd03dTask, now_ms);

            nnet::tick(gAppState.gWifiManager);
            nnet::tick_tcp_client(nnet::get_link_state(gAppState.gWifiManager), gAppState.gTcpClient);

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