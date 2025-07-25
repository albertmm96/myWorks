cmake_minimum_required(VERSION 3.8)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
    "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project("AsebanSkyWatch")

# 📦 Ta cible existante (conserve le nom)
add_executable(AsebanSkyWatch "AsebanSkyWatch.cpp" "AsebanSkyWatch.h")

# ✅ Définis la version C++
if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET AsebanSkyWatch PROPERTY CXX_STANDARD 20)
endif()

# ➕ Indique où trouver Qt (remplace si besoin avec ton dossier)
set(CMAKE_PREFIX_PATH "D:/MyApps/Qt/6.9.1/msvc2022_64")

# 🧩 Recherche Qt Widgets
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# 🛠️ Active le MOC/UIC automatique si tu utilises .ui/rcc
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 🔗 Lie Qt à ton exécutable
target_link_libraries(AsebanSkyWatch PRIVATE Qt6::Widgets)
