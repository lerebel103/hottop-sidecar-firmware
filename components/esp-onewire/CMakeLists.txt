set(COMPONENT_SRCDIRS
        src/
        )

set(COMPONENT_ADD_INCLUDEDIRS
        src/
        )

set(COMPONENT_REQUIRES
        freertos
        driver
        )

idf_component_register(
        SRC_DIRS ${COMPONENT_SRCDIRS}
        REQUIRES ${COMPONENT_REQUIRES}
        INCLUDE_DIRS ${COMPONENT_ADD_INCLUDEDIRS}
)
