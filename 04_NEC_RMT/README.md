ESP-IDF NEC RMT
====================

Este repositorio contiene una colección de proyectos y experimentos desarrollados utilizando el **Espressif IoT Development Framework (ESP-IDF)**. El objetivo es documentar el aprendizaje de periféricos, protocolos y arquitectura de software para sistemas embebidos.
Una línea para desperta a GitHub

## 🛠️ Entorno de Desarrollo
- **IDE:** Espressif-IDE (basado en Eclipse).
- **Framework:** ESP-IDF v5.x (C/C++).
- **Hardware Principal:** ESP32-WROOM-32.
- **Herramienta de Carga:** `idf.py` / UART.

---

## 📂 Estructura de Proyectos

| Carpeta | Descripción | Periféricos / Protocolos |
| :--- | :--- | :--- |
| `01_NEC_RMT` | Decodificación de señales infrarrojas (IR) | RMT (Remote Control), GPIO |
| `02_NEC_RMT` | Incluye conexión  WiFi y comunicación MQTT |

---

## 📡 Detalles de Hardware
En estos proyectos se utilizan comúnmente los siguientes componentes:
* **Sensor de Infrarrojos:** Receptor IR (ej. TSOP38238) para protocolo NEC.
* **Comunicación:** MQTT mediante el broker HiveMQ Cloud.

---
