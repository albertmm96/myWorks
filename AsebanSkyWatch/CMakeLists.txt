cmake_minimum_required(VERSION 3.21)

if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

project("AsebanSkyWatch")

# activate the automatic handling of Qt's MOC, UIC, and RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# the existing source files
add_executable(AsebanSkyWatch
    src/main/main.cpp
    src/main/main.h
    src/backend/fetch/openSkyFetcher.h
    src/backend/fetch/openSkyFetcher.cpp
    src/gui/views/mainwindow.cpp
    src/gui/views/mainwindow.h
    src/gui/views/mainwindow.ui
 "src/gui/controllers/bridge.h" "src/gui/controllers/bridge.cpp" "src/utils/tile_math.h" "src/utils/tile_math.cpp" "src/backend/services/LiveFlightsService.h" "src/backend/services/LiveFlightsService.cpp"  "src/backend/fetch/openWeatherFetcher.h" "src/backend/fetch/openWeatherFetcher.cpp" "src/backend/services/LiveWeatherService.h" "src/backend/services/LiveWeatherService.cpp" "src/backend/models/aircraft_state.h" "src/backend/models/weather_grid.h" "src/backend/compute/tpi/rolling_window.h" "src/backend/compute/tpi/robust_stats.h" "src/backend/compute/tpi/tpi_engine.h" "src/backend/compute/tpi/tpi_engine.cpp")

add_custom_command(TARGET AsebanSkyWatch POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E echo "Copying credentials json to $<TARGET_FILE_DIR:AsebanSkyWatch>"

  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/config/openSkyCredentials.json"
    "$<TARGET_FILE_DIR:AsebanSkyWatch>/openSkyCredentials.json"

  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/config/openWeatherCredentials.json"
    "$<TARGET_FILE_DIR:AsebanSkyWatch>/openWeatherCredentials.json"
)


# define the C++ standard if supported by the CMake version
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET AsebanSkyWatch PROPERTY CXX_STANDARD 20)
endif()

# show where to find Qt installation
# set(CMAKE_PREFIX_PATH "D:/MyApps/Qt/6.9.1/msvc2022_64")

# we search for the Qt6 packages we need
find_package(Qt6 COMPONENTS REQUIRED
    Widgets Gui Core
    Sql Network
    WebChannel Positioning WebEngineCore WebEngineWidgets)


# link the Qt6 libraries to the executable
target_link_libraries(AsebanSkyWatch PRIVATE 
    Qt6::Widgets Qt6::Gui Qt6::Core
    Qt6::Sql Qt6::Network
    Qt6::WebChannel Qt6::Positioning Qt6::WebEngineCore Qt6::WebEngineWidgets)

target_include_directories(AsebanSkyWatch PRIVATE
    ${CMAKE_SOURCE_DIR}/src/gui/views
    ${CMAKE_SOURCE_DIR}/src/gui/controllers
    ${CMAKE_SOURCE_DIR}/src/backend/fetch
    ${CMAKE_SOURCE_DIR}/src/backend/services
    ${CMAKE_SOURCE_DIR}/src/backend/models
    ${CMAKE_SOURCE_DIR}/src/backend/compute/tpi
    ${CMAKE_SOURCE_DIR}/src/utils
)