#include "../include/downloader.hpp"
#include "../include/config.hpp"
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
#include <algorithm>
#include <regex>   
#include <cstdio>  

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
map<int, int> progreso_archivos;    
set<int> archivos_completados;      
set<int> archivos_esperados;        
vector<string> archivos_listos;     

// --- Prototipos internos ---
int64_t extract_message_id(const std::string& url);
void request_file_from_link(const string& link);
void request_download(int file_id);
bool handle_auth_state(const json& j);
void mover_archivo(const string& origen, const string& destino);

// =======================================================
// HERRAMIENTAS DE LIMPIEZA Y EXTRACCIÓN
// =======================================================

void limpiar_basura_nombres(const string& ruta) {
    regex basuras(R"(\s*\[.*?\])");
    vector<pair<filesystem::path, filesystem::path>> renombrados;

    for (const auto& entry : filesystem::recursive_directory_iterator(ruta)) {
        string filename = entry.path().filename().string();
        string new_filename = regex_replace(filename, basuras, "");
        
        if (filename != new_filename) {
            filesystem::path old_path = entry.path();
            filesystem::path new_path = old_path.parent_path() / new_filename;
            renombrados.push_back({old_path, new_path});
        }
    }

    sort(renombrados.begin(), renombrados.end(), [](const auto& a, const auto& b) {
        return a.first.string().length() > b.first.string().length();
    });

    for (const auto& par : renombrados) {
        filesystem::rename(par.first, par.second);
        cout << "✨ Nombre limpio: " << par.second.filename().string() << endl;
    }
}

bool extraer_con_progreso(const string& archivo_origen, const string& carpeta_destino) {
    string cmd = "7z x \"" + archivo_origen + "\" -o\"" + carpeta_destino + "\" -y -bsp1 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return false;

    char buffer[128];
    string last_perc = "";
    
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        string line(buffer);
        size_t pos = line.find('%');
        if (pos != string::npos) {
            int start = pos - 1;
            while (start >= 0 && isspace(line[start])) start--; 
            while (start >= 0 && isdigit(line[start])) start--;
            
            string perc = line.substr(start + 1, pos - start);
            if (perc != last_perc && !perc.empty()) {
                cout << "\r⏳ Extrayendo: " << perc << "   " << flush;
                last_perc = perc;
            }
        }
    }
    
    int result = pclose(pipe);
    cout << "\r" << string(50, ' ') << "\r"; 
    return (result == 0);
}


// =======================================================
// MOVER EN VEZ DE COPIAR (VERSIÓN SEGURA)
// =======================================================
void mover_archivo(const string& origen, const string& destino) {
    if (!std::filesystem::exists(origen)) return;
    try {
        if (std::filesystem::exists(destino) && std::filesystem::is_regular_file(destino)) {
            std::filesystem::remove(destino);
        }
        std::filesystem::rename(origen, destino); 
    } catch (...) {
        std::filesystem::copy(origen, destino, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove_all(origen);
    }
}

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
                int progreso = (downloaded * 100) / total;
                progreso_archivos[file_id] = progreso;
            }

            if (file["local"]["is_downloading_completed"].get<bool>()) {
                if (!std::filesystem::exists(local_path)) continue;

                if (archivos_completados.find(file_id) == archivos_completados.end()) {
                    archivos_completados.insert(file_id);
                    progreso_archivos.erase(file_id);

                    string filename = archivos_escaneados[file_id].nombre; 
                    
                    mover_archivo(local_path, download_path + filename);
                    archivos_listos.push_back(filename);
                    
                    cout << "\r✅ Descargado en Buffer: " << filename << string(40, ' ') << endl;
                    archivos_procesados++;
                }
            }
            else if (total > 0 && total_archivos_reales > 0) {
                string linea_progreso = "\r⏳ [Completados: " + to_string(archivos_procesados) + "/" + to_string(total_archivos_reales) + "] Activos: ";
                for (const auto& par : progreso_archivos) {
                    linea_progreso += to_string(par.second) + "%  ";
                }
                linea_progreso += string(20, ' ');
                cout << linea_progreso;
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
// LA OPCIÓN DEL MENÚ 
// =======================================================
void opcion_descarga_rango() {
    string link_start, link_end;

    cout << "\n🔗 Introduce el enlace INICIAL del rango: ";
    cin >> link_start;
    size_t pos_start = link_start.find('?');
    if (pos_start != string::npos) link_start = link_start.substr(0, pos_start);

    cout << "🔗 Introduce el enlace FINAL del rango: ";
    cin >> link_end;
    size_t pos_end = link_end.find('?');
    if (pos_end != string::npos) link_end = link_end.substr(0, pos_end);

    cout << "📂 Especifique ruta DESTINO (deje vacío para [" << download_path << "]): ";
    cin.ignore();
    getline(cin, user_download_path);

    if (user_download_path.empty()) user_download_path = download_path;
    if (user_download_path.back() != '/') user_download_path += '/';

    if (!std::filesystem::exists(user_download_path)) {
        cout << "\n⚠️ La ruta especificada no existe: " << user_download_path << endl;
        cout << "¿Desea crear el directorio ahora? (s/n): ";
        char confirmar;
        cin >> confirmar;
        cin.ignore(256, '\n'); 

        if (confirmar == 's' || confirmar == 'S') {
            try {
                std::filesystem::create_directories(user_download_path);
                cout << "✅ Directorio creado con éxito." << endl;
            } catch (const std::exception& e) {
                cout << "❌ Error al crear el directorio: " << e.what() << endl;
                return; 
            }
        } else {
            cout << "❌ Descarga cancelada." << endl;
            return; 
        }
    }

    int64_t start_msg_id = extract_message_id(link_start);
    int64_t end_msg_id = extract_message_id(link_end);

    enlaces.clear();
    for (int64_t id = start_msg_id; id <= end_msg_id; id++) {
        string base_url = link_start.substr(0, link_start.find_last_of('/'));
        enlaces.push_back(base_url + "/" + to_string(id));
    }

    // FASE 1: PRE-ESCANEO
    total_enlaces = enlaces.size();
    enlaces_info_recibidos = 0;
    archivos_escaneados.clear();

    cout << "\n🔍 Analizando enlaces...\n";
    for (const string& link : enlaces) {
        request_file_from_link(link);
    }

    while (enlaces_info_recibidos < total_enlaces) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); 
    }

    total_archivos_reales = archivos_escaneados.size();
    if (total_archivos_reales == 0) {
        cout << "❌ No se encontraron archivos descargables.\n";
        cout << "Presiona Intro para volver al menú...";
        cin.get();
        return;
    }

    bool hay_comprimidos = false;
    for (const auto& par : archivos_escaneados) {
        string f = par.second.nombre;
        if (f.find(".zip") != string::npos || f.find(".rar") != string::npos || 
            f.find(".7z") != string::npos || regex_search(f, regex(R"(\.\d{3}$)"))) {
            hay_comprimidos = true;
            break;
        }
    }

    // --- VARIABLES DE DECISIÓN ---
    bool auto_descomprimir = false;
    bool aplanar_carpetas = false; 

    if (hay_comprimidos) {
        cout << "📦 ¡Detectados archivos comprimidos (mezclados o puros)!\n";
        cout << "¿Desea extraer el contenido al Destino y borrar la caché al terminar? (s/n): ";
        char conf_desc;
        cin >> conf_desc;
        cin.ignore(256, '\n'); 
        
        if (conf_desc == 's' || conf_desc == 'S') {
            auto_descomprimir = true;
            
            cout << "\n📂 ¿Qué estructura desea para los archivos descomprimidos?\n";
            cout << " 1. Mantener carpetas completas (Ideal para Películas)\n";
            cout << " 2. Extraer archivos únicamente a la raíz (Ideal para Series)\n";
            cout << "Elige una opción (1 o 2): ";
            char conf_est;
            cin >> conf_est;
            cin.ignore(256, '\n');
            if (conf_est == '2') aplanar_carpetas = true;
        }
    } else {
        cout << "📄 Detectados " << total_archivos_reales << " archivos directos.\n";
    }

    // FASE 2: DESCARGA
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

    // FASE FINAL: PROCESAMIENTO HÍBRIDO
    if (auto_descomprimir) {
        vector<string> principales;
        vector<string> directos;
        
        // 1. Clasificar archivos: Comprimidos vs Directos (ej. .mkv)
        for (const string& f : archivos_listos) {
            bool es_comp = (f.find(".zip") != string::npos || f.find(".rar") != string::npos || 
                            f.find(".7z") != string::npos || regex_search(f, regex(R"(\.\d{3}$)")));

            if (es_comp) {
                // Si es un archivo maestro (ej. .001, o un .zip suelto) lo apuntamos
                if (f.find(".001") != string::npos || f.find(".part1.rar") != string::npos || f.find(".part01.rar") != string::npos) {
                    principales.push_back(f);
                } 
                else if ((f.find(".rar") != string::npos && f.find(".part") == string::npos) || 
                         (f.find(".zip") != string::npos && f.find(".zip.") == string::npos) ||
                         (f.find(".7z") != string::npos && f.find(".7z.") == string::npos)) {
                    principales.push_back(f);
                }
            } else {
                // Es un archivo directo multimedia
                directos.push_back(f);
            }
        }

        // SALA DE CUARENTENA (Solo para lo comprimido)
        string temp_dir = download_path + "cuarentena_7z/";
        filesystem::create_directories(temp_dir);
        bool algun_error = false;

        // 2. Extraer paquetes
        for (size_t i = 0; i < principales.size(); i++) {
            cout << "📦 Descomprimiendo paquete (" << (i+1) << "/" << principales.size() << "): " << principales[i] << "\n";
            bool ok = extraer_con_progreso(download_path + principales[i], temp_dir);
            if (!ok) {
                cout << "❌ Hubo un error al descomprimir este archivo.\n";
                algun_error = true;
            }
        }

        cout << "\n✅ FASE DE DESCOMPRESIÓN FINALIZADA.\n";
        if (algun_error) cout << "⚠️ Atención: Hubo fallos en algunos paquetes.\n";

        cout << "🧹 Limpiando basura de los nombres...\n";
        limpiar_basura_nombres(temp_dir);

        cout << "🚚 Moviendo archivos al Destino final...\n";
        
        // A) Mover lo extraído
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
        
        // B) Mover los archivos directos (Los .mkv que venían sueltos)
        regex basuras(R"(\s*\[.*?\])");
        for (const string& f : directos) {
            string f_limpio = regex_replace(f, basuras, "");
            mover_archivo(download_path + f, user_download_path + f_limpio);
            if (f != f_limpio) cout << "✨ Nombre limpio (Directo): " << f_limpio << endl;
        }

        // Limpieza final del Buffer: Solo se borra lo que era basura comprimida
        filesystem::remove_all(temp_dir); 
        for (const string& f : archivos_listos) {
            bool es_comp = (f.find(".zip") != string::npos || f.find(".rar") != string::npos || 
                            f.find(".7z") != string::npos || regex_search(f, regex(R"(\.\d{3}$)")));
            if (es_comp) {
                std::filesystem::remove(download_path + f);
            }
        }
        
        cout << "✅ Operación completada con éxito.\n";

    } else {
        // Modo movimiento directo
        cout << "🚚 Moviendo archivos...\n";
        regex basuras(R"(\s*\[.*?\])");
        
        for (const string& f : archivos_listos) {
            string f_limpio = regex_replace(f, basuras, "");
            mover_archivo(download_path + f, user_download_path + f_limpio);
            if (f != f_limpio) cout << "✨ Nombre limpio: " << f_limpio << endl;
        }
        cout << "✅ Operación completada.\n";
    }

    cout << "\nPresiona Intro para volver al menú...";
    cin.get();
}

// ... (Resto igual) ...
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