# Mejoras propuestas para CCTV Scanner v0.6+

## 📊 Análisis actual

**Lo que ya funciona:**
- ✅ Escaneo de dispositivos CCTV (SADP, Dahua, genéricos)
- ✅ Prueba de enlace (ping, descarga, RSSI)
- ✅ Inventario local persistente
- ✅ Historial de descubrimientos
- ✅ Portal web configuración
- ✅ OTA updates
- ✅ Historial de puntos de survey

**Limitaciones detectadas:**
- ❌ No hay multi-dispositivo
- ❌ No hay búsqueda/filtrado avanzado
- ❌ No hay alertas de cambios
- ❌ No hay reportes automatizados
- ❌ No hay análisis de seguridad
- ❌ No hay exportación avanzada
- ❌ No hay API REST para terceros
- ❌ No hay autenticación

---

## 🎯 Mejoras por impacto + complejidad

### **IMPACTO ALTO / COMPLEJIDAD BAJA**

#### 1. **Búsqueda y filtrado avanzado en portal**
**Problema:** Usuario no puede filtrar 100 dispositivos encontrados
**Solución:**
- Filtro por fabricante
- Filtro por tipo (cámara, NVR, etc)
- Buscar por IP/MAC/nombre
- Ordenar por RSSI, fecha, tipo

**Código:** 50-100 líneas JavaScript
**Tiempo:** 2-3 horas
**Valor:** ⭐⭐⭐⭐ (Uso inmediato)

---

#### 2. **Exportación mejorada (JSON + Excel)**
**Problema:** Solo CSV actual
**Solución:**
- Exportar a JSON estructurado
- Exportar a Excel (.xlsx) con múltiples hojas
- Incluir gráficos en PDF
- Filtrar por fecha/dispositivo antes de exportar

**Código:** 100-150 líneas
**Tiempo:** 3-4 horas
**Valor:** ⭐⭐⭐⭐ (Documentación profesional)

---

#### 3. **Detección de cambios en red**
**Problema:** No sabe si un dispositivo desapareció
**Solución:**
- Comparar último escaneo vs anterior
- Marcar dispositivos: "Nuevo", "Desapareció", "Cambió IP"
- Indicador visual (colores: verde=OK, rojo=desapareció)
- Historial de cambios por dispositivo

**Código:** 80-120 líneas
**Tiempo:** 4-5 horas
**Valor:** ⭐⭐⭐⭐⭐ (Seguridad)

---

#### 4. **Alertas locales (sin cloud)**
**Problema:** No notifica de cambios críticos
**Solución:**
- Reglas: "Si desaparece un dispositivo conocido → alerta"
- Sonido/vibración en ESP32
- LED indicador
- Registro de eventos críticos
- Notificación en portal

**Código:** 150-200 líneas
**Tiempo:** 5-6 horas
**Valor:** ⭐⭐⭐⭐⭐ (Seguridad activa)

---

#### 5. **Dashboard visual mejorado**
**Problema:** Portal es funcional pero poco visual
**Solución:**
- Gráfico de estado de red (verde/amarillo/rojo)
- Mapa visual de dispositivos por tipo
- Timeline de cambios (línea temporal)
- Indicadores de salud (% dispositivos disponibles)
- Minimap de RSSI (heatmap)

**Código:** 200-300 líneas HTML/CSS/JS
**Tiempo:** 6-8 horas
**Valor:** ⭐⭐⭐⭐ (Usabilidad)

---

### **IMPACTO ALTO / COMPLEJIDAD MEDIA**

#### 6. **Análisis de seguridad básico**
**Problema:** No advierte sobre riesgos
**Solución:**
- ⚠️ Detección de dispositivos sin autenticación
- ⚠️ Advertencia si IP coincide con rangos conocidos de botnet
- ⚠️ Check de firmware desactualizado (si base de datos)
- 🔐 Reporte de "dispositivos de riesgo"
- 📊 Score de seguridad de la red CCTV

**Código:** 200-300 líneas
**Tiempo:** 8-10 horas
**Valor:** ⭐⭐⭐⭐⭐ (Diferenciador real)

---

#### 7. **Historial vs tiempo real**
**Problema:** No puede comparar estados anterior vs actual
**Solución:**
- "Ver snapshot de hace X horas"
- Comparar dos momentos en tiempo
- Gráfico: dispositivos encontrados por día/hora
- Detectar patrones (ej: un dispositivo que aparece/desaparece)

**Código:** 250-350 líneas
**Tiempo:** 8-10 horas
**Valor:** ⭐⭐⭐⭐ (Análisis)

---

#### 8. **Información extendida de dispositivos**
**Problema:** Datos limitados por protocolo
**Solución:**
- Intentar obtener info via HTTP GET /favicon.ico (CCTV típicamente lo sirve)
- Screenshot del dispositivo (si es web accessible)
- Intentar conexión HTTP/HTTPS para verificar puerto web real
- Banner grabbing (qué servidor responde)
- Geolocación aproximada de IP (si es pública)

**Código:** 300-400 líneas
**Tiempo:** 10-12 horas
**Valor:** ⭐⭐⭐⭐ (Información profesional)

---

### **IMPACTO MEDIO / COMPLEJIDAD BAJA**

#### 9. **Temas oscuro/claro y personalización**
**Problema:** Portal solo tiene un tema
**Solución:**
- Toggle dark/light mode
- Colores personalizables
- Guardar preferencias
- Modo "profesional" (sin animaciones)

**Código:** 100 líneas
**Tiempo:** 2 horas
**Valor:** ⭐⭐⭐ (UX)

---

#### 10. **Geolocalización de puntos de survey**
**Problema:** Puntos solo tienen RSSI, no ubicación
**Solución:**
- GPS opcional (módulo GPS externo)
- O: marcar manual "Salón", "Entrada", "Terraza"
- Mostrar puntos en mini-mapa
- Exportar mapa con puntos

**Código:** 100-150 líneas
**Tiempo:** 4-5 horas
**Valor:** ⭐⭐⭐⭐ (Análisis de cobertura)

---

#### 11. **Estadísticas globales**
**Problema:** No hay resumen de datos
**Solución:**
- Total dispositivos encontrados
- Fabricantes más comunes
- Canales Wi-Fi más saturados
- Promedio RSSI de la red
- Dispositivos sin MAC conocida (sospechosos)

**Código:** 80-120 líneas
**Tiempo:** 3-4 horas
**Valor:** ⭐⭐⭐ (Insight)

---

### **IMPACTO ALTO / COMPLEJIDAD ALTA**

#### 12. **Backend cloud simple (multi-dispositivo)**
**Problema:** No puede ver múltiples ESP32 en un dashboard
**Solución:**
- Servidor Node.js/Python simple
- ESP32 registra y envía datos periódicamente
- Dashboard web con múltiples dispositivos
- Histórico centralizado
- Alertas por email/webhook

**Código:** 500-800 líneas backend + 600-800 líneas frontend
**Tiempo:** 30-40 horas
**Valor:** ⭐⭐⭐⭐⭐ (Escala empresarial)

---

#### 13. **API REST pública**
**Problema:** No se puede integrar con otros sistemas
**Solución:**
- Documentación OpenAPI/Swagger
- Endpoints:
  - GET `/api/v1/devices` - lista de dispositivos
  - GET `/api/v1/devices/{id}` - detalles
  - POST `/api/v1/scan` - iniciar escaneo
  - GET `/api/v1/history` - historial
  - POST `/api/v1/alert-config` - configurar alertas
- Autenticación por token
- Rate limiting

**Código:** 200-300 líneas
**Tiempo:** 8-10 horas
**Valor:** ⭐⭐⭐⭐⭐ (Integración)

---

#### 14. **Detección automática de amenazas**
**Problema:** No advierte de intrusiones
**Solución:**
- Detección de "dispositivo desconocido en red CCTV"
- Alertas de MAC spoofing
- Detección de puertos anómalos
- Blacklist de MACs/IPs sospechosas
- Análisis de patrones de escaneo

**Código:** 400-500 líneas
**Tiempo:** 15-20 horas
**Valor:** ⭐⭐⭐⭐⭐ (Ciberseguridad)

---

## 🗓️ Roadmap recomendado

### **v0.6** (1-2 semanas)
- ✅ Búsqueda/filtrado avanzado (#1)
- ✅ Detección de cambios en red (#3)
- ✅ Dashboard visual mejorado (#5)
- ✅ Exportación mejorada (#2)

**Impacto:** 40-60 horas de desarrollo
**Valor percibido:** ⭐⭐⭐⭐⭐

---

### **v0.7** (2-3 semanas)
- ✅ Análisis de seguridad básico (#6)
- ✅ Alertas locales (#4)
- ✅ Información extendida de dispositivos (#8)
- ✅ Temas y personalización (#9)

**Impacto:** 40-50 horas
**Valor:** Diferenciador empresarial

---

### **v0.8** (3-4 semanas)
- ✅ Historial vs tiempo real (#7)
- ✅ Geolocalización de puntos (#10)
- ✅ Estadísticas globales (#11)
- ✅ API REST pública (#13)

**Impacto:** 50-60 horas
**Valor:** Listo para integradores

---

### **v1.0 PRO** (versión de pago)
- ✅ Backend cloud multi-dispositivo (#12)
- ✅ Detección automática de amenazas (#14)
- ✅ Soporte por email
- ✅ Garantía de actualizaciones

**Precio sugerido:** $99 única vez o $9.99/mes
**ROI:** Vale la pena tras v0.7

---

## 📈 Ranking por valor/esfuerzo

| # | Mejora | Valor | Esfuerzo | Prioridad |
|---|--------|-------|----------|-----------|
| 1 | Búsqueda avanzada | ⭐⭐⭐⭐ | 2h | 🔴 Alta |
| 3 | Detección cambios | ⭐⭐⭐⭐⭐ | 4h | 🔴 Alta |
| 2 | Exportación mejorada | ⭐⭐⭐⭐ | 3h | 🔴 Alta |
| 5 | Dashboard visual | ⭐⭐⭐⭐ | 7h | 🟠 Media |
| 4 | Alertas locales | ⭐⭐⭐⭐⭐ | 6h | 🟠 Media |
| 6 | Análisis seguridad | ⭐⭐⭐⭐⭐ | 10h | 🟠 Media |
| 9 | Temas/UI | ⭐⭐⭐ | 2h | 🟡 Baja |
| 11 | Estadísticas | ⭐⭐⭐ | 4h | 🟡 Baja |
| 8 | Info extendida | ⭐⭐⭐⭐ | 11h | 🟡 Baja |
| 7 | Historial tiempo | ⭐⭐⭐⭐ | 9h | 🟡 Baja |
| 10 | Geolocalización | ⭐⭐⭐⭐ | 5h | 🟡 Baja |
| 13 | API REST | ⭐⭐⭐⭐⭐ | 9h | 🟠 Media |
| 12 | Backend cloud | ⭐⭐⭐⭐⭐ | 35h | 🔵 Futura |
| 14 | Detección amenazas | ⭐⭐⭐⭐⭐ | 18h | 🔵 Futura |

---

## 💡 Mi recomendación

**Semana 1-2: Haz v0.6 (impacto inmediato)**
1. Búsqueda/filtrado (#1) - 2h
2. Detección cambios (#3) - 4h  
3. Exportación mejorada (#2) - 3h
4. Dashboard visual (#5) - 7h

**Total:** ~15 horas = 3-4 días de trabajo

**Resultado:** Una versión claramente mejorada que justifica llamarse "v0.6"

¿Cuál de estas te interesa más? ¿O prefieres un mix?
