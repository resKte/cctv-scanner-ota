# Registro de cambios

## v0.5.4 - 2026-11-13

- OTA: la LCD muestra la pantalla negra y el progreso desde que comienza la preparación de una actualización confirmada, incluyendo la conexión y validación inicial del firmware. Si la preparación falla, vuelve a la pantalla de estado.
- El portal muestra una pantalla de progreso OTA que bloquea clics, se actualiza cada segundo y recarga automáticamente al terminar.
- Publicada en GitHub Releases (v0.5.4, `firmware.bin` de 3.167.088 bytes, SHA-256 `deff1e990455d130b09ea6f868383a72000cc1a9a3c0f6aeee3d14f5f9d3fc27`); lista para descarga e instalación OTA.

## v0.5.3 - 2026-11-13

- Publicada por OTA; pendiente de confirmación en campo.
- Durante una actualización OTA el LCD muestra pantalla negra con "ACTUALIZACION EN CURSO", barra de porcentaje y aviso de no reiniciar ni apagar.
- El portal muestra una pantalla de progreso OTA con barra que bloquea los clics, sube al inicio, sigue la reconexión tras el reinicio y recarga al terminar.
- Mientras se descarga el firmware, el portal solo atiende el estado OTA (las demás peticiones responden 503).

## v0.5.2 - 2026-11-13

- Publicada por OTA; pendiente de confirmación en campo.

- Wi-Fi: la placa ya no reintenta sin fin una red guardada ausente. Sin clientes en el AP, escanea y solo se conecta si una red guardada es visible (mejor RSSI); si no, espera 120 s y el portal indica "Sin red de confianza visible". Con clientes en el AP no hay reintentos. Se desactiva el auto-reconnect de la librería.
- El escaneo manual cancela el intento STA pendiente, evitando el error "Falló el escaneo pasivo de Wi-Fi".
- Portal: el refresco periódico ya no mueve el scroll de Resumen y se pausa mientras se escanea.
- BOOT: se mantiene el ciclo original de QR (AP, BLE, portal por red); verificado en la LCD.
- "Corroborar HTML": tiempos de espera ampliados (conexión 2,5 s con un reintento; cabeceras 4 s).
- Buscadores SADP, Dahua y genéricos: botón "Comprobar HTML" y columna "Servidor HTML" con enlace a los puertos 80/8080 que respondan HTML. El servidor acepta las IP descubiertas por estos buscadores.
- Inventario CCTV: al elegir una IP conocida se autocompletan MAC, nombre, fabricante, modelo, firmware y tipo (desde ARP, reservas del gateway e historial de descubrimientos), sin sobrescribir lo escrito a mano.

## v0.5.1 - 2026-11-13

- Versión confirmada como estable por el usuario ("perfecto") y publicada por OTA en `resKte/cctv-scanner-ota` (release v0.5.1, `firmware.bin` de 3.153.008 bytes, SHA-256 `b8ac2331317d86379e11034cdbb8bf8be49c8ba334fa529bcd2bf46501168131`). `stable.json` en `main` apunta a esta versión.
- OTA verificada de extremo a extremo: la placa detectó v0.5.1 desde el manifiesto, se actualizó con confirmación `ACTUALIZAR` y reporta `currentVersion: v0.5.1`.
- OTA: la lectura del manifiesto se hace por Content-Length (GitHub raw usa keep-alive y antes la lectura quedaba colgada). La descarga exige que el tamaño recibido coincida con el del manifiesto, usa lectura manual con tiempos límite y mantiene la validación SHA-256 y el rollback.
- OTA: se corrigió un desvío de la autorización física con BOOT (desbordamiento del contador de tiempo) para cambiar la URL del manifiesto.
- Se permite `:` en los valores de activos (MAC con separadores) al sanitizarlos.
- El guardado de redes guardadas es seguro ante escrituras parciales.
- Correcciones menores: bandera de credenciales BLE, condición de carrera al formatear la SD en el estado JSON, límites del historial (300 registros en el JSON, 2000 en CSV) y colocación acotada de etiquetas del espectro.
- El historial completo de equipos CCTV muestra primero el registro más reciente.
- El portal sigue abierto, sin autenticación ni CSRF, por decisión del usuario.

## En preparación (identidad de equipos; v0.5.0 sigue estable)

- Se expone un ID técnico derivado de la MAC de fábrica del ESP32-S3 y se puede asignar un alias de inventario (`CCTV-0001`) desde el portal.
- El estado, el LOG persistente y la salida Serial incluyen versión, modelo/revisión del chip, núcleos, Flash, PSRAM, contador persistente de arranques, motivo del último reset e ID de arranque. La salida Serial incorpora una línea `DEVICE_INFO` en JSON para facilitar una futura herramienta de PC.
- La primera placa se cargó por USB en COM7. El API y el portal reportan ID `CCTV-S3-288485556C3C`, alias `CCTV-0001`, ESP32-S3 rev. 2, 16 MB Flash, 8 MB PSRAM y contador de arranques persistente.
- No se queman eFuses ni se afirma protección anticlonación en esta etapa: el ID técnico identifica el chip, pero para autenticar reportes y dificultar clones se necesita después aprovisionar una clave criptográfica única y validar Secure Boot/flash encryption. Falta la confirmación del usuario; v0.5.0 sigue estable.

## En preparación (actualizaciones OTA; v0.5.0 sigue estable)

- Se añade configuración de un manifiesto público de versiones estables desde el portal. La ESP32 consulta por HTTPS al tener conexión y cada 6 horas; los errores de red se reintentan a los 30 segundos.
- En Resumen se añade el botón “Actualizar dispositivo”, deshabilitado cuando no hay una versión más nueva. La consulta solo anuncia la versión encontrada; la descarga e instalación requieren confirmación explícita y validan tamaño y SHA-256 antes de activar la partición OTA inactiva.
- Compilada y cargada por USB en COM7. Se verificó el botón deshabilitado y que la API rechace la solicitud si no hay una versión disponible. La placa aún no tiene configurada una URL de manifiesto; falta publicar/configurar una Release y probar una actualización real. v0.5.0 sigue estable.
- Cambiar la URL del manifiesto requiere una autorización física breve con BOOT, para impedir que un cliente local modifique silenciosamente la fuente de firmware.
- El primer firmware con OTA debe cargarse por USB. El arranque del nuevo firmware comprueba pantalla, LittleFS y punto de acceso antes de confirmar la imagen; si falla, el bootloader revierte a la versión anterior.
- Se documenta en `OTA.md` el formato del manifiesto y la publicación manual en GitHub Releases. Falta crear/configurar el repositorio público, cargar por USB, publicar un binario de prueba y validar la actualización OTA en la placa. No se asigna una versión estable hasta que el usuario confirme la prueba.

## En preparación (próxima versión de prueba; v0.5.0 sigue estable)

- Se integra una búsqueda manual Dahua DHDiscover en “Dispositivos LAN”, con consulta multicast DHIP y presentación de IP, MAC, modelo, clase, firmware y número de serie cuando la cámara responde. No realiza barrido de direcciones, intenta acceso ni autenticación, ni modifica el inventario.
- Compilada y cargada en COM7; la tarjeta y la ruta respondieron. La prueba inicial no recibió respuestas compatibles; falta probarla con un equipo Dahua conectado antes de confirmar la función o asignar una versión estable.
- Se añade “BUSCAR DISPOSITIVOS GENÉRICOS” para consultas multicast ONVIF/WS-Discovery, SSDP/UPnP y mDNS/DNS-SD. SADP y DHDiscover quedan excluidos de esta búsqueda.
- Compilada y cargada en COM7; la tarjeta y la ruta respondieron. La prueba inicial no encontró servicios genéricos compatibles en la LAN; falta validarla con equipos que anuncien alguno de estos protocolos. Esta función sigue pendiente de validación; v0.5.0 permanece estable para el portal de configuración Wi-Fi.
- Se agrega el historial persistente de detecciones SADP, DHDiscover y genéricas en `/discovery_history.csv` en la microSD, con fecha/hora local, protocolo, IP, MAC, identificación, red y datos disponibles. La pestaña “Dispositivos LAN” muestra los 200 registros recientes; los errores de almacenamiento se notifican sin ocultar los resultados encontrados. Pendiente de prueba de campo; v0.5.0 sigue siendo la versión estable del portal.

## v0.5.0 - 2026-11-13

- Versión confirmada como estable por el usuario tras probar el portal cautivo de configuración Wi-Fi.
- El primer toque corto de BOOT muestra un QR para conectarse al AP de la ESP32 y acceder al portal en `http://192.168.4.1`. El usuario confirmó que el AP permanece disponible mientras selecciona y configura la red Wi-Fi desde la página principal.
- Se conservan el aprovisionamiento BLE, el QR de acceso por STA y la pantalla de estado de la microSD.
- Firmware compilado y cargado en COM7; el usuario confirmó que la configuración funcionó correctamente.

## v0.4.0 - 2026-11-12

- Versión confirmada como estable por el usuario tras probar el firmware.
- “Dispositivos LAN” incluye una búsqueda SADP manual para Hikvision/EZVIZ mediante una consulta multicast en la STA conectada y escucha de 3 segundos. Presenta IP, MAC, modelo y firmware de los equipos que responden; no barre direcciones, intenta acceso ni autenticación, ni modifica el inventario.
- Compilada y cargada en COM7. La interfaz respondió y la búsqueda manual detectó un dispositivo SADP en la LAN; el usuario confirmó que funciona correctamente.

## v0.3.0 - 2026-11-12

- Versión confirmada como estable por el usuario tras probar el firmware.
- El historial completo de CCTV se conserva en `cctv_history.csv` en la microSD. Cada alta nueva registra fecha/hora local del navegador y RSSI medido por la ESP32; se puede descargar el CSV del día local actual. Los equipos anteriores se preservan sin atribuirles fecha ni señal histórica.
- El inventario CCTV conserva sus registros permanentes en LittleFS y permite IP observadas, temporales o ninguna. El historial de la SD incluye los equipos existentes y las altas nuevas.
- El almacenamiento informa bytes exactos para evitar que registros pequeños aparezcan como 0 KB.
- La prueba de enlace muestra la respuesta ICMP del gateway, el RSSI clasificado por color y la descarga de Internet. No presenta el ping como tasa de transferencia local.
- Se retiraron las operaciones CSV IEEE/OUI del firmware; el fabricante del gateway se consulta individualmente por MAC y se muestra en Resumen.
- Compilada y cargada en COM7. Portal e historial comprobados; la descarga CSV diaria responde correctamente. El usuario confirmó que la versión funciona.

## Bitácora de desarrollo de v0.3.0

Las siguientes notas conservan el estado de cada revisión mientras se desarrollaba esta versión. Los pendientes que figuran aquí fueron supersedidos por la confirmación estable de v0.3.0.

- Se eliminó del firmware la descarga, carga, consulta local y migración de CSV IEEE/OUI, sin borrar archivos existentes en LittleFS ni en la microSD. El panel web sigue ocultando errores IEEE/OUI históricos (que permanecen disponibles por Serial). La MAC del gateway se consulta individualmente por HTTPS a `api.macvendors.com` y el fabricante, MAC, estado y enlace aparecen en Resumen.
- La prueba de enlace ahora combina el RTT ICMP hacia el gateway, RSSI con nivel de señal (buena ≥ -60 dBm, media de -61 a -70 dBm, mala < -70 dBm) y la descarga de Internet. El RTT del router no se presenta como Mbps; una medición local de transferencia requiere un servidor de prueba activo en el router.
- El estado de almacenamiento ahora transmite y representa bytes exactos, para que datos pequeños guardados en LittleFS no aparezcan redondeados como 0 KB. Se mantienen los archivos históricos de LittleFS y el contenido de la microSD.
- Firmware compilado y cargado en COM7; verificados 20.480/3.538.944 bytes usados en LittleFS y 1.536/15.389.934.080 bytes en la microSD. Los tres puntos guardados siguieron presentes tras la actualización.
- “Inventario CCTV” muestra un historial completo guardado en `cctv_history.csv` en la microSD. Las altas nuevas guardan fecha/hora local del navegador y RSSI medido por la ESP32; la descarga CSV filtra el día local. Los equipos ya existentes se migraron sin inventar fecha ni señal histórica.
- Compilado y cargado en COM7. Se validó en el portal la tabla con los tres equipos previos, el API (HTTP 200) y la descarga diaria CSV (HTTP 200, solo cabecera al no haber altas nuevas hoy); no se creó ningún equipo de prueba. Pendiente confirmación del usuario antes de declarar una versión estable.
- Compilado y cargado en COM7. Verificados HTTP 200 del portal, resolución del gateway como HUAWEI, ping al gateway (5,3 ms, RSSI -55 dBm) y descarga externa de 1 MB (2,131 Mbps). Pendiente la confirmación del usuario sobre el uso visual del test; no se asigna versión estable todavía.
- El alta del inventario CCTV admite dejar MAC y firmware vacíos; “Ninguna” guarda el equipo sin IP y “Ingresar IP manualmente” conserva una IP temporal excluida de las comparaciones de LAN. La selección de IP detectadas permanece separada. Compilado y cargado en COM7; en el portal se vieron las opciones distintas y tres IP observadas.
- El punto de verificación `Verificacion_RSSI` descargó 1 MB desde Cloudflare; el API y la tabla mostraron RSSI de -54 dBm y canal 11. Los cuatro puntos históricos consultados guardaban RSSI=0 y por eso aparecen como “No disponible”; no es posible reconstruir sus valores antiguos.
- “Inventario CCTV” ahora ofrece IP observadas por ARP y direcciones de la tabla del gateway, opción “Ninguna” con entrada manual y fabricantes HIKVISION, DAHUA, EZVIZ y OTRAS. Se ocultaron la importación de listas y la tabla de equipos registrados sin eliminar los datos ni las APIs.
- Las observaciones ARP se vacían al cambiar de red STA para no sugerir direcciones de una red anterior.
- Compilado y cargado en COM7. El portal respondió HTTP 200; la pestaña CCTV mostró dos IP detectadas, y se verificó que ya no aparecen la importación ni la tabla. Las APIs respondieron sin alterar el inventario existente. Pendiente confirmación del usuario antes de asignar versión estable.
- El importador del inventario CCTV acepta CSV/TSV y salidas de texto ARP como `arp -a`; valida IP/MAC por fila, continúa tras errores individuales y no marca éxito si no puede actualizar la tabla.
- Revisión compilada y cargada en COM7. En el portal de `192.168.100.123` se verificó una importación real CSV y la lectura de `arp -a`; se retiraron las dos filas sintéticas de prueba y se confirmó que el inventario quedó vacío. La continuación ante duplicados se verificó con respuestas simuladas.
- Se quitó IEEE de “Dispositivos LAN”: ya no se muestra titular por MAC ni hay controles de descarga/carga; la exportación ARP tampoco incluye fabricante.
- Las bases MA-L/MA-M/MA-S se actualizan automáticamente en segundo plano una vez por arranque, al resolver el host IEEE por DNS. Si aún no hay conexión, se vuelve a comprobar cada minuto; si la actualización ya se intentó, no se reintenta hasta el próximo arranque.
- “Corroborar HTML” solicita `GET /` en 80/8080, identifica HTML por `Content-Type` o por etiquetas del cuerpo, y espera 1,2 s entre peticiones. Solo admite STA conectada e IP válidas de dispositivos conocidos.
- El panel web del LOG oculta registros IEEE/OUI; se conservan en el archivo y en la salida Serial para diagnóstico.
- Esta revisión compiló correctamente y se cargó en COM7. Falta que el usuario valide “Corroborar HTML” y el filtro IEEE/OUI en Resumen; no se asigna versión estable hasta esa confirmación.
- Compilación y carga en COM7 completadas; falta confirmar en la placa que la interfaz LAN y la actualización automática funcionan. No se asigna versión estable hasta esa validación.
- “Resumen” incluye al final el LOG de errores: conserva hasta 32 KB en LittleFS con rotación, permite consultarlo/descargarlo desde el portal y lo vuelca por Serial al conectar USB. Registra errores del API, navegador, red, almacenamiento e IEEE; usa identificador de arranque y tiempo desde el encendido.
- Compilación y carga en COM7 completadas; verificado el volcado completo por Serial. El log registró los tres fallos IEEE `HTTP -1 (connection refused)`; falta confirmar visualmente el cuadro en Resumen. No se asigna versión estable hasta la validación del usuario.
- La tabla LAN distingue si cada fila procede de ARP, de una reserva del gateway o del inventario importado; aclara que ninguna reserva confirma conectividad actual.
- La pestaña LAN incluye un listado pasivo de SSID visibles en 2.4 GHz, incluidos posibles SSID invitados/virtuales. No infiere pertenencia al router ni detecta VLAN aisladas sin datos de configuración del router.
- El importador reconoce la columna `Description` de exportaciones TP-Link IP/MAC Binding como nombre del equipo; conserva el límite de 100 registros por operación.
- Se incorporaron al binario las 256 tablas TP-Link archivadas y un índice por IP/MAC del gateway. La tabla coincidente se muestra al conectar la STA; los vínculos no se consideran equipos activos y las comprobaciones continúan limitadas a IP conocidas.
- La MAC del gateway se asocia a la red Wi-Fi actual (SSID y BSSID); los resultados ARP de una asociación anterior se descartan para no mostrar vecinos de otra red.
- La disponibilidad y descarga de las bases IEEE ahora requieren encabezado correcto y al menos 100 asignaciones válidas en el CSV local.
- El último punto mostrado en la LCD solo se actualiza después de confirmar que se guardó correctamente en LittleFS.
- Se retiraron estilos de inicio de sesión sin componentes HTML asociados y el include `ctime` sin uso.
- Se movieron la base IEEE OUI y la revisión defensiva manual a “Dispositivos LAN”; los vecinos ARP se muestran ahora en la tabla LAN y ya no aparecen en “Inventario CCTV”.
- La tabla LAN permite probar ping a un dispositivo de la misma subred, de a uno por vez, con pausa mínima de un segundo, máximo de 30 pings por minuto, límite de 30 segundos y control para finalizarlo.
- La comprobación manual y secuencial usa HTTP GET de `/` en los puertos 80 y 8080, solo para IP conocidas de ARP, inventario o tabla del gateway; limita cada ejecución a 32 IP y respeta una pausa de 1,2 segundos entre peticiones. No barre la subred.
- Debajo de la tabla LAN se muestra el resultado de cada puerto con estado HTTP, latencia TCP medida por la ESP32 y enlace al servicio solo si declara `Content-Type: text/html`.
- Compilación y carga de esta prueba en COM7 completadas. El portal volvió a responder con STA y microSD montadas, y contiene la nueva tabla; falta que el usuario valide resultados con sus equipos. No se cambia `VERSION` ni se declara estable hasta su confirmación.
- Verificación en el portal después de la carga: dos dispositivos observados por ARP; uno respondió HTML en HTTP/80 y el otro respondió HTTP 404 sin HTML confirmado. La tabla mostró latencia y solo enlazó el resultado HTML; no se hizo un barrido de subred.
- Al responder un ping por fila, se comprueba HTTP/80 para esa IP: aparece una tilde verde enlazada si se confirma HTML, una cruz cian si no hay servidor HTML y un indicador ámbar si la comprobación no pudo completarse.
- La actualización IEEE administra MA-L, MA-M y MA-S en CSV separados y coteja la MAC por el prefijo asignado más específico (24, 28 o 36 bits). El portal muestra la disponibilidad de cada base y permite descargar cada CSV local por separado.
- El portal permite cargar por separado los CSV IEEE descargados en el equipo, con validación y reemplazo seguro; se añadió como alternativa porque la ESP32 recibe `connection refused` al conectar con los tres enlaces.
- La compilación con MA-L/MA-M/MA-S se cargó en COM7. El portal muestra los tres controles de instalación y la microSD montada; el estado indica que aún faltan las tres bases. Pendiente cargar los CSV reales y comprobar identificaciones; no se considera estable.
- El formato de microSD se trasladó del menú LCD/BOOT a Resumen > Almacenamiento del portal HTML, debajo de la barra de espacio. Requiere confirmación explícita y muestra una barra indeterminada y el resultado; no se simula un porcentaje.
- En una prueba serial anterior la ESP32 detectó la tarjeta como 29 MB, pero el formato no devolvió resultado tras 180 segundos y no se repitió. Con el firmware principal restaurado, el portal reporta la microSD montada con 14.33 GB totales y 0 KB usados.
- Compilación y carga de esta actualización en COM7 completadas. Se verificó la página y que el endpoint rechace un POST sin confirmación; no se inició otro formato. Falta confirmar visualmente la LCD y el resultado del formato. No se modifica `VERSION` hasta la confirmación del usuario.
- Compilación y carga de esta actualización en COM7 completadas; falta confirmar en el portal el resultado visual de ping/HTML y el funcionamiento de la LCD. No se modifica `VERSION` hasta la confirmación del usuario.
- Verificado tras cargar en COM7: el endpoint de la ESP32 detectó el gateway como HTML en HTTP/80. No se exploran puertos adicionales ni rangos de IP.
- Se añadieron barras de espacio usado/libre para LittleFS y microSD; la SD se marca como no disponible cuando no está montada.
- La tarjeta de almacenamiento se movió de “Dispositivos LAN” a “Resumen”, conservando las barras y la actualización de datos.
- Compilado y cargado en COM7 con el entorno principal; Serial mostró el intento IEEE fallido por “connection refused”. Falta confirmar visualmente la LCD y probar el portal; no se cambia `VERSION` hasta que el usuario confirme el funcionamiento.

## v0.2.0 - 2026-11-12

- Versión de prueba confirmada por el usuario como estable: LCD y portal funcionan.
- El campo de IP usa un teclado orientado a URL para facilitar la entrada del punto en dispositivos móviles.
- El portal muestra el último mensaje de diagnóstico de la microSD; no se formateó la tarjeta.
- Se puede descargar el inventario CCTV como CSV.
- Se pueden exportar como CSV los resultados del escaneo Wi-Fi y las observaciones ARP locales.
- Se aclara que la actualización IEEE requiere Internet.
- Verificado en la placa: escaneo de 6 redes y exportaciones CSV de escaneo y vecinos ARP.

## Pendiente

- Falta comprobar en un teléfono que el teclado permita ingresar puntos en la dirección IP.
- La microSD aparece montada en el portal con 14.33 GB y 0 KB usados; el resultado de la operación de formato anterior sigue sin confirmación explícita y no se ejecutó otro formato en esta actualización.
- La descarga directa de IEEE había fallado con `HTTP -1 (connection refused)`. La versión cargada ofrece la instalación manual desde el portal; faltan los CSV reales para probar las tres instalaciones y el cotejo en placa.
- Los datos adicionales persistidos son pequeños: actualmente hay un punto de 226 bytes en la respuesta JSON y ningún equipo en el inventario CCTV. Se dejan en LittleFS para evitar depender de la SD para esos datos; la configuración Wi-Fi permanece en NVS.
- Se integró ESP32Ping para probar con un único ping ICMP la puerta de enlace STA; compilado y cargado, prueba funcional pendiente.
- Se añadió una pestaña de dispositivos que combina observaciones ARP e inventario local, con enlaces HTTP; no realiza barrido activo.
- Serial identificó `sd_spi_probe` como el firmware que se estaba ejecutando cuando la LCD dejó de verse. Se restauró el entorno principal y Serial volvió a mostrar actividad de la aplicación; falta confirmar visualmente la LCD.
- PlatformIO ahora selecciona el entorno principal por defecto para reducir el riesgo de cargar por error un firmware de diagnóstico sin LCD.
- Los vecinos ARP son observaciones locales de la ESP32; no equivalen a la tabla completa del router.

## v0.1.0 - 2026-11-12

- Versión estable de prueba con la retroiluminación LCD corregida a GPIO46 y confirmada por el usuario.
- Verificado en esta versión: imagen en la LCD.
- Pendiente de validar o corregir: microSD, descarga del CSV IEEE y tabla ARP del gateway.
