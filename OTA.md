# Actualizaciones OTA desde GitHub

La ESP32 descarga actualizaciones mediante conexiones HTTPS salientes; no requiere DDNS, puertos entrantes ni un token de GitHub en el firmware. El repositorio y sus Releases deben ser públicos.

## Preparación del repositorio

1. Usa el repositorio público [resKte/cctv-scanner-ota](https://github.com/resKte/cctv-scanner-ota).
2. En una GitHub Release, publica el binario como `firmware.bin`. La URL debe seguir este formato:
   `https://github.com/resKte/cctv-scanner-ota/releases/download/ETIQUETA/firmware.bin`
3. Calcula el SHA-256 y tamaño del binario desde PowerShell:

   ```powershell
   Get-FileHash .pio\build\CCTV_Scanner_Clean\firmware.bin -Algorithm SHA256
   (Get-Item .pio\build\CCTV_Scanner_Clean\firmware.bin).Length
   ```

4. Solo después de que el binario de la Release esté disponible, publica `ota/stable.json` en `main` con la versión, URL, SHA-256 y tamaño exactos:

   ```json
   {
     "version": "v0.5.0",
     "url": "https://github.com/resKte/cctv-scanner-ota/releases/download/v0.5.0/firmware.bin",
     "sha256": "205c862f5c90fe84628c37f16a4c85a001b4ae4d957e171e7f8a8657894b00bf",
     "size": 3149984
   }
   ```

   Estos valores corresponden al binario local `v0.5.0` de 3.149.984 bytes. Para otras versiones, usa el SHA-256 y tamaño reales del `.bin`. La versión debe ser `vMAYOR.MENOR.PARCHE`; la placa ignora versiones iguales o anteriores a la instalada.

## Configuración en la placa

1. El primer firmware que incorpora OTA se debe cargar por USB.
2. Conecta la ESP32 a una red Wi-Fi con Internet y abre el portal.
3. En **Resumen > Actualizaciones OTA desde GitHub**, mantén pulsado BOOT durante 1 segundo y, antes de 30 segundos, guarda esta URL:
   `https://raw.githubusercontent.com/resKte/cctv-scanner-ota/main/ota/stable.json`
4. **Guardar y comprobar** inicia una comprobación inmediata. Después, la placa consulta cada 6 horas mientras está conectada a Internet. Si la red aún no tiene salida a Internet o falla la consulta, reintenta a los 30 segundos. También se puede pulsar **Comprobar ahora**.
5. Si hay una versión más nueva, en **Resumen** aparece **Actualizar dispositivo** y el número de versión. La comprobación no descarga ni instala firmware. La persona debe pulsar el botón y confirmar; sin una actualización disponible, el botón permanece deshabilitado.

Tras la confirmación, el binario se descarga en la partición OTA inactiva y se comprueba su tamaño y SHA-256 antes de activarlo. HTTPS valida los certificados con el paquete CA integrado. El manifiesto y el binario deben pertenecer a la misma fuente pública confiable: SHA-256 detecta descargas corruptas, pero no sustituye una firma digital del manifiesto.

El bootloader conserva el firmware anterior. Tras reiniciar, la aplicación comprueba la pantalla, LittleFS y el punto de acceso antes de confirmar la nueva imagen; si esa prueba falla, se solicita rollback. No se debe declarar estable una versión hasta completar la prueba en la placa.

La versión compilada se toma del archivo `VERSION`; el proceso de compilación la valida y la incorpora automáticamente al firmware para evitar que el portal y el número de versión se desincronicen.
