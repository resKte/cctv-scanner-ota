# 🚀 START HERE - Subir a GitHub Rápidamente

## ⚡ Manera Más Rápida (30 segundos)

Copia y pega esto en **PowerShell** en la carpeta del proyecto:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/upload_to_github.ps1
```

**¡Eso es todo!** El script hace todo automáticamente. ✨

---

## 📋 Si Prefieres Hacerlo Manualmente

Ejecuta estos comandos en orden:

```powershell
cd "C:\Users\yo}\Documents\PlatformIO\Projects\CCTV_Scanner_Clean"
git init
git add .
git commit -m "v0.5.4: OTA LCD negro y barra de progreso"
git remote add origin https://github.com/resKte/cctv-scanner-ota.git
git push -u origin main
```

---

## ✅ Verificar que Funcionó

1. Ve a: **https://github.com/resKte/cctv-scanner-ota**
2. Deberías ver todos tus archivos aquí

¡Listo! 🎉

---

## 📚 Documentación

Si necesitas más info:

- **Instrucciones Rápidas**: Lee `UPLOAD_TO_GITHUB.md`
- **Guía Completa**: Lee `GUIA_GITHUB_UPLOAD.md`
- **Checklist**: Lee `ARCHIVO_VERIFICACION.md`

---

## 🔧 Requisitos

- ✅ Git instalado ([descargar](https://git-scm.com/download/win))
- ✅ Tu usuario y token/contraseña de GitHub

---

**Versión**: v0.5.4  
**Estado**: ✅ Listo para producción
