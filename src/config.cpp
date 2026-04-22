#include "../include/config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>

using namespace std;
using json = nlohmann::json;

// Definición real de las variables
int api_id = 0;
string api_hash = "";
string download_path = "";
string user_download_path = "";

bool cargar_configuracion() {
    ifstream config_file("config/config.json");
    if (!config_file.is_open()) {
        cerr << "❌ Error: No se pudo abrir config/config.json. Asegúrate de que existe." << endl;
        return false;
    }
    
    json config;
    try {
        config_file >> config;
        api_id = config["api_id"];
        api_hash = config["api_hash"];
        download_path = config["download_path"];
        user_download_path = download_path; // Asignamos la predeterminada de inicio
        cout << "✅ Configuración cargada correctamente." << endl;
        return true;
    } catch (const exception& e) {
        cerr << "❌ Error leyendo el JSON: " << e.what() << endl;
        return false;
    }
}