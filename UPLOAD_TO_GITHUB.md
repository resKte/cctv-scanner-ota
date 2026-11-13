# 📤 Subir a GitHub - Instrucciones Rápidas

## ⚡ Opción 1: Script Automático (RECOMENDADO)

Abre PowerShell en la carpeta del proyecto y ejecuta:

```powershell
cd "C:\Users\yo}\Documents\PlatformIO\Projects\CCTV_Scanner_Clean"
powershell -ExecutionPolicy Bypass -File scripts/upload_to_github.ps1
```

El script hará todo automáticamente:
- ✅ Inicializa Git (si no está inicializado)
- ✅ Agrega todos los archivos
- ✅ Crea un commit con v0.5.4
- ✅ Configura la conexión a GitHub
- ✅ Sube los cambios

---

## 🔧 Opción 2: Comandos Manuales

Si prefieres hacerlo paso a paso:

### Paso 1: Ir al proyecto
```powershell
cd "C:\Users\yo}\Documents\PlatformIO\Projects\CCTV_Scanner_Clean"
```

### Paso 2: Inicializar Git (primera vez)
```powershell
git init
```

### Paso 3: Agregar archivos
```powershell
git add .
```

### Paso 4: Crear commit
```powershell
git commit -m "v0.5.4: OTA LCD negro y barra de progreso"
```

### Paso 5: Conectar con GitHub (primera vez)
```powershell
git remote add origin https://github.com/resKte/cctv-scanner-ota.git
```

### Paso 6: Subir cambios
```powershell
git push -u origin main
```

---

## ✅ Verificar que Funcionó

1. Ve a: **https://github.com/resKte/cctv-scanner-ota**
2. Deberías ver:
   - Todos los archivos del proyecto
   - El CHANGELOG.md actualizado
   - El archivo VERSION con v0.5.4
   - Los scripts en la carpeta `scripts/`
   - Las tablas de router en `router_tables/`

---

## 🚨 Solución de Problemas

### Git no está instalado
Descarga desde: https://git-scm.com/download/win

### "fatal: not a git repository"
Ejecuta: `git init`

### "Permission denied" o errores de autenticación
Usa tu **token de GitHub** en lugar de contraseña:
1. Ve a: https://github.com/settings/tokens
2. Crea un token personal
3. Usa ese token cuando te pida contraseña

### "Everything up-to-date" (sin cambios)
- Modifica un archivo (ej: README.md)
- Luego: `git add .` y `git commit -m "mensaje"`

---

## 📊 Status Actual

- ✔️ Proyecto completo y compilado
- ✔️ Versión: **v0.5.4**
- ✔️ CHANGELOG actualizado
- ✔️ Archivos .gitignore configurados
- ✔️ 257 tablas de router incluidas
- ✔️ Scripts de automatización listos

---

## 🎉 ¡Listo!

Tus archivos están en GitHub y listos para:
- Descargas
- Colaboración
- Releases OTA
- Documentación
