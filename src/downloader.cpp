#include "../include/downloader.hpp"
#include "../include/config.hpp"
#include "../include/utils.hpp" // <-- Importamos nuestras nuevas herramientas
#include <td/telegram/td_json_client.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <filesystem>
#include <chrono>
#include <map>
#include <set>
#include <regex>   // Requerido para buscar archivos .001, .002
#include <fstream> // Requerido para leer el archivo .txt
#include <utility>

using namespace std;
using json = nlohmann::json;

// --- Estructura para la Fase de Escaneo ---
struct FileInfo {
    int id;
    string nombre;
    bool completado;
    string ruta_local;
};

// --- Variables Globales ---
void* client = nullptr;
atomic<bool> td_running{false};
thread td_thread;

vector<string> enlaces;
int total_enlaces = 0;
atomic<int> enlaces_info_recibidos{0}; 

int total_archivos_reales = 0;
atomic<int> archivos_procesados{0};    

map<int, FileInfo> archivos_escaneados; 
map<int, pair<int64_t, int64_t>> progreso_archivos;    
set<int> archivos_completados;      
set<int> archivos_esperados;        
vector<string> archivos_listos;     

// --- Prototipos internos ---
int64_t extract_message_id(const std::string& url);
void request_file_from_link(const string& link);
void request_download(int file_id);
bool handle_auth_state(const json& j);

// =======================================================
// EL HILO SECUNDARIO (Procesa eventos)
// =======================================================
void hilo_receptor_tdlib() {
    while (td_running) {
        const char* result = td_json_client_receive(client, 1.0);
        if (!result) continue;

        auto j = json::parse(result);
        string type = j["@type"];

        if (type == "messageLinkInfo") {
            auto message = j["message"];
            auto content = message["content"];

            if (content["@type"] == "messageDocument" || content["@type"] == "messageVideo") {
                string media_type = (content["@type"] == "messageDocument") ? "document" : "video";
                auto file = content[media_type][media_type];
                
                FileInfo info;
                info.id = file["id"];
                info.completado = file["local"]["is_downloading_completed"].get<bool>();
                info.ruta_local = file["local"]["path"];
                
                if (content[media_type].contains("file_name") && !content[media_type]["file_name"].is_null()) {
                    info.nombre = content[media_type]["file_name"];
                } else {
                    info.nombre = std::filesystem::path(info.ruta_local).filename();
                }

                archivos_escaneados[info.id] = info;
            }
            enlaces_info_recibidos++; 
        }
        else if (type == "updateFile") {
            auto file = j["file"];
            int file_id = file["id"];
            
            if (archivos_esperados.find(file_id) == archivos_esperados.end()) continue; 

            string local_path = file["local"]["path"]; 
            int64_t downloaded = file["local"]["downloaded_size"];
            int64_t total = file["expected_size"];

            if (total > 0) {
                progreso_archivos[file_id] = {downloaded, total};
            }

            if (file["local"]["is_downloading_completed"].get<bool>()) {
                if (!std::filesystem::exists(local_path)) continue;

                if (archivos_completados.find(file_id) == archivos_completados.end()) {
                    archivos_completados.insert(file_id);
                    
                    string filename = archivos_escaneados[file_id].nombre; 
                    
                    mover_archivo(local_path, download_path + filename);
                    archivos_listos.push_back(filename);
                    
                    cout << "\r✅ Descargado en Buffer: " << filename << string(40, ' ') << endl;
                    archivos_procesados++;
                }
            }
            else if (total > 0 && total_archivos_reales > 0) {
                int64_t sum_downloaded = 0;
                int64_t sum_total = 0;
                
                for (const auto& par : progreso_archivos) {
                    sum_downloaded += par.second.first;
                    sum_total += par.second.second;
                }
                
                int pct_global = (sum_total > 0) ? (sum_downloaded * 100) / sum_total : 0;

                cout << "\r⏳ [Completados: " << archivos_procesados << "/" << total_archivos_reales 
                     << "] Progreso global del lote: " << pct_global << "%" << string(20, ' ');
                cout.flush();
            }
        }
        else if (type == "error") {
            if (enlaces_info_recibidos < total_enlaces) enlaces_info_recibidos++;
            if (total_archivos_reales > 0 && archivos_procesados < total_archivos_reales) archivos_procesados++;
        }
    }
}

// =======================================================
// MOTOR CENTRAL DE DESCARGA
// =======================================================
void procesar_tarea(string link_start, string link_end, string destino, bool interactivo, string tipo_media = "S", bool crear_ruta = true) {
    size_t pos_start = link_start.find('?');
    if (pos_start != string::npos) link_start = link_start.substr(0, pos_start);
    size_t pos_end = link_end.find('?');
    if (pos_end != string::npos) link_end = link_end.substr(0, pos_end);

    user_download_path = destino;
    if (user_download_path.empty()) user_download_path = download_path;
    if (user_download_path.back() != '/') user_download_path += '/';

    if (!std::filesystem::exists(user_download_path)) {
        if (interactivo) {
            cout << "\n⚠️ La ruta especificada no existe: " << user_download_path << endl;
            cout << "¿Desea crear el directorio ahora? (s/n): ";
            char confirmar; cin >> confirmar; cin.ignore(256, '\n'); 
            if (confirmar == 's' || confirmar == 'S') {
                std::filesystem::create_directories(user_download_path);
            } else {
                cout << "❌ Descarga cancelada.\n"; return; 
            }
        } else {
            if (crear_ruta) {
                std::filesystem::create_directories(user_download_path);
                cout << "📂 Ruta creada automáticamente: " << user_download_path << "\n";
            } else {
                cout << "❌ La ruta no existe y se configuró 'N' para crearla. Saltando tarea...\n";
                return;
            }
        }
    }

    int64_t start_msg_id = extract_message_id(link_start);
    int64_t end_msg_id = extract_message_id(link_end);
    enlaces.clear();
    for (int64_t id = start_msg_id; id <= end_msg_id; id++) {
        string base_url = link_start.substr(0, link_start.find_last_of('/'));
        enlaces.push_back(base_url + "/" + to_string(id));
    }

    total_enlaces = enlaces.size();
    enlaces_info_recibidos = 0;
    archivos_escaneados.clear();

    cout << "\n🔍 Analizando enlaces...\n";
    for (const string& link : enlaces) request_file_from_link(link);

    while (enlaces_info_recibidos < total_enlaces) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    }

    total_archivos_reales = archivos_escaneados.size();
    if (total_archivos_reales == 0) {
        cout << "❌ No se encontraron archivos descargables.\n";
        if (interactivo) { cout << "Presiona Intro para volver al menú..."; cin.get(); }
        return;
    }

    bool hay_comprimidos = false;
    for (const auto& par : archivos_escaneados) {
        string f = par.second.nombre;
        if (f.find(".zip") != string::npos || f.find(".rar") != string::npos || 
            f.find(".7z") != string::npos || regex_search(f, regex(R"(\.\d{3}$)"))) {
            hay_comprimidos = true; break;
        }
    }

    bool auto_descomprimir = false;
    bool aplanar_carpetas = false; 

    if (hay_comprimidos) {
        if (interactivo) {
            cout << "📦 ¡Detectados archivos comprimidos (mezclados o puros)!\n";
            cout << "¿Desea extraer el contenido al Destino y borrar la caché al terminar? (s/n): ";
            char conf_desc; cin >> conf_desc; cin.ignore(256, '\n'); 
            if (conf_desc == 's' || conf_desc == 'S') {
                auto_descomprimir = true;
                cout << "\n📂 ¿Qué estructura desea para los archivos descomprimidos?\n";
                cout << " 1. Mantener carpetas completas (Ideal para Películas)\n";
                cout << " 2. Extraer archivos únicamente a la raíz (Ideal para Series)\n";
                cout << "Elige una opción (1 o 2): ";
                char conf_est; cin >> conf_est; cin.ignore(256, '\n');
                if (conf_est == '2') aplanar_carpetas = true;
            }
        } else {
            auto_descomprimir = true;
            if (tipo_media == "S" || tipo_media == "s" || tipo_media == "serie" || tipo_media == "Serie") {
                aplanar_carpetas = true; 
            } else {
                aplanar_carpetas = false; 
            }
        }
    } else {
        cout << "📄 Detectados " << total_archivos_reales << " archivos directos.\n";
    }

    archivos_procesados = 0; 
    archivos_completados.clear();
    archivos_esperados.clear();  
    progreso_archivos.clear();
    archivos_listos.clear();

    cout << "\n📥 Iniciando descargas al Buffer...\n";
    for (const auto& par : archivos_escaneados) {
        const FileInfo& info = par.second;
        archivos_esperados.insert(info.id);

        if (info.completado && std::filesystem::exists(info.ruta_local)) {
            archivos_completados.insert(info.id);
            int64_t fake_size = 1000; 
            progreso_archivos[info.id] = {fake_size, fake_size}; 
            
            mover_archivo(info.ruta_local, download_path + info.nombre);
            archivos_listos.push_back(info.nombre);
            cout << "\r✅ [Caché] Movido al Buffer: " << info.nombre << string(40, ' ') << endl;
            archivos_procesados++;
        } else {
            request_download(info.id);
        }
    }

    while (archivos_procesados < total_archivos_reales) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); 
    }

    cout << "\r" << string(80, ' ') << "\r"; 
    cout << "🎉 ¡Descarga en Buffer completada!\n\n";

    if (auto_descomprimir) {
        vector<string> principales;
        vector<string> directos;
        
        for (const string& f : archivos_listos) {
            bool es_comp = (f.find(".zip") != string::npos || f.find(".rar") != string::npos || 
                            f.find(".7z") != string::npos || regex_search(f, regex(R"(\.\d{3}$)")));

            if (es_comp) {
                if (f.find(".001") != string::npos || f.find(".part1.rar") != string::npos || f.find(".part01.rar") != string::npos) {
                    principales.push_back(f);
                } else if ((f.find(".rar") != string::npos && f.find(".part") == string::npos) || 
                           (f.find(".zip") != string::npos && f.find(".zip.") == string::npos) ||
                           (f.find(".7z") != string::npos && f.find(".7z.") == string::npos)) {
                    principales.push_back(f);
                }
            } else {
                directos.push_back(f);
            }
        }

        string temp_dir = download_path + "cuarentena_7z/";
        filesystem::create_directories(temp_dir);
        bool algun_error = false;

        for (size_t i = 0; i < principales.size(); i++) {
            cout << "📦 Descomprimiendo paquete (" << (i+1) << "/" << principales.size() << "): " << principales[i] << "\n";
            if (!extraer_con_progreso(download_path + principales[i], temp_dir)) algun_error = true;
        }

        cout << "\n✅ FASE DE DESCOMPRESIÓN FINALIZADA.\n";
        cout << "🧹 Limpiando basura de los nombres...\n";
        limpiar_basura_nombres(temp_dir);

        cout << "🚚 Moviendo archivos al Destino final...\n";
        if (aplanar_carpetas) {
            for (const auto& entry : filesystem::recursive_directory_iterator(temp_dir)) {
                if (entry.is_regular_file()) { 
                    string nombre = entry.path().filename().string();
                    mover_archivo(entry.path().string(), user_download_path + nombre);
                }
            }
        } else {
            for (const auto& entry : filesystem::directory_iterator(temp_dir)) {
                string nombre = entry.path().filename().string();
                mover_archivo(entry.path().string(), user_download_path + nombre);
            }
        }
        
        regex basuras(R"(\s*\[.*?\])");
        for (const string& f : directos) {
            string f_limpio = regex_replace(f, basuras, "");
            mover_archivo(download_path + f, user_download_path + f_limpio);
        }

        filesystem::remove_all(temp_dir); 
        for (const string& f : archivos_listos) {
            if (f.find(".zip") != string::npos || f.find(".rar") != string::npos || 
                f.find(".7z") != string::npos || regex_search(f, regex(R"(\.\d{3}$)"))) {
                std::filesystem::remove(download_path + f);
            }
        }
    } else {
        cout << "🚚 Moviendo archivos directos...\n";
        regex basuras(R"(\s*\[.*?\])");
        for (const string& f : archivos_listos) {
            string f_limpio = regex_replace(f, basuras, "");
            mover_archivo(download_path + f, user_download_path + f_limpio);
        }
    }

    cout << "✅ Operación completada con éxito para esta ruta.\n";
    if (interactivo) { cout << "\nPresiona Intro para volver al menú..."; cin.get(); }
}


// =======================================================
// OPCIÓN 1: DESCARGA INTERACTIVA
// =======================================================
void opcion_descarga_rango() {
    string link_start, link_end, destino;
    cout << "\n🔗 Introduce el enlace INICIAL del rango: "; cin >> link_start;
    cout << "🔗 Introduce el enlace FINAL del rango: "; cin >> link_end;
    cout << "📂 Especifique ruta DESTINO (deje vacío para [" << download_path << "]): ";
    cin.ignore(); getline(cin, destino);
    
    procesar_tarea(link_start, link_end, destino, true);
}

// =======================================================
// OPCIÓN 2: DESCARGA POR LOTES (TXT)
// =======================================================
void opcion_descarga_archivo() {
    string ruta_archivo;
    cout << "\n📄 Introduce la ruta exacta del archivo de texto (ej. /home/usuario/lista.txt): ";
    cin >> ruta_archivo;
    cin.ignore(256, '\n');

    ifstream archivo(ruta_archivo);
    if (!archivo.is_open()) {
        cout << "❌ Error: No se pudo abrir el archivo. Comprueba que la ruta es correcta.\n";
        cout << "Presiona Intro para volver al menú...";
        cin.get();
        return;
    }

    vector<vector<string>> tareas;
    string linea;
    while (getline(archivo, linea)) {
        if (linea.empty() || linea[0] == '#') continue; 
        
        vector<string> tokens = separar_linea(linea, '|');
        if (tokens.size() >= 3) {
            string tipo = (tokens.size() >= 4) ? tokens[3] : "S"; 
            string crear = (tokens.size() >= 5) ? tokens[4] : "S";
            
            tareas.push_back({tokens[0], tokens[1], tokens[2], tipo, crear});
        } else {
            cout << "⚠️ Ignorando línea mal formateada: " << linea << "\n";
        }
    }
    archivo.close();

    if (tareas.empty()) {
        cout << "❌ No se encontraron tareas válidas en el archivo.\n";
        cout << "Presiona Intro para volver al menú...";
        cin.get();
        return;
    }

    cout << "\n🚀 ¡Iniciando procesamiento por lotes! Se han cargado " << tareas.size() << " tareas.\n";
    cout << "☕ Puedes dejar esto trabajando solo. El sistema automatizará todo el proceso.\n\n";

    for (size_t i = 0; i < tareas.size(); i++) {
        cout << string(60, '=') << "\n";
        cout << "🎬 PROCESANDO TAREA [" << (i+1) << "/" << tareas.size() << "]\n";
        cout << "📁 Destino: " << tareas[i][2] << "\n";
        cout << "⚙️  Tipo: " << (tareas[i][3] == "S" || tareas[i][3] == "s" ? "Serie (Aplanar)" : "Película (Carpetas)") << "\n";
        cout << string(60, '=') << "\n";
        
        bool crear_bool = (tareas[i][4] == "S" || tareas[i][4] == "s" || tareas[i][4] == "Si" || tareas[i][4] == "si");
        
        procesar_tarea(tareas[i][0], tareas[i][1], tareas[i][2], false, tareas[i][3], crear_bool);
        cout << "\n";
    }

    cout << "🎉 ¡TODO EL PROCESO POR LOTES HA TERMINADO!\n";
    cout << "Presiona Intro para volver al menú...";
    cin.get();
}

// =======================================================
// INICIALIZACIÓN Y AUTENTICACIÓN
// =======================================================
bool iniciar_telegram() {
    json log_req = {{"@type", "setLogVerbosityLevel"}, {"new_verbosity_level", 0}};
    td_json_client_execute(nullptr, log_req.dump().c_str());

    client = td_json_client_create();
    cout << "🔄 Conectando a Telegram..." << endl;

    bool authenticated = false;
    while (!authenticated) {
        const char* result = td_json_client_receive(client, 1.0);
        if (!result) continue;
        auto j = json::parse(result);
        if (j["@type"] == "updateAuthorizationState") {
            authenticated = handle_auth_state(j);
        }
    }

    td_running = true;
    td_thread = thread(hilo_receptor_tdlib);
    return true;
}

void detener_telegram() {
    td_running = false;
    if (td_thread.joinable()) td_thread.join();
    td_json_client_destroy(client);
}

// =======================================================
// FUNCIONES DE RED INTERNAS
// =======================================================
int64_t extract_message_id(const std::string& url) {
    size_t last_slash = url.find_last_of('/');
    if (last_slash == std::string::npos) return -1;
    return std::stoll(url.substr(last_slash + 1));
}

void request_file_from_link(const string& link) {
    json req = {{"@type", "getMessageLinkInfo"}, {"url", link}};
    td_json_client_send(client, req.dump().c_str());
}

void request_download(int file_id) {
    json req = {{"@type", "downloadFile"}, {"file_id", file_id}, {"priority", 1}, {"offset", 0}, {"limit", 0}, {"synchronous", false}};
    td_json_client_send(client, req.dump().c_str());
}

bool handle_auth_state(const json& j) {
    auto state = j["authorization_state"]["@type"];
    if (state == "authorizationStateWaitTdlibParameters") {
        json params = {
            {"@type", "setTdlibParameters"}, {"use_test_dc", false}, {"database_directory", "tdlib_db"},
            {"files_directory", download_path}, {"use_file_database", true}, {"use_chat_info_database", true},
            {"use_message_database", true}, {"use_secret_chats", true}, {"api_id", api_id},
            {"api_hash", api_hash}, {"system_language_code", "es"}, {"device_model", "PC"},
            {"system_version", "Linux"}, {"application_version", "2.0"}
        };
        td_json_client_send(client, params.dump().c_str());
    }
    else if (state == "authorizationStateWaitPhoneNumber") {
        string phone; cout << "Introduce tu número (+34...): "; cin >> phone;
        json req = {{"@type", "setAuthenticationPhoneNumber"}, {"phone_number", phone}};
        td_json_client_send(client, req.dump().c_str());
    }
    else if (state == "authorizationStateWaitCode") {
        string code; cout << "Introduce el código: "; cin >> code;
        json req = {{"@type", "checkAuthenticationCode"}, {"code", code}};
        td_json_client_send(client, req.dump().c_str());
    }
    else if (state == "authorizationStateWaitPassword") {
        string pass; cout << "Introduce tu contraseña 2FA: "; cin >> pass;
        json req = {{"@type", "checkAuthenticationPassword"}, {"password", pass}};
        td_json_client_send(client, req.dump().c_str());
    }
    else if (state == "authorizationStateReady") {
        cout << "✅ Autenticación con Telegram exitosa." << endl;
        return true;
    }
    return false;
}