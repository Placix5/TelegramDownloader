#include "../include/extractor.hpp"
#include "../include/utils.hpp"
#include "../include/config.hpp" 
#include <iostream>
#include <filesystem>
#include <vector>
#include <string>
#include <regex>

using namespace std;

void descompresion_lotes() {
    string ruta_origen, ruta_destino;

    cout << "\n📂 Introduce la ruta ORIGEN donde están los comprimidos (ej. " << download_path << "): ";
    cin.ignore();
    getline(cin, ruta_origen);
    if (ruta_origen.empty()) ruta_origen = download_path;
    if (ruta_origen.back() != '/') ruta_origen += '/';

    if (!filesystem::exists(ruta_origen) || !filesystem::is_directory(ruta_origen)) {
        cout << "❌ La ruta de origen no es válida.\nPresiona Intro para volver...";
        cin.get(); return;
    }

    cout << "📂 Introduce la ruta DESTINO para los extraídos (deja vacío para extraer aquí mismo): ";
    getline(cin, ruta_destino);
    if (ruta_destino.empty()) ruta_destino = ruta_origen;
    if (ruta_destino.back() != '/') ruta_destino += '/';

    if (!filesystem::exists(ruta_destino)) {
        cout << "¿Desea crear el directorio de destino? (s/n): ";
        char conf; cin >> conf; cin.ignore(256, '\n');
        if (conf == 's' || conf == 'S') filesystem::create_directories(ruta_destino);
        else { cout << "❌ Operación cancelada.\n"; return; }
    }

    vector<string> principales;
    vector<string> todos_comprimidos;

    // Escaneamos la carpeta en busca de comprimidos
    for (const auto& entry : filesystem::directory_iterator(ruta_origen)) {
        if (!entry.is_regular_file()) continue;
        string f = entry.path().filename().string();
        string path_str = entry.path().string();

        bool es_comp = (f.find(".zip") != string::npos || f.find(".rar") != string::npos || 
                        f.find(".7z") != string::npos || regex_search(f, regex(R"(\.\d{3}$)")));

        if (es_comp) {
            todos_comprimidos.push_back(path_str);
            // Filtramos solo los archivos "Maestros" para pasárselos a 7z
            if (f.find(".001") != string::npos || f.find(".part1.rar") != string::npos || f.find(".part01.rar") != string::npos) {
                principales.push_back(path_str);
            } else if ((f.find(".rar") != string::npos && f.find(".part") == string::npos) || 
                       (f.find(".zip") != string::npos && f.find(".zip.") == string::npos) ||
                       (f.find(".7z") != string::npos && f.find(".7z.") == string::npos)) {
                principales.push_back(path_str);
            }
        }
    }

    if (principales.empty()) {
        cout << "❌ No se encontraron archivos comprimidos válidos en: " << ruta_origen << "\n";
        cout << "Presiona Intro para volver...";
        cin.get(); return;
    }

    cout << "\n📦 Se han encontrado " << principales.size() << " paquetes principales para extraer.\n";
    
    cout << "¿Desea aplanar las carpetas (sacar todo a la raíz del destino, ideal para Series)? (s/n): ";
    char conf_aplanar; cin >> conf_aplanar; 
    bool aplanar = (conf_aplanar == 's' || conf_aplanar == 'S');

    cout << "¿Desea borrar los originales comprimidos tras extraer con éxito? (s/n): ";
    char conf_borrar; cin >> conf_borrar; cin.ignore(256, '\n');
    bool borrar = (conf_borrar == 's' || conf_borrar == 'S');

    string temp_dir = ruta_destino + "cuarentena_7z/";
    filesystem::create_directories(temp_dir);
    bool algun_error = false;

    // Extraer todo a la sala de cuarentena
    for (size_t i = 0; i < principales.size(); i++) {
        cout << "\n📦 Descomprimiendo paquete (" << (i+1) << "/" << principales.size() << "): " << filesystem::path(principales[i]).filename().string() << "\n";
        if (!extraer_con_progreso(principales[i], temp_dir)) algun_error = true;
    }

    cout << "\n✅ FASE DE DESCOMPRESIÓN FINALIZADA.\n";
    cout << "🧹 Limpiando basura de los nombres...\n";
    limpiar_basura_nombres(temp_dir);

    cout << "🚚 Moviendo archivos al Destino final...\n";
    if (aplanar) {
        for (const auto& entry : filesystem::recursive_directory_iterator(temp_dir)) {
            if (entry.is_regular_file()) {
                string nombre = entry.path().filename().string();
                mover_archivo(entry.path().string(), ruta_destino + nombre);
            }
        }
    } else {
        for (const auto& entry : filesystem::directory_iterator(temp_dir)) {
            string nombre = entry.path().filename().string();
            mover_archivo(entry.path().string(), ruta_destino + nombre);
        }
    }

    filesystem::remove_all(temp_dir);

    if (borrar && !algun_error) {
        cout << "🗑️ Borrando archivos comprimidos originales...\n";
        for (const string& f : todos_comprimidos) {
            filesystem::remove(f);
        }
    } else if (algun_error) {
        cout << "⚠️ Hubo errores en 7z. No se borrarán los originales por seguridad.\n";
    }

    cout << "\n🎉 ¡Descompresión por lotes completada!\n";
    cout << "Presiona Intro para volver al menú...";
    cin.get();
}