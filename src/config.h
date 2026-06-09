/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define NO_SERVER_ADDR "0.0.0.0"
#define DEFAULT_SERVER_ADDR CONFIG_NET_SAMPLE_MQTT_BROKER_HOSTNAME

#ifdef CONFIG_NET_CONFIG_SETTINGS
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR CONFIG_NET_CONFIG_MY_IPV6_ADDR
#define SERVER_ADDR CONFIG_NET_CONFIG_PEER_IPV6_ADDR
#else
#define ZEPHYR_ADDR CONFIG_NET_CONFIG_MY_IPV4_ADDR
#define SERVER_ADDR CONFIG_NET_CONFIG_PEER_IPV4_ADDR
#endif
#else
#ifdef CONFIG_NET_IPV6
#define ZEPHYR_ADDR "2001:db8::1"
#define SERVER_ADDR "2001:db8::2"
#else
#define ZEPHYR_ADDR "192.168.1.101"
#define SERVER_ADDR "0.0.0.0"
// #define SERVER_ADDR		"192.168.1.10"
#endif
#endif

#if defined(CONFIG_SOCKS)
#define SOCKS5_PROXY_ADDR SERVER_ADDR
#define SOCKS5_PROXY_PORT 1080
#endif

#ifdef CONFIG_MQTT_LIB_TLS
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
#define SERVER_PORT 9001
#else
#define SERVER_PORT 8883
#endif /* CONFIG_MQTT_LIB_WEBSOCKET */
#else
#ifdef CONFIG_MQTT_LIB_WEBSOCKET
#define SERVER_PORT 9001
#else
#define SERVER_PORT 1883
#endif /* CONFIG_MQTT_LIB_WEBSOCKET */
#endif

#define APP_CONNECT_TIMEOUT_MS 2000
#define APP_SLEEP_MSECS 500

#define APP_CONNECT_TRIES 10

#define APP_MQTT_BUFFER_SIZE 128

#define APP_MQTT_KEEPALIVE 4

// MQTT definitions
#define PRODUCT_ID "SA"     // User defined
#define SERIAL_NUMBER "pu1" // It will be extracted from EFUSE_BLK3 (USER_DATA)

#define MQTT_COMPANY "technaid_sl"
#define MQTT_PRODUCT "techstim"
#define MQTT_PUBLISH_DEVICE_DESCRIPTOR MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/device_descriptor"
#define MQTT_PUBLISH_PARAMETERS MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/parameters"
#define MQTT_PUBLISH_INFO MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/info"
#define MQTT_SUBSCRIBE_IDS MQTT_COMPANY "/" MQTT_PRODUCT "/" SERIAL_NUMBER "/ids"

// #define TARGETS_ARRAY "[" SERIAL_NUMBER "," SERIAL_NUMBER "]"
#define TARGETS_ARRAY "[" SERIAL_NUMBER "," SERIAL_NUMBER "]"

/* descriptor en un único literal de bytes (Little‑Endian) */
#define DEVICE_DESCRIPTOR_BYTES                               \
    "\x26"                     /* Length = 38 */              \
    "\x00\x04"                 /* DescriptorVersion = 1024 */ \
    "\x00"                     /* DeviceClass */              \
    "\x01"                     /* DeviceSubClass */           \
    "\x01"                     /* DeviceID */                 \
    "\x02"                     /* DeviceProtocol */           \
    "\x0D"                     /* MaxPacketSize */            \
    "\x39\x30"                 /* Vendor = 12345 */           \
    "\x0D"                     /* Product */                  \
    "\x00\x04"                 /* BCDDevice = 1024 */         \
    "\x7B\x00\x00\x00"         /* SerialNumber = 123 */       \
    "\x10"                     /* IDs */                      \
    "\x00\x10"                 /* HardwareVersion = 4096 */   \
    "\x00\x04"                 /* FirmwareVersion = 1024 */   \
    "\x0D"                     /* ProtocolsSupported */       \
    "\x68\x01\xA8\xC0"         /* WIP = 192.168.1.104 */      \
    "\x00\x00\x00\x00"         /* CANID */                    \
    "\xB7\x3A\x11\x44\x1B\x00" /* MAC = 00:1B:44:11:3A:B7 */  \
    "\xB8\x0B"                 /* Port = 3000 */              \
    "\x00\xC2\x01\x00"         /* ByteSpeed = 115200 */

/* longitud en bytes */
#define DEVICE_DESCRIPTOR_LEN sizeof(DEVICE_DESCRIPTOR_BYTES) - 1

#define DEVICE_DESCRIPTOR_MACRO "{\n \
\"Length\": 38,\n \
\"DescriptorVersion\": 1024,\n \
\"DeviceClass\": 0,\n \
\"DeviceSubClass\": 1,\n \
\"DeviceID\": 1,\n \
\"DeviceProtocol\": 2,\n \
\"MaxPacketSize\": 13,\n \
\"Vendor\": 12345,\n \
\"Product\": 13,\n \
\"BCDDevice\": 1024,\n \
\"SerialNumber\": 123,\n \
\"IDs\": 16,\n \
\"HardwareVersion\": 4096,\n \
\"FirmwareVersion\": 1024,\n \
\"ProtocolsSupported\": 13,\n \
\"WIP\": 3232235880,\n \
\"CANID\": 0,\n \
\"MAC\": 117106096823,\n \
\"Port\": 3000,\n \
\"ByteSpeed\": 115200,\n \
}"

/* descriptor genérico de un dispositivo Techstim */
typedef struct
{
    uint8_t Length;             /* 38 bytes totales */
    uint16_t DescriptorVersion; /* 1024  (v0.4.0) */
    uint8_t DeviceClass;        /* 0 → Techstim */
    uint8_t DeviceSubClass;     /* 1 → Techstim v2 */
    uint16_t DeviceID;          /* 1 → SA */
    uint8_t DeviceProtocol;     /* 2 → Wi‑Fi */
    uint8_t MaxPacketSize;      /* 13 */
    uint16_t Vendor;            /* 12345  Technaid SL */
    uint8_t Product;            /* 13  Techstim Wi‑Fi register ID */
    uint16_t BCDDevice;         /* 1024  release */
    uint32_t SerialNumber;      /* 123 */
    uint8_t IDs;                /* 16 */
    uint16_t HardwareVersion;   /* 4096  (v0.4.0) */
    uint16_t FirmwareVersion;   /* 1024  (v1.0.0) */
    uint8_t ProtocolsSupported; /* 13 → USB,BT,Wi‑Fi */
    uint32_t WIP;               /* IP en formato host (0xC0A80168=192.168.1.104) */
    uint32_t CANID;             /* 0 → ninguno */
    uint64_t MAC;               /* 0x001B44113AB7 = 00:1B:44:11:3A:B7 */
    uint16_t Port;              /* 3000 */
    uint32_t ByteSpeed;         /* 115200 */
} device_descriptor_t;


/* Estructuras para parámetros de configuración */
typedef struct
{
    int16_t pos_amp[4];
    size_t pos_amp_len;
} pos_amp_t;

typedef struct
{
    int16_t neg_amp[4];
    size_t neg_amp_len;
} neg_amp_t;

typedef struct
{
    int16_t pos_time[4];
    size_t pos_time_len;
} pos_time_t;

typedef struct
{
    int16_t neg_time[4];
    size_t neg_time_len;
} neg_time_t;

typedef struct
{
    int16_t motor_amp[4];
    size_t motor_amp_len;
} umbral_motor_t;

typedef struct
{
    int16_t comfort_amp[4];
    size_t comfort_amp_len;
} umbral_comfort_t;

typedef struct
{
    int16_t max_amp[4];
    size_t max_amp_len;
} max_current_t;

typedef struct
{
    int16_t min_amp[4];
    size_t min_amp_len;
} min_current_t;

/**
 * Parameter index map for product configuration.
 * These values were previously defined as individual PARAM_* macros.
 */
typedef struct
{
    pos_amp_t pos_amp; // Positive amplitude for channels 1-4 [0-100 mA]
    neg_amp_t neg_amp; // Negative amplitude for channels 1-4 [0-100 mA]
    pos_time_t pos_time; // Positive pulse time for channels 1-4 [0-1000 µs]
    neg_time_t neg_time; // Negative pulse time for channels 1-4 [0-1000 µs]

    uint8_t order_channels; // Order of channels [0-3]
    uint8_t repetitions;    // Number of repetitions [single: 0, double: 1, triple: 2]
    uint8_t inv_pulse;      // Invert pulse [0-1]

    umbral_motor_t umbral_motor;   // Motor threshold for channels 1-4 [0-100 mA]
    umbral_comfort_t umbral_comfort; // Comfort threshold for channels 1-4 [0-100 mA]

    min_current_t min_current; // Minimum current for channels 1-4 [0-100 mA]
    max_current_t max_current; // Maximum current for channels 1-4 [0-100 mA]

    uint16_t intra_freq; // Intra-group frequency [0-200 Hz]
    uint16_t group_freq; // Group frequency [0-200 Hz]

    uint16_t upgrade_ramp;   // Upgrade ramp time [0-1000 ms]
    uint16_t downgrade_ramp; // Downgrade ramp time [0-1000 ms]

    uint16_t stimulation_mode; // Stimulation mode [Symmetric: 0, Asymmetric: 1]
    uint16_t delay;            // Delay between positive and negative pulse [100-1000 ms]

    uint16_t device_id;        // Device ID [PRODUCT_ID]
    uint16_t firmware_version; // Firmware version [4096 for v1.0.0, SOFTWARE_VERSION_SEMVER]
    uint16_t error;            // Error code [0 for no error, otherwise specific error codes]
    uint16_t status;           // Status code bitmask ([0/1]: stimulation inactive/active; bit 0: channel 1, bit 1: channel 2, etc.)
    uint16_t enabled;          // Enabled channels bitmask (bit 0 for channel 1, bit 1 for channel 2, etc.)

    uint16_t log_counter; // Log counter for tracking parameter changes (incremented on each change)
    uint16_t qualifier;   // Qualifier Card ID index for the product (similar to PRODUCT_ID)
} parameters_t;

/*******************************************************************************
 *  FIRMWARE VERSION AND CARD ID                                               *
 ******************************************************************************/
#ifdef APPVERSION
#define SOFTWARE_VERSION APP_VERSION_STRING // String format for SW version
// #define SOFTWARE_VERSION_SEMVER ((APP_VERSION_NUMBER) >> 4U)                                                      /* MAJOR.MINOR.PATCH [0xM.mm.P] */
#define SOFTWARE_VERSION_SEMVER ((APP_VERSION_MAJOR << 12U) + (APP_VERSION_MINOR << 4U) + (APP_PATCHLEVEL << 0U)) /* MAJOR.MINOR.PATCH [0xM.mm.P] */
#else
#define SOFTWARE_VERSION "v0.20.1"      // String format for SW version
#define SOFTWARE_VERSION_SEMVER 0x0141U /* MAJOR.MINOR.PATCH [0xM.mm.P] */
#endif

#define HARDWARE_VERSION_SEMVER 0x0010U   /* MAJOR.MINOR.PATCH [0xM.mm.P] */
#define RELEASE_VERSION_SEMVER 0x0010U    /* MAJOR.MINOR.PATCH [0xM.mm.P] */
#define DESCRIPTOR_VERSION_SEMVER 0x1000U /* MAJOR.MINOR.PATCH [0xM.mm.P] */

#define KIND_ID 83    // 'S' in ASCII DEC (0x53) for Tech'S'tim := Stimulation
#define DEFAULT_ID 65 // 'A' in ASCII DEC (0x41) for first device A = 0
// #define PRODUCT_ID (((KIND_ID) << 8) | (DEFAULT_ID))

#define MAX_PARAMS 68 // [A-Z][AA-AZ][BA-BP]

// Info status values
#define INFO_STATUS_CONNECTED "connected"
#define INFO_STATUS_DISCONNECTED "disconnected"

// Info mode values
#define INFO_MODE_CONTROLLER "controller"
#define INFO_MODE_PERIPHERAL "peripheral"

#define MQTT_CLIENTID PRODUCT_ID

#endif
