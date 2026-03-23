cmake_minimum_required(VERSION 3.5)
project(proxy)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find the Paho MQTT C++ package
find_package(PahoMqttCpp REQUIRED)

# Add your source files
set(SOURCE_FILES
  config.cpp
  main.cpp
  proxy.cpp 
    )

# Create an executable
add_executable(proxy ${SOURCE_FILES})

target_compile_options(proxy PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wshadow
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
)

target_link_options(proxy PRIVATE
  -Wl,-z,relro
  -Wl,-z,now
)

# Link against the Paho MQTT C++ library
target_link_libraries(proxy PahoMqttCpp::paho-mqttpp3)

