# Aseban SkyWatch

Aseban SkyWatch is a C++20 desktop application for real-time aircraft and weather visualization.

The application retrieves live aircraft state vectors from the OpenSky Network, combines them with OpenWeather data, and displays the results through an interactive web map embedded in a Qt interface.

The main interest of this backend project lies in its lightweight state management for data ingestion, compared with a conventional persistent relational architecture. By making the UI the primary source of truth, the backend remains largely stateless and leverages TimescaleDB for scalable time-series storage.

## Features

* Live aircraft state acquisition from OpenSky
* Weather data acquisition from OpenWeather
* Interactive aircraft visualization on a web-based map
* Geographic area and map-tile calculations
* Native C++ and JavaScript communication through Qt WebChannel
* Asynchronous HTTP requests using Qt Network
* Aircraft and weather data models
* Modular fetcher, service, controller, and view components

## Technology Stack

* [C++20](https://isocpp.org/)
* [Qt 6](https://doc.qt.io/qt-6/get-and-install-qt.html): Widgets, Network, SQL, WebChannel, Positioning and WebEngine
* [CMake 3.21+](https://cmake.org/download/)
* [PostgreSQL](https://www.postgresql.org/download/) with the [TimescaleDB extension](https://docs.timescale.com/self-hosted/latest/install/)
* [Qt QPSQL driver](https://doc.qt.io/qt-6/sql-driver.html)
* [OpenLayers](https://openlayers.org/doc/) map frontend
* [OpenSky REST API](https://openskynetwork.github.io/opensky-api/rest.html)
* [OpenWeather API](https://openweathermap.org/api)
* HTML, CSS and JavaScript

## Architecture

```text
OpenSky API ───────► OpenSky Fetcher ───────► Live Flights Service
                                                       │
OpenWeather API ───► OpenWeather Fetcher ───► Live Weather Service
                                                       │
                                                       ▼
                                              Qt Application Layer
                                                       │
                                                       ▼
                                               WebChannel Bridge
                                                       │
                                                       ▼
                                             Embedded Web Map
```

## Project Structure

```text
AsebanSkyWatch/
├── config/
├── src/
│   ├── backend/
│   │   ├── compute/
│   │   ├── fetch/
│   │   ├── models/
│   │   └── services/
│   ├── gui/
│   │   ├── controllers/
│   │   └── views/
│   ├── main/
│   └── utils/
├── CMakeLists.txt
└── README.md
```

### Backend

* `fetch/` contains the OpenSky and OpenWeather HTTP clients.
* `services/` manages live aircraft and weather updates.
* `models/` contains aircraft and weather data structures.
* `compute/` contains experimental computation modules.

### GUI

* `views/` contains the Qt desktop interface.
* `controllers/` manages communication between Qt and the embedded map.
* Qt WebChannel connects the C++ backend to the JavaScript frontend.

### Utilities

* Geographic coordinate processing
* Map-tile calculations
* Bounding-area calculations

## Requirements

Before building, install:

* A C++20 compiler. On Windows, use the [MSVC Desktop development with C++ workload](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation).
* [Qt 6](https://doc.qt.io/qt-6/qt-online-installation.html) with `Core`, `Gui`, `Widgets`, `Network`, `Sql`, `WebChannel`, `Positioning`, `WebEngineCore` and `WebEngineWidgets`.
* [CMake 3.21+](https://cmake.org/download/).
* [PostgreSQL](https://www.postgresql.org/download/) with [TimescaleDB](https://docs.timescale.com/self-hosted/latest/install/) enabled.
* The Qt [QPSQL PostgreSQL driver](https://doc.qt.io/qt-6/sql-driver.html).
* OpenSky OAuth client credentials and an OpenWeather API key.

CMake configures and builds the application; it does not install these dependencies.

## Build

After installing and configuring the dependencies:

```bash
cmake -S . -B build
cmake --build build --config Release
```

The project can also be opened through `CMakeLists.txt` in Qt Creator.

## Configuration

The application expects:

* `config/openSkyCredentials.json`
* `config/openWeatherCredentials.json`
* A local PostgreSQL instance with TimescaleDB enabled
* Database connection settings matching the local PostgreSQL installation

Credential files are excluded from version control.

## Experimental TPI Module

The repository contains an experimental TPI computation prototype based on rolling windows and robust statistical calculations.

This module is not functional, is not integrated into the active application workflow, and is no longer under development.

## Current Status

Implemented:

* OpenSky and OpenWeather data acquisition
* Qt desktop interface and embedded OpenLayers map
* Native-to-web communication through Qt WebChannel
* PostgreSQL storage with a TimescaleDB hypertable
* Live aircraft and weather services
* Geographic processing utilities

Not currently implemented:

* Persistent historical trajectory workflow
* Automated test suite
* Release packaging
* Cloud deployment
* Production monitoring


## Screenshots

### Main GUI & backend beta v1.2

![Main Window](docs/screen1.jpg)

### TimescaleDB underlying hypertable example

![flights DB](docs/screen2.png)

### Initial Context Diagram

[View the architecture context diagram (PDF)](docs/initialContextDiagram.pdf)


## License

This project is licensed under the MIT License. See the [LICENSE](https://github.com/albertmm96/myWorks/blob/main/AsebanSkyWatch/LICENSE) for details.
