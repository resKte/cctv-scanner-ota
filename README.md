# CCTV Scanner - ESP32-S3

**Herramienta profesional de escaneo, descubrimiento y auditoría de redes CCTV**

[![GitHub Release](https://img.shields.io/github/v/release/resKte/cctv-scanner-ota)](https://github.com/resKte/cctv-scanner-ota/releases)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Platform: ESP32-S3](https://img.shields.io/badge/Platform-ESP32--S3-brightgreen)](https://www.espressif.com/)

## 🎯 Descripción

CCTV Scanner es un firmware para ESP32-S3 que implementa un portal web de descubrimiento y mapeo de dispositivos de vigilancia en redes locales. Incluye múltiples protocolos de detección (SADP, Dahua DHDiscover, ONVIF, SSDP, mDNS) y proporciona un inventario persistente con historial de cambios.

## ✨ Características principales

### 🔍 Descubrimiento de dispositivos
- **SADP** (Hikvision/EZVIZ) - Multicast discovery
- **Dahua DHDiscover** - Protocolo Dahua nativo
- **ONVIF WS-Discovery** - Estándar de vigilancia IP
- **SSDP/UPnP** - Dispositivos compatibles
- **mDNS/DNS-SD** - Servicios locales

### 📊 Portal web
- Configuración Wi-Fi sin contraseña
- Inventario CCTV con MAC, modelo, firmware, tipo
- Historial de descubrimientos (200+ registros)
- Prueba de enlace (ping, RSSI, descarga)
- Análisis de puntos de survey
- Tabla ARP del gateway
- Exportación CSV

### 🔄 Actualizaciones OTA
- Descarga automática desde GitHub Releases
- Validación SHA-256
- Rollback automático si falla arranque
- Pantalla de progreso en LCD
- Overlay en web portal

### 📱 Pantalla LCD
- Pantalla ST7789 172x320
- Información en tiempo real
- Estado de Wi-Fi, almacenamiento, versión
- Progreso OTA con barra
- QR para acceso rápido

### 💾 Almacenamiento
- LittleFS (8 MB) para inventario
- MicroSD (hasta 2TB) para historial
- Persistencia de configuración
- Logging de errores

## 🛠️ Hardware requerido

| Componente | Especificación |
|-----------|-----------------|
| Microcontrolador | ESP32-S3 |
| Flash | 16 MB mínimo |
| PSRAM | 8 MB |
| Pantalla | LCD ST7789 (172x320) |
| Almacenamiento | MicroSD (opcional) |
| Alimentación | 5V USB-C |

### Pines utilizados
- **LCD**: GPIO 39-46, 40, 41, 42, 45
- **SD Card**: GPIO 11, 12, 13
- **BOOT**: GPIO 0

## 📦 Instalación

### Opción 1: Firmware precompilado (recomendado)

1. Descarga el último firmware:
   ```bash
   wget https://github.com/resKte/cctv-scanner-ota/releases/download/v0.5.4/firmware.bin
   ```

2. Instala esptool:
   ```bash
   pip install esptool
   ```

3. Carga en tu ESP32-S3:
   ```bash
   esptool.py --chip esp32s3 -p /dev/ttyUSB0 write_flash 0x0 firmware.bin
   ```

   **En Windows:**
   ```cmd
   esptool.py --chip esp32s3 -p COM3 write_flash 0x0 firmware.bin
   ```

### Opción 2: Compilar desde código fuente

1. Instala [PlatformIO](https://platformio.org/):
   ```bash
   pip install platformio
   ```

2. Clona el repositorio:
   ```bash
   git clone https://github.com/resKte/cctv-scanner-ota.git
   cd cctv-scanner-ota
   ```

3. Compila y carga:
   ```bash
   platformio run -t upload
   ```

## 🚀 Uso rápido

### Primer arranque

1. **Activa el punto de acceso:**
   - Presiona el botón BOOT durante 1 segundo
   - El LCD mostrará un código QR

2. **Conecta al portal:**
   - Escanea el QR o conecta a `ESP32-SiteSurvey` (contraseña: `12345678`)
   - Abre: **http://192.168.4.1**

3. **Configura Wi-Fi:**
   - Selecciona tu red
   - Ingresa contraseña
   - Espera conexión

### Portal principal

Una vez conectado a tu red:
- **Resumen:** Estado del dispositivo, versión, almacenamiento
- **Dispositivos LAN:** Escanea CCTV en tu red
- **Inventario CCTV:** Registra equipos encontrados
- **Puntos de Survey:** Mapea señal Wi-Fi
- **Actualizaciones OTA:** Configura descarga automática

## 🔄 Actualizaciones OTA

### Configuración

1. En el portal: **Resumen > Actualizaciones OTA desde GitHub**
2. Ingresa la URL del manifiesto:
   ```
   https://raw.githubusercontent.com/resKte/cctv-scanner-ota/main/ota/stable.json
   ```
3. Presiona **Guardar y comprobar**
4. Mantén BOOT presionado 1 segundo durante 30 segundos para cambiar la URL

### Descarga automática

- Verifica cada 6 horas
- Reintentos cada 30 segundos si falla
- Descarga en partición inactiva
- Valida SHA-256 antes de activar
- Rollback automático si no arranca

## 📋 API REST

El dispositivo expone endpoints JSON para integración:

```bash
# Estado general
curl http://192.168.4.1/api/status

# Estado OTA
curl http://192.168.4.1/api/ota/status

# Historial descubrimientos
curl http://192.168.4.1/api/discovery-history

# Vecinos ARP
curl http://192.168.4.1/api/arp-neighbors

# Puntos de survey
curl http://192.168.4.1/api/points
```

Ver documentación completa en [OTA.md](OTA.md)

## 📊 Versiones

| Versión | Fecha | Estado | Cambios |
|---------|-------|--------|---------|
| **v0.5.4** | 2026-11-13 | ✅ Actual | LCD muestra pantalla negra y progreso OTA |
| v0.5.3 | 2026-11-13 | ✅ Estable | Interfaz OTA en web y LCD |
| v0.5.2 | 2026-11-13 | ✅ Estable | Mejoras Wi-Fi, escaneo manual |
| v0.5.1 | 2026-11-13 | ✅ Estable | OTA verificado y funcional |
| v0.5.0 | 2026-11-13 | ✅ Estable | Portal cautivo, QR, inventario |

Ver [CHANGELOG.md](CHANGELOG.md) completo.

## 🐛 Reportar problemas

¿Encontraste un bug? [Abre un issue](https://github.com/resKte/cctv-scanner-ota/issues)

Incluye:
- Modelo de ESP32
- Versión del firmware
- Pasos para reproducir
- Logs (desde Serial o error-log)

## 🤝 Contribuciones

Las contribuciones son bienvenidas. Para cambios mayores:

1. Abre un [Issue](https://github.com/resKte/cctv-scanner-ota/issues) primero
2. Haz fork del repositorio
3. Crea una rama: `git checkout -b feature/tu-mejora`
4. Commit: `git commit -m "Describe tu cambio"`
5. Push: `git push origin feature/tu-mejora`
6. Abre Pull Request

Ver [CONTRIBUTING.md](CONTRIBUTING.md)

## ⚖️ Licencia

Este proyecto está bajo licencia **GPL v3**. Ver [LICENSE](LICENSE) para detalles.

**Resumen:** Eres libre de usar, modificar y distribuir este software, siempre que las modificaciones también sean GPL v3.

## 🔐 Seguridad

Para reportar vulnerabilidades **sin hacerlas públicas** primero:
1. **NO** abras un issue público
2. Contacta a través de [Security Advisories](https://github.com/resKte/cctv-scanner-ota/security/advisories)

Ver [SECURITY.md](SECURITY.md)

## 🙏 Agradecimientos

- [Arduino_GFX_Library](https://github.com/moononournation/Arduino_GFX_Library)
- [ESP-IDF](https://docs.espressif.com/projects/esp-idf/)
- Comunidad ESP32

## 📧 Contacto

- **Issues:** [GitHub Issues](https://github.com/resKte/cctv-scanner-ota/issues)
- **Discussions:** [GitHub Discussions](https://github.com/resKte/cctv-scanner-ota/discussions)

---

**Versión:** v0.5.4  
**Actualizado:** 2026-11-13  
**Estado:** ✅ Producción
