#!/bin/bash

# Configuración 
BASE="http://localhost:8080"
PASS=0
FAIL=0

# Helpers
ok() {
    echo "✓ $1"
    PASS=$((PASS + 1))
}

fail() {
    echo "✗ $1"
    FAIL=$((FAIL + 1))
}

check() {
    local descripcion=$1
    local respuesta=$2
    local esperado=$3

    if echo "$respuesta" | grep -q "$esperado"; then
        ok "$descripcion"
    else
        fail "$descripcion → esperaba '$esperado', got: $respuesta"
    fi
}

# Tests 
echo ""
echo "=== sistema-busqueda test suite ==="
echo ""

# 1. Status
R=$(curl -s "$BASE/status")
check "GET /status responde ok" "$R" "ok"

# 2. Login correcto
R=$(curl -s -X POST "$BASE/login" \
  -H "Content-Type: application/json" \
  -d '{"usuario":"PUI","clave":"Clave$Segura123!"}')
check "POST /login con credenciales correctas" "$R" "token"

# 3. Login incorrecto
R=$(curl -s -X POST "$BASE/login" \
  -H "Content-Type: application/json" \
  -d '{"usuario":"PUI","clave":"wrongpass"}')
check "POST /login con credenciales incorrectas" "$R" "error"

# 4. Activar reporte de prueba
R=$(curl -s -X POST "$BASE/activar-reporte-prueba" \
  -H "Content-Type: application/json" \
  -d '{"id":"test-001"}')
check "POST /activar-reporte-prueba" "$R" "exitosa"

# 5. Activar reporte sin campos obligatorios
R=$(curl -s -X POST "$BASE/activar-reporte" \
  -H "Content-Type: application/json" \
  -d '{"nombre":"Sin CURP"}')
check "POST /activar-reporte sin campos obligatorios" "$R" "error"

# 6. Activar reporte con CURP inválida
R=$(curl -s -X POST "$BASE/activar-reporte" \
  -H "Content-Type: application/json" \
  -d '{"id":"test-002","curp":"CORTA"}')
check "POST /activar-reporte con CURP inválida" "$R" "error"

# 7. Activar 3 reportes simultáneos (concurrencia)
echo ""
echo "--- Prueba de concurrencia ---"
curl -s -X POST "$BASE/activar-reporte" \
  -H "Content-Type: application/json" \
  -d '{"id":"curp-001","curp":"XEXX010101HNEXXXA1","nombre":"Persona Uno"}' > /dev/null &

curl -s -X POST "$BASE/activar-reporte" \
  -H "Content-Type: application/json" \
  -d '{"id":"curp-002","curp":"XEXX010101HNEXXXA2","nombre":"Persona Dos"}' > /dev/null &

curl -s -X POST "$BASE/activar-reporte" \
  -H "Content-Type: application/json" \
  -d '{"id":"curp-003","curp":"XEXX010101HNEXXXA3","nombre":"Persona Tres"}' > /dev/null &

wait
sleep 2

# 8. Verificar que los 3 reportes están activos
R=$(curl -s "$BASE/reportes")
check "GET /reportes lista reportes después de concurrencia" "$R" "curp-001"

R=$(curl -s "$BASE/status")
check "GET /status muestra reportes activos en memoria" "$R" "reportes_activos"

# 9. Desactivar reporte inexistente
R=$(curl -s -X POST "$BASE/desactivar-reporte" \
  -H "Content-Type: application/json" \
  -d '{"id":"no-existe"}')
check "POST /desactivar-reporte con id inexistente" "$R" "error"

# 10. Desactivar reporte existente
R=$(curl -s -X POST "$BASE/desactivar-reporte" \
  -H "Content-Type: application/json" \
  -d '{"id":"curp-001"}')
check "POST /desactivar-reporte detiene thread correctamente" "$R" "desactivado"

# 11. Ruta inexistente
R=$(curl -s "$BASE/ruta-que-no-existe")
check "GET ruta inexistente devuelve 404" "$R" "error"

# Resultado final 
echo ""
echo "==================================="
echo "Resultado: $PASS pasaron, $FAIL fallaron"
echo "==================================="
echo ""

if [ $FAIL -eq 0 ]; then
    exit 0
else
    exit 1
fi
