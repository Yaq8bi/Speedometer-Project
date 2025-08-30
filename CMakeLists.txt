cmake_minimum_required(VERSION 3.22)

project(av24tr LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(  ) #-Werror -Wall -Wextra -Wpedantic

set(PROJECT_FONT_PATH ${PROJECT_SOURCE_DIR}/desktop/client/res/MaterialIcons.ttf)
set(LOCAL_FONT_DIR $ENV{HOME}/.local/share/fonts)
set(ICONS_PATH ${LOCAL_FONT_DIR}/MaterialIcons.ttf)

set(CLIENT_SOURCES_PATH ${PROJECT_SOURCE_DIR}/desktop/client/src/)
set(SERVER_SOURCES_PATH ${PROJECT_SOURCE_DIR}/desktop/server/src/)

set(CLIENT_HEADERS_PATH ${PROJECT_SOURCE_DIR}/desktop/client/include/)
set(SERVER_HEADERS_PATH ${PROJECT_SOURCE_DIR}/desktop/server/include/)

if(NOT EXISTS ${ICONS_PATH})
file(MAKE_DIRECTORY "${LOCAL_FONT_DIR}")
file(COPY "${PROJECT_FONT_PATH}" DESTINATION "${LOCAL_FONT_DIR}")
execute_process(COMMAND fc-cache -f -v ${LOCAL_FONT_DIR})
endif()

if(NOT EXISTS ${PROJECT_SOURCE_DIR}/build/sound.wav)
file(COPY "${PROJECT_SOURCE_DIR}/desktop/client/res/sound.wav" DESTINATION "${PROJECT_SOURCE_DIR}/build")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Multimedia)
set(CLIENT_LINK_LIBRARIES Qt6::Core Qt6::Widgets Qt6::Multimedia)
set(SERVER_LINK_LIBRARIES Qt6::Core Qt6::Widgets)


set(CLIENT_MAIN_PATH ${PROJECT_SOURCE_DIR}/desktop/client/main.cpp)
set(SERVER_MAIN_PATH ${PROJECT_SOURCE_DIR}/desktop/server/main.cpp)

set(CLIENT_SOURCES)
list(APPEND CLIENT_SOURCES ${CLIENT_SOURCES_PATH}canvas.cpp)
list(APPEND CLIENT_SOURCES ${CLIENT_SOURCES_PATH}comservice.cpp)
list(APPEND CLIENT_SOURCES ${CLIENT_SOURCES_PATH}window.cpp)

set(SERVER_SOURCES)
list(APPEND SERVER_SOURCES ${SERVER_SOURCES_PATH}comservice.cpp)
list(APPEND SERVER_SOURCES ${SERVER_SOURCES_PATH}window.cpp)

set(CLIENT_HEADERS)
list(APPEND CLIENT_HEADERS ${CLIENT_HEADERS_PATH}canvas.h)
list(APPEND CLIENT_HEADERS ${CLIENT_HEADERS_PATH}comservice.h)
list(APPEND CLIENT_HEADERS ${CLIENT_HEADERS_PATH}window.h)

set(SERVER_HEADERS)
list(APPEND SERVER_HEADERS ${SERVER_HEADERS_PATH}window.h)
list(APPEND SERVER_HEADERS ${SERVER_HEADERS_PATH}comservice.h)


# Define a switch-like string variable
set(COMM_PROTOCOL "TCP" CACHE STRING "Choose communication protocol: UART or TCP") #TCP is temporarily set by default. Will be set to null when both communication protocols are implemented.

# Validate and set macro accordingly
if (COMM_PROTOCOL STREQUAL "UART")
    message(STATUS "Selected communication protocol: UART")
    add_compile_definitions(PRIVATE COMM_PROTOCOL_UART)

    list(APPEND CLIENT_SOURCES ${CLIENT_SOURCES_PATH}uartservice.cpp)
    list(APPEND SERVER_SOURCES ${SERVER_SOURCES_PATH}uartservice.cpp)
    
    list(APPEND CLIENT_HEADERS ${CLIENT_HEADERS_PATH}uartservice.h)
    list(APPEND SERVER_HEADERS ${SERVER_HEADERS_PATH}uartservice.h)
    
    find_package(Qt6 REQUIRED COMPONENTS SerialPort)
    list(APPEND CLIENT_LINK_LIBRARIES Qt6::SerialPort)
    list(APPEND SERVER_LINK_LIBRARIES Qt6::SerialPort)
    
    add_custom_target(upload_client
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/esp32/client
                pio run --target upload --upload-port "/dev/ttyUSB1"
        COMMENT "Uploading client firmware..."
    )
    
    add_custom_target(upload_server
        COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/esp32/server
                pio run --target upload --upload-port "/dev/ttyUSB0"
        COMMENT "Uploading server firmware..."
    )

    
elseif (COMM_PROTOCOL STREQUAL "TCP")
    message(STATUS "Selected communication protocol: TCP")
    add_compile_definitions(server PRIVATE COMM_PROTOCOL_TCP)
    
    list(APPEND CLIENT_SOURCES ${CLIENT_SOURCES_PATH}tcpservice.cpp)
    list(APPEND SERVER_SOURCES ${SERVER_SOURCES_PATH}tcpservice.cpp)
    
    list(APPEND CLIENT_HEADERS ${CLIENT_HEADERS_PATH}tcpservice.h)
    list(APPEND SERVER_HEADERS ${SERVER_HEADERS_PATH}tcpservice.h)

else()
    message(FATAL_ERROR "Invalid COMM_PROTOCOL specified. Choose UART or TCP via: \n\"cmake .. -DCOMM_PROTOCOL=option\".")
endif()
    
add_executable(server ${SERVER_MAIN_PATH} ${SERVER_HEADERS} ${SERVER_SOURCES})
target_link_libraries(server PUBLIC ${SERVER_LINK_LIBRARIES})

add_executable(client ${CLIENT_MAIN_PATH} ${CLIENT_HEADERS} ${CLIENT_SOURCES})
target_link_libraries(client PUBLIC ${CLIENT_LINK_LIBRARIES})

target_include_directories(client PRIVATE ${PROJECT_SOURCE_DIR}/shared ${CLIENT_HEADERS_PATH})
target_include_directories(server PRIVATE ${PROJECT_SOURCE_DIR}/shared ${SERVER_HEADERS_PATH})

if (COMM_PROTOCOL STREQUAL "UART")
    add_dependencies(client upload_client)
    add_dependencies(server upload_server)
endif()