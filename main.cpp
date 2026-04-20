#include <iostream>
#include <thread>
#include <mutex>
#include <map>
#include <string>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sqlite3.h>
#include <atomic>
#include <functional>

// Estructuras
struct Reporte {
    std::string id;
    std::string curp;
    std::string nombre;
    std::atomic<bool> activo{true};
};

// Variables globales
std::mutex mtx_log;
std::mutex mtx_reportes;
std::map<std::string, Reporte*> reportes_activos;
std::map<std::string, std::thread> hilos_busqueda;
sqlite3* db;
const int PUERTO = 8080;

// Base de datos
void inicializar_db() {
    int rc = sqlite3_open("data/pui.db", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "Error abriendo DB: " << sqlite3_errmsg(db) << std::endl;
        return;
    }

    const char* sql_reportes = R"(
        CREATE TABLE IF NOT EXISTS reportes (
            id TEXT PRIMARY KEY,
            curp TEXT NOT NULL,
            nombre TEXT,
            estado TEXT DEFAULT 'activo',
            fecha_activacion DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

    const char* sql_coincidencias = R"(
        CREATE TABLE IF NOT EXISTS coincidencias (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            reporte_id TEXT NOT NULL,
            fase INTEGER NOT NULL,
            descripcion TEXT,
            fecha DATETIME DEFAULT CURRENT_TIMESTAMP
        );
    )";

    sqlite3_exec(db, sql_reportes, nullptr, nullptr, nullptr);
    sqlite3_exec(db, sql_coincidencias, nullptr, nullptr, nullptr);

    std::lock_guard<std::mutex> lock(mtx_log);
    std::cout << "[DB] Base de datos inicializada correctamente" << std::endl;
}

void guardar_reporte_db(const std::string& id, const std::string& curp, const std::string& nombre) {
    std::string sql = "INSERT OR IGNORE INTO reportes (id, curp, nombre) VALUES ('"
                      + id + "','" + curp + "','" + nombre + "');";
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
}

void actualizar_estado_db(const std::string& id, const std::string& estado) {
    std::string sql = "UPDATE reportes SET estado='" + estado + "' WHERE id='" + id + "';";
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
}

void guardar_coincidencia_db(const std::string& reporte_id, int fase, const std::string& desc) {
    std::lock_guard<std::mutex> lock(mtx_log);
    std::string sql = "INSERT INTO coincidencias (reporte_id, fase, descripcion) VALUES ('"
                      + reporte_id + "'," + std::to_string(fase) + ",'" + desc + "');";
    sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    std::cout << "[COINCIDENCIA] Reporte " << reporte_id << " fase " << fase << ": " << desc << std::endl;
}

std::string listar_reportes_db() {
    std::string resultado = "[";
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, curp, nombre, estado, fecha_activacion FROM reportes;";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        bool primero = true;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (!primero) resultado += ",";
            resultado += "{";
            resultado += "\"id\":\"" + std::string((char*)sqlite3_column_text(stmt, 0)) + "\",";
            resultado += "\"curp\":\"" + std::string((char*)sqlite3_column_text(stmt, 1)) + "\",";
            resultado += "\"nombre\":\"" + std::string((char*)sqlite3_column_text(stmt, 2)) + "\",";
            resultado += "\"estado\":\"" + std::string((char*)sqlite3_column_text(stmt, 3)) + "\",";
            resultado += "\"fecha\":\"" + std::string((char*)sqlite3_column_text(stmt, 4)) + "\"";
            resultado += "}";
            primero = false;
        }
        sqlite3_finalize(stmt);
    }
    resultado += "]";
    return resultado;
}

// Lógica de búsqueda
void fase1(Reporte& reporte) {
    std::lock_guard<std::mutex> lock(mtx_log);
    std::cout << "[FASE 1] Buscando datos básicos de " << reporte.curp << std::endl;
    guardar_coincidencia_db(reporte.id, 1, "Datos básicos encontrados para CURP " + reporte.curp);
}

void fase2(Reporte& reporte) {
    {
        std::lock_guard<std::mutex> lock(mtx_log);
        std::cout << "[FASE 2] Búsqueda histórica de " << reporte.curp << std::endl;
    }
    // búsqueda histórica de 12 años
    for (int anio = 0; anio < 3 && reporte.activo; anio++) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        guardar_coincidencia_db(reporte.id, 2,
            "Registro histórico año -" + std::to_string(anio + 1) + " para " + reporte.curp);
    }
    guardar_coincidencia_db(reporte.id, 2, "Búsqueda histórica finalizada");
}

void fase3(Reporte& reporte) {
    {
        std::lock_guard<std::mutex> lock(mtx_log);
        std::cout << "[FASE 3] Iniciando búsqueda continua de " << reporte.curp << std::endl;
    }
    int ciclo = 0;
    while (reporte.activo) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (!reporte.activo) break;
        ciclo++;
        guardar_coincidencia_db(reporte.id, 3,
            "Búsqueda continua ciclo " + std::to_string(ciclo) + " para " + reporte.curp);
    }
    std::lock_guard<std::mutex> lock(mtx_log);
    std::cout << "[FASE 3] Búsqueda continua detenida para " << reporte.curp << std::endl;
}

void busqueda_completa(Reporte& reporte) {
    fase1(reporte);
    fase2(reporte);
    fase3(reporte);
}

// HTTP Parser 
std::string obtener_metodo(const std::string& request) {
    return request.substr(0, request.find(' '));
}

std::string obtener_ruta(const std::string& request) {
    size_t inicio = request.find(' ') + 1;
    size_t fin = request.find(' ', inicio);
    return request.substr(inicio, fin - inicio);
}

std::string obtener_body(const std::string& request) {
    size_t pos = request.find("\r\n\r\n");
    if (pos == std::string::npos) return "";
    return request.substr(pos + 4);
}

std::string extraer_campo(const std::string& body, const std::string& campo) {
    std::string clave = "\"" + campo + "\":\"";
    size_t inicio = body.find(clave);
    if (inicio == std::string::npos) return "";
    inicio += clave.length();
    size_t fin = body.find("\"", inicio);
    return body.substr(inicio, fin - inicio);
}

// Respuestas HTTP
std::string respuesta_ok(const std::string& body) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

std::string respuesta_error(int codigo, const std::string& mensaje) {
    std::string body = "{\"error\":\"" + mensaje + "\"}";
    std::string status = codigo == 400 ? "400 Bad Request" :
                         codigo == 401 ? "401 Unauthorized" :
                         codigo == 404 ? "404 Not Found" : "500 Internal Server Error";
    return "HTTP/1.1 " + status + "\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "\r\n" + body;
}

// Handlers
std::string handle_login(const std::string& body) {
    std::string usuario = extraer_campo(body, "usuario");
    std::string clave = extraer_campo(body, "clave");
    if (usuario.empty() || clave.empty())
        return respuesta_error(400, "usuario y clave requeridos");
    if (usuario != "PUI" || clave != "Clave$Segura123!")
        return respuesta_error(401, "Credenciales inválidas");
    return respuesta_ok("{\"token\":\"token-simulado-pui-2026\"}");
}

std::string handle_activar_reporte(const std::string& body) {
    std::string id   = extraer_campo(body, "id");
    std::string curp = extraer_campo(body, "curp");
    std::string nombre = extraer_campo(body, "nombre");

    if (id.empty() || curp.empty())
        return respuesta_error(400, "id y curp son requeridos");

    if (curp.length() != 18)
        return respuesta_error(400, "curp debe tener 18 caracteres");

    {
        std::lock_guard<std::mutex> lock(mtx_reportes);
        if (reportes_activos.count(id))
            return respuesta_error(400, "reporte ya existe");
    }

    Reporte* r = new Reporte{id, curp, nombre};
    guardar_reporte_db(id, curp, nombre);

    {
        std::lock_guard<std::mutex> lock(mtx_reportes);
        reportes_activos[id] = r;
        hilos_busqueda[id] = std::thread(busqueda_completa, std::ref(*r));
    }

    std::lock_guard<std::mutex> lock(mtx_log);
    std::cout << "[ACTIVAR] Reporte " << id << " CURP " << curp << std::endl;
    return respuesta_ok("{\"message\":\"Reporte activado correctamente\",\"id\":\"" + id + "\"}");
}

std::string handle_activar_prueba(const std::string& body) {
    std::string id = extraer_campo(body, "id");
    if (id.empty()) return respuesta_error(400, "id requerido");
    std::lock_guard<std::mutex> lock(mtx_log);
    std::cout << "[PRUEBA] Conectividad verificada id=" << id << std::endl;
    return respuesta_ok("{\"message\":\"Prueba de conectividad exitosa\"}");
}

std::string handle_desactivar_reporte(const std::string& body) {
    std::string id = extraer_campo(body, "id");
    if (id.empty()) return respuesta_error(400, "id requerido");

    std::lock_guard<std::mutex> lock(mtx_reportes);
    if (!reportes_activos.count(id))
        return respuesta_error(404, "reporte no encontrado");

    reportes_activos[id]->activo = false;
    actualizar_estado_db(id, "inactivo");

    if (hilos_busqueda[id].joinable())
        hilos_busqueda[id].join();

    delete reportes_activos[id];
    reportes_activos.erase(id);
    hilos_busqueda.erase(id);

    std::cout << "[DESACTIVAR] Reporte " << id << " detenido" << std::endl;
    return respuesta_ok("{\"message\":\"Reporte desactivado correctamente\"}");
}

std::string handle_status() {
    std::lock_guard<std::mutex> lock(mtx_reportes);
    int activos = reportes_activos.size();
    return respuesta_ok("{\"status\":\"ok\",\"reportes_activos\":" + std::to_string(activos) + "}");
}

std::string handle_reportes() {
    return respuesta_ok(listar_reportes_db());
}

// Manejo de conexión
void manejar_conexion(int client_fd) {
    char buffer[4096] = {0};
    read(client_fd, buffer, 4096);
    std::string request(buffer);

    std::string metodo = obtener_metodo(request);
    std::string ruta   = obtener_ruta(request);
    std::string body   = obtener_body(request);

    std::string respuesta;

    if (metodo == "POST" && ruta == "/login")
        respuesta = handle_login(body);
    else if (metodo == "POST" && ruta == "/activar-reporte")
        respuesta = handle_activar_reporte(body);
    else if (metodo == "POST" && ruta == "/activar-reporte-prueba")
        respuesta = handle_activar_prueba(body);
    else if (metodo == "POST" && ruta == "/desactivar-reporte")
        respuesta = handle_desactivar_reporte(body);
    else if (metodo == "GET" && ruta == "/status")
        respuesta = handle_status();
    else if (metodo == "GET" && ruta == "/reportes")
        respuesta = handle_reportes();
    else
        respuesta = respuesta_error(404, "ruta no encontrada");

    write(client_fd, respuesta.c_str(), respuesta.size());
    close(client_fd);
}

int main() {
    inicializar_db();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Error creando socket" << std::endl;
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in direccion{};
    direccion.sin_family      = AF_INET;
    direccion.sin_addr.s_addr = INADDR_ANY;
    direccion.sin_port        = htons(PUERTO);

    if (bind(server_fd, (sockaddr*)&direccion, sizeof(direccion)) == -1) {
        std::cerr << "Error en bind" << std::endl;
        return 1;
    }

    if (listen(server_fd, 10) == -1) {
        std::cerr << "Error en listen" << std::endl;
        return 1;
    }

    std::cout << "[SERVER] Servidor corriendo en puerto " << PUERTO << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd == -1) continue;

        std::lock_guard<std::mutex> lock(mtx_log);
        std::cout << "[SERVER] Nueva conexión aceptada" << std::endl;

        std::thread t(manejar_conexion, client_fd);
        t.detach();
    }

    sqlite3_close(db);
    return 0;
}
