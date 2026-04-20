# sistema-busqueda

Servidor HTTP concurrente en C++ que simula el backend de una institución diversa integrada a un sistema de búsqueda de personas desaparecidas.

Inspirado en el Manual Técnico de la Plataforma Única de Identidad (PUI) publicado en el Diario Oficial de la Federación el 23 de enero de 2026.


## ¿Qué hace?

Cuando llega una CURP al servidor, se lanza un thread dedicado que ejecuta 3 fases de búsqueda en paralelo con otros reportes activos:

- **Fase 1** — búsqueda de datos básicos
- **Fase 2** — búsqueda histórica (últimos 12 años)
- **Fase 3** — búsqueda continua cada 10 segundos hasta recibir desactivación

Cada coincidencia encontrada se persiste en SQLite. Múltiples reportes corren en paralelo con sincronización mediante mutex.

## Stack técnico

| Capa | Tecnología |
|---|---|
| Lenguaje | C++17 |
| HTTP | Sockets BSD raw |
| Concurrencia | std::thread + mutex + atomic |
| Base de datos | SQLite3 |
| Contenedor | Docker (Ubuntu 24) |
| Build | CMake |
| Orquestador | Makefile |
| Pruebas | test.sh con curl |


## Endpoints

| Endpoint | Método | Descripción |
|---|---|---|
| `/login` | POST | Autenticación, devuelve token |
| `/activar-reporte` | POST | Registra CURP y lanza búsqueda |
| `/activar-reporte-prueba` | POST | Prueba de conectividad |
| `/desactivar-reporte` | POST | Detiene búsqueda activa |
| `/status` | GET | Reportes activos en memoria |
| `/reportes` | GET | Lista completa en BD |


## Cómo correrlo

### Requisitos
- Docker

### Arrancar

```bash
make docker-build
make docker-run# sistema-busqueda
Concurrent HTTP server in C++ with multithreading, SQLite and Docker
