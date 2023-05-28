cmake_minimum_required(VERSION 3.5)

set(COMPONENT_SRCDIRS
        src/mqtt
        src/wifi
        src/sntp
        )

set(COMPONENT_ADD_INCLUDEDIRS
        src/
        )

set(COMPONENT_REQUIRES
        freertos
        nvs_flash
        wifi_provisioning
        mbedtls
        qrcode
        coreJSON
        coreMQTT
        coreMQTT-Agent
        backoffAlgorithm
        )

idf_component_register(
        SRC_DIRS ${COMPONENT_SRCDIRS}
        REQUIRES ${COMPONENT_REQUIRES}
        INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)