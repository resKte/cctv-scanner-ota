# Script para subir CCTV_Scanner_Clean a GitHub
# Uso: powershell -ExecutionPolicy Bypass -File upload_to_github.ps1

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "📤 CCTV Scanner - GitHub Upload Script" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

$projectDir = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$gitURL = "https://github.com/resKte/cctv-scanner-ota.git"
$gitURL_SSH = "git@github.com:resKte/cctv-scanner-ota.git"

Write-Host "📁 Directorio del Proyecto: $projectDir" -ForegroundColor Yellow
Write-Host ""

# Verifica si Git está instalado
try {
    $gitVersion = git --version 2>&1
    Write-Host "✅ Git detectado: $gitVersion" -ForegroundColor Green
} catch {
    Write-Host "❌ ERROR: Git no está instalado" -ForegroundColor Red
    Write-Host "Descarga desde: https://git-scm.com/download/win" -ForegroundColor Yellow
    exit 1
}

# Cambiar al directorio del proyecto
Set-Location $projectDir
Write-Host "📂 Cambiando a: $projectDir" -ForegroundColor Yellow
Write-Host ""

# Verificar VERSION
if (Test-Path "VERSION") {
    $version = (Get-Content "VERSION").Trim()
    Write-Host "📌 Versión detectada: $version" -ForegroundColor Green
} else {
    Write-Host "⚠️  No se encontró archivo VERSION" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "Opciones de conexión:" -ForegroundColor Cyan
Write-Host "1. HTTPS (requiere usuario/token)"
Write-Host "2. SSH (requiere clave SSH configurada)"
Write-Host "3. Salir"
Write-Host ""
$choice = Read-Host "Selecciona una opción (1-3)"

switch ($choice) {
    "1" {
        $remoteURL = $gitURL
        $protocolName = "HTTPS"
    }
    "2" {
        $remoteURL = $gitURL_SSH
        $protocolName = "SSH"
    }
    "3" {
        Write-Host "Cancelado" -ForegroundColor Yellow
        exit 0
    }
    default {
        Write-Host "❌ Opción no válida" -ForegroundColor Red
        exit 1
    }
}

Write-Host ""
Write-Host "Usando protocolo: $protocolName" -ForegroundColor Cyan
Write-Host "URL: $remoteURL" -ForegroundColor Cyan
Write-Host ""

# Paso 1: Verificar si es un repositorio Git
if (-not (Test-Path ".git")) {
    Write-Host "🆕 Inicializando repositorio Git..." -ForegroundColor Cyan
    git init
    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ Error al inicializar Git" -ForegroundColor Red
        exit 1
    }
    Write-Host "✅ Repositorio inicializado" -ForegroundColor Green
} else {
    Write-Host "✅ Ya es un repositorio Git" -ForegroundColor Green
}

# Paso 2: Agregar archivos
Write-Host ""
Write-Host "📝 Agregando archivos..." -ForegroundColor Cyan
git add .
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Error al agregar archivos" -ForegroundColor Red
    exit 1
}
Write-Host "✅ Archivos agregados" -ForegroundColor Green

# Paso 3: Crear commit
Write-Host ""
Write-Host "💾 Creando commit..." -ForegroundColor Cyan
$commitMessage = "v0.5.4: OTA LCD negro y barra de progreso`n`n- Pantalla negra desde preparación de actualización`n- Barra de progreso durante OTA`n- Portal con pantalla de progreso que se recarga automáticamente`n- Publicado en GitHub Releases`n`nCo-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>"
git commit -m $commitMessage
if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Error al crear commit (¿No hay cambios nuevos?)" -ForegroundColor Yellow
} else {
    Write-Host "✅ Commit creado" -ForegroundColor Green
}

# Paso 4: Configurar remote
Write-Host ""
Write-Host "🔗 Configurando conexión remota..." -ForegroundColor Cyan
$existingRemote = git remote -v 2>&1 | Select-String "origin"

if ($null -eq $existingRemote) {
    Write-Host "   Agregando origen remoto..." -ForegroundColor Gray
    git remote add origin $remoteURL
    if ($LASTEXITCODE -ne 0) {
        Write-Host "❌ Error al agregar remote" -ForegroundColor Red
        exit 1
    }
    Write-Host "   ✅ Origen remoto agregado" -ForegroundColor Green
} else {
    Write-Host "   ✅ Origen remoto ya existe" -ForegroundColor Green
    Write-Host "   $existingRemote" -ForegroundColor Gray
    
    $update = Read-Host "¿Quieres actualizar la URL remota? (s/n)"
    if ($update -eq "s" -or $update -eq "S") {
        git remote set-url origin $remoteURL
        Write-Host "   ✅ URL remota actualizada" -ForegroundColor Green
    }
}

# Paso 5: Fetch opcional
Write-Host ""
Write-Host "📥 Descargando cambios del servidor..." -ForegroundColor Cyan
git fetch origin 2>&1
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ Fetch completado" -ForegroundColor Green
} else {
    Write-Host "⚠️  No hay conexión o repositorio vacío (continuando...)" -ForegroundColor Yellow
}

# Paso 6: Detectar rama
Write-Host ""
Write-Host "🔍 Detectando rama principal..." -ForegroundColor Cyan
$currentBranch = git rev-parse --abbrev-ref HEAD 2>&1
$mainBranch = if ($currentBranch -eq "HEAD") { "main" } else { $currentBranch }
Write-Host "   Rama actual: $mainBranch" -ForegroundColor Gray

# Paso 7: Push
Write-Host ""
Write-Host "🚀 Subiendo a GitHub..." -ForegroundColor Cyan
git push -u origin $mainBranch
if ($LASTEXITCODE -eq 0) {
    Write-Host "✅ ¡Cambios subidos correctamente!" -ForegroundColor Green
} else {
    Write-Host "⚠️  Hubo un error al subir" -ForegroundColor Yellow
    Write-Host "   Intenta manualmente: git push -u origin $mainBranch" -ForegroundColor Gray
}

# Resumen final
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "📊 Resumen de Estado" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
git status
Write-Host ""
Write-Host "🎉 ¡Proceso completado!" -ForegroundColor Green
Write-Host ""
Write-Host "Próximos pasos:" -ForegroundColor Yellow
Write-Host "1. Verifica: https://github.com/resKte/cctv-scanner-ota" -ForegroundColor Yellow
Write-Host "2. Crea un Release si es necesario" -ForegroundColor Yellow
Write-Host "3. Descarga el firmware.bin desde las Releases" -ForegroundColor Yellow
Write-Host ""
