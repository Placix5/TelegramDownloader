#include <td/telegram/td_json_client.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <fstream>

using namespace std;
using json = nlohmann::json;

// Configuración global (se rellenará en el main)
int api_id;
string api_hash;
string download_path;
string user_download_path;

// Prototipos
bool handle_auth_state(void* client, const json& j);
void request_file_from_link(void* client, const string& link);
bool handle_file_update(const json& j);
void request_download(void* client, int file_id);
void request_range_download(void* client, int64_t chat_id, int64_t start_id, int64_t end_id);
int64_t extract_message_id(const std::string& url);
void request_next_in_list(void* client, const vector<string>& links, int& actual, int total);

int main() {

    std::ifstream config_file("config/config.json");
    if (!config_file.is_open()) {
        cerr << "❌ Error: No se pudo abrir config/config.json. Asegúrate de que existe." << endl;
        return 1;
    }
    
    json config;
    config_file >> config;

    api_id = config["api_id"];
    api_hash = config["api_hash"];
    download_path = config["download_path"];
    user_download_path = download_path; // Por defecto asignamos la misma

    void* client = td_json_client_create();

    int64_t chat_id = 0;
    int64_t start_id = 0;
    int64_t end_id = 0;
    int link_count = 0;

    vector<string> links;

    int total = links.size();
    int actual = 0;

    json log_req = {
        {"@type", "setLogVerbosityLevel"},
        {"new_verbosity_level", 1}  // 0 = nada, 1 = errores, 2 = warnings, 3 = info, 4 = debug
    };
    std::string log_payload = log_req.dump();
    td_json_client_send(client, log_payload.c_str());

    // Bucle principal
    bool authenticated = false;
    bool requested = false;

    while (true) {
        const char* result = td_json_client_receive(client, 1.0);
        if (!result) continue;

        auto j = json::parse(result);
        string type = j["@type"];

        if (type == "updateAuthorizationState") {
            authenticated = handle_auth_state(client, j);
            if (authenticated && !requested) {
                cout << "🎉 Autenticado. Ahora pedimos el archivo..." << endl;
                string link_start, link_end;
                cout << "Introduce el enlace INICIAL del rango: ";
                cin >> link_start;
                cout << "Introduce el enlace FINAL del rango: ";
                cin >> link_end;

                // Una vez tenemos los enlaces de inicio y de fin, rellenamos la lista links
                // con los enlaces intermedios (asumiendo que son consecutivos)
                int64_t start_msg_id = extract_message_id(link_start);
                int64_t end_msg_id = extract_message_id(link_end);

                // Rellenamos la lista de enlaces
                for (int64_t id = start_msg_id; id <= end_msg_id; id++) {
                    string base_url = link_start.substr(0, link_start.find_last_of('/'));
                    links.push_back(base_url + "/" + to_string(id));
                    cout << "Añadido enlace: " << base_url + "/" + to_string(id) << endl;
                }
                
                cout << "Especifiique ruta de descarga (deje vacío para usar la predeterminada): ";
                cin.ignore(); // Limpiar el buffer de entrada
                getline(cin, user_download_path);

                cout << "\n📥 Iniciando descarga de archivos...\n";

                total = links.size();
                actual = 0;

                // Solicitamos el primero de los enlaces
                request_next_in_list(client, links, actual, total);

                requested = true;
            }
        }
        else if (type == "messageLinkInfo") {

            auto message = j["message"];
            auto content = message["content"];

            cout << "✅ Mensaje encontrado." << endl;
            cout << "Chat ID: " << message["chat_id"] << endl;
            cout << "ID de mensaje: " << message["id"] << endl;

            if (user_download_path.empty()) {
                user_download_path = download_path;
            }

            if (content["@type"] == "messageDocument") {
                cout << "📦 Tipo: Documento" << endl;

                auto file = content["document"]["document"];
                if (file["local"]["is_downloading_completed"].get<bool>()) {
                    cout << "✅ El archivo ya está descargado en local: " << file["local"]["path"] << endl;

                    string local_path = file["local"]["path"];
                    string filename = std::filesystem::path(local_path).filename();
                    string final_path = user_download_path + filename;

                    try {
                        std::filesystem::copy_file(local_path, final_path, std::filesystem::copy_options::overwrite_existing);
                        std::filesystem::remove(local_path);
                        cout << "\n✅ Archivo movido a: " << final_path << endl;
                    } catch (std::exception& e) {
                        cerr << "\n❌ Error moviendo archivo: " << e.what() << endl;
                    }

                    request_next_in_list(client, links, actual, total);
                }
                else
                {
                    int file_id = content["document"]["document"]["id"];
                    cout << "📦 Lanzando descarga para file_id: " << file_id << endl;
                    request_download(client, file_id);
                }
            }
            else if (content["@type"] == "messageVideo") {
                cout << "📦 Tipo: Video" << endl;

                auto file = content["video"]["video"];
                if (file["local"]["is_downloading_completed"].get<bool>()) {
                    cout << "✅ El vídeo ya está descargado en local: " << file["local"]["path"] << endl;

                    string local_path = file["local"]["path"];
                    string filename = std::filesystem::path(local_path).filename();
                    string final_path = user_download_path + filename;

                    try {
                        std::filesystem::copy_file(local_path, final_path, std::filesystem::copy_options::overwrite_existing);
                        std::filesystem::remove(local_path);
                        cout << "\n✅ Archivo movido a: " << final_path << endl;
                    } catch (std::exception& e) {
                        cerr << "\n❌ Error moviendo archivo: " << e.what() << endl;
                    }

                    request_next_in_list(client, links, actual, total);
                }
                else
                {
                    int file_id = content["video"]["video"]["id"];
                    cout << "📦 Lanzando descarga para file_id: " << file_id << endl;
                    request_download(client, file_id);
                }
            }
            else {
                cout << "❌ El mensaje no contiene un archivo descargable." << endl;
            }
        }
        else if (type == "updateFile") {
            if (handle_file_update(j)) {
                request_next_in_list(client, links, actual, total);
            }
        }
        else if (type == "error") {
            cerr << "❌ Error: " << j.dump() << endl;
        }
    }

    return 0;
}

// ------------------- Autenticación -------------------
bool handle_auth_state(void* client, const json& j) {
    auto state = j["authorization_state"]["@type"];

    if (state == "authorizationStateWaitTdlibParameters") {
        json params = {
            {"@type", "setTdlibParameters"},
            {"use_test_dc", false},
            {"database_directory", "tdlib_db"},
            {"files_directory", download_path},
            {"use_file_database", true},
            {"use_chat_info_database", true},
            {"use_message_database", true},
            {"use_secret_chats", true},
            {"api_id", api_id},
            {"api_hash", api_hash},
            {"system_language_code", "es"},
            {"device_model", "PC"},
            {"system_version", "Linux"},
            {"application_version", "1.0"}
        };
        td_json_client_send(client, params.dump().c_str());
    }
    else if (state == "authorizationStateWaitPhoneNumber") {
        string phone;
        cout << "Introduce tu número (+34...): ";
        cin >> phone;
        json req = {{"@type", "setAuthenticationPhoneNumber"}, {"phone_number", phone}};
        td_json_client_send(client, req.dump().c_str());
    }
    else if (state == "authorizationStateWaitCode") {
        string code;
        cout << "Introduce el código recibido: ";
        cin >> code;
        json req = {{"@type", "checkAuthenticationCode"}, {"code", code}};
        td_json_client_send(client, req.dump().c_str());
    }
    else if (state == "authorizationStateWaitPassword") {
        string pass;
        cout << "Introduce tu contraseña 2FA: ";
        cin >> pass;
        json req = {{"@type", "checkAuthenticationPassword"}, {"password", pass}};
        td_json_client_send(client, req.dump().c_str());
    }
    else if (state == "authorizationStateReady") {
        cout << "✅ Autenticación completada." << endl;
        return true;
    }
    return false;
}

// ------------------- Extraer ID de mensaje de enlace -------------------
int64_t extract_message_id(const std::string& url) {
    size_t last_slash = url.find_last_of('/');
    if (last_slash == std::string::npos) return -1;
    std::string id_str = url.substr(last_slash + 1);
    return std::stoll(id_str);
}

// ------------------- Resolver enlace -------------------
void request_file_from_link(void* client, const string& link) {
    json req = {
        {"@type", "getMessageLinkInfo"},
        {"url", link}
    };
    td_json_client_send(client, req.dump().c_str());
}

void request_download(void* client, int file_id) {
    json download_req = {
        {"@type", "downloadFile"},
        {"file_id", file_id},
        {"priority", 1},
        {"offset", 0},
        {"limit", 0},
        {"synchronous", false}
    };
    td_json_client_send(client, download_req.dump().c_str());
}

void request_range_download(void* client, int64_t chat_id, int64_t start_id, int64_t end_id) {
    int64_t current_id = end_id;
    int batch_size = 10;

    while (current_id >= start_id) {
        json req = {
            {"@type", "getChatHistory"},
            {"chat_id", chat_id},
            {"from_message_id", current_id},
            {"offset", -batch_size},
            {"limit", batch_size},
            {"only_local", false}
        };
        td_json_client_send(client, req.dump().c_str());

        // Esperar respuesta y procesar mensajes...
        // En cada mensaje, si tiene archivo, llamar a request_download(file_id)

        current_id -= batch_size;
    }
}

// ------------------- Manejo de descargas -------------------
bool handle_file_update(const json& j) {
    auto file = j["file"];

    if (user_download_path.back() != '/')
        user_download_path += '/';

    string local_path = file["local"]["path"];
    string filename = std::filesystem::path(local_path).filename();
    string final_path = user_download_path + filename;

    int64_t downloaded = file["local"]["downloaded_size"];
    int64_t total = file["expected_size"];

    if (total > 0 && !std::filesystem::exists(final_path)) {
        double progress = (double)downloaded / total * 100.0;

        // Dibujar barra de progreso
        int width = 50; // ancho de la barra
        int pos = (int)(progress * width / 100.0);

        cout << "\r[";
        for (int i = 0; i < width; ++i) {
            if (i < pos) cout << "=";
            else if (i == pos) cout << ">";
            else cout << " ";
        }
        cout << "] " << (int)progress << " %";
        cout.flush();
    }  

    if (file["local"]["is_downloading_completed"].get<bool>()) {
        // ✅ Verificar si ya está en destino
        if (std::filesystem::exists(final_path)) {
            //cout << "\n📁 Archivo ya movido: " << final_path << endl;
            return false;
        }

        // ✅ Verificar si existe en origen
        if (!std::filesystem::exists(local_path)) {
            cerr << "\n⚠️ Archivo no encontrado en origen: " << local_path << endl;
            return false;
        }

        // ✅ Mover el archivo
        try {
            if (std::filesystem::exists(final_path)) {
                std::filesystem::remove(final_path);
            }
            std::filesystem::rename(local_path, final_path);
            cout << "\n✅ Archivo movido a: " << final_path << endl;

            return true;
        } catch (std::exception& e) {
            cerr << "\n❌ Error moviendo archivo: " << e.what() << endl;
        }
    }
    return false;
}

// ------------------- Manejo de descargas -------------------
void request_next_in_list(void* client, const vector<string>& links, int& actual, int total) {
    if (actual < total) {
        cout << "\n📥 Descargando mensaje " << actual + 1 << " de " << total << ": " << links[actual] << endl;
        request_file_from_link(client, links[actual]);
        actual++;
    }
    else {
        cout << "\n🎉 Todas las descargas completadas." << endl;
    }
}