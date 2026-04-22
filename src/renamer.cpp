#include "../include/renamer.hpp"
#include <iostream>
#include <filesystem>
#include <regex>
#include <vector>
#include <algorithm>
#include <iomanip>

using namespace std;

void renombrado_masivo() {
    string ruta_serie;
    cout << "\n📂 Introduce la ruta de la serie (ej. /mnt/jellyfin/Series/Slow Horses): ";
    cin.ignore();
    getline(cin, ruta_serie);

    if (!ruta_serie.empty() && ruta_serie.back() == '/') ruta_serie.pop_back();

    if (!filesystem::exists(ruta_serie) || !filesystem::is_directory(ruta_serie)) {
        cout << "❌ La ruta no es válida o no es un directorio.\nPresiona Intro para volver al menú...";
        cin.get(); return;
    }

    filesystem::path path_serie(ruta_serie);
    string nombre_serie = path_serie.filename().string();
    cout << "\n🎬 Serie detectada: " << nombre_serie << "\n";

    regex regex_temporada(R"(Temporada\s+(\d+))", regex_constants::icase);
    int carpetas_procesadas = 0;

    for (const auto& entry_carpeta : filesystem::directory_iterator(path_serie)) {
        if (!entry_carpeta.is_directory()) continue;
        string nombre_carpeta = entry_carpeta.path().filename().string();
        int num_temporada = -1;
        
        string carpeta_lower = nombre_carpeta;
        transform(carpeta_lower.begin(), carpeta_lower.end(), carpeta_lower.begin(), ::tolower);

        if (carpeta_lower == "specials" || carpeta_lower == "especiales") {
            num_temporada = 0;
        } else {
            smatch match;
            if (regex_search(nombre_carpeta, match, regex_temporada)) num_temporada = stoi(match[1].str());
        }

        if (num_temporada == -1) continue; 

        cout << "\n📁 Procesando: " << nombre_carpeta << "...\n";
        carpetas_procesadas++;

        vector<filesystem::path> episodios;
        for (const auto& entry_ep : filesystem::directory_iterator(entry_carpeta.path())) {
            if (entry_ep.is_regular_file()) episodios.push_back(entry_ep.path());
        }
        sort(episodios.begin(), episodios.end());

        int contador = 1;
        for (const auto& ruta_episodio : episodios) {
            string extension = ruta_episodio.extension().string();
            stringstream ss;
            ss << nombre_serie << " - " << num_temporada << "x" << setfill('0') << setw(2) << contador << extension;
            filesystem::path ruta_nueva = entry_carpeta.path() / ss.str();

            if (ruta_episodio != ruta_nueva) {
                try {
                    filesystem::rename(ruta_episodio, ruta_nueva);
                    cout << "   ✅ Renombrado: " << ruta_episodio.filename().string() << " -> " << ss.str() << "\n";
                } catch (...) {
                    cout << "   ❌ Sin permisos o error para renombrar: " << ruta_episodio.filename().string() << "\n";
                }
            }
            contador++;
        }
    }

    if (carpetas_procesadas == 0) cout << "\n⚠️ No se encontraron carpetas con el formato 'Temporada X' o 'Specials'.\n";
    else cout << "\n🎉 ¡Renombrado masivo completado!\n";
    
    cout << "Presiona Intro para volver al menú..."; cin.get();
}