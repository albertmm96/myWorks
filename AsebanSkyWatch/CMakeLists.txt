cmake_minimum_required(VERSION 3.8)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("AsebanSkyWatch")

# activer le MOC/UIC automatique si on utilise .ui/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# la cible existante (on conserve le nom)
add_executable(AsebanSkyWatch
    src/main/main.cpp
    src/main/main.h
    src/backend/fetch/openSkyFetcher.h
    src/backend/fetch/openSkyFetcher.cpp
    src/gui/views/mainwindow.cpp
    src/gui/views/mainwindow.h
    src/gui/views/mainwindow.ui
)

# définir la version C++
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET AsebanSkyWatch PROPERTY CXX_STANDARD 20)
endif()

# indiquer où trouver le dossier Qt
set(CMAKE_PREFIX_PATH "D:/MyApps/Qt/6.9.1/msvc2022_64")

# chercher les packages Qt nécessaires au projet
find_package(Qt6 COMPONENTS REQUIRED
    Widgets Gui Core
    Sql Network
    WebChannel Positioning WebEngineCore WebEngineWidgets)


# lier Qt à son exécutable
target_link_libraries(AsebanSkyWatch PRIVATE 
    Qt6::Widgets Qt6::Gui Qt6::Core
    Qt6::Sql Qt6::Network
    Qt6::WebChannel Qt6::Positioning Qt6::WebEngineCore Qt6::WebEngineWidgets)

target_include_directories(AsebanSkyWatch PRIVATE
    ${CMAKE_SOURCE_DIR}/src/gui/views
    ${CMAKE_SOURCE_DIR}/src/backend/fetch
)