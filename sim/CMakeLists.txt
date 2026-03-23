cmake_minimum_required(VERSION 3.5)
project(sim)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


# Find the Paho MQTT C++ package
find_package(PahoMqttCpp REQUIRED)

# Add your source files
set(SOURCE_FILES
  main.cpp
  sim.cpp
  filehandling.cpp 
    )

# Create an executable
add_executable(sim ${SOURCE_FILES})

target_compile_options(sim PRIVATE
  -Wall
  -Wextra
  -Wpedantic
  -Wconversion
  -Wshadow
  -fstack-protector-strong
  -D_FORTIFY_SOURCE=2
)

target_link_options(sim PRIVATE
  -Wl,-z,relro
  -Wl,-z,now
)

# Link against the Paho MQTT C++ library
target_link_libraries(sim PahoMqttCpp::paho-mqttpp3)
