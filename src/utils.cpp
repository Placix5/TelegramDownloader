#include "../include/utils.hpp"
#include <iostream>
#include <filesystem>
#include <regex>
#include <cstdio>
#include <sstream>

using namespace std;

void limpiar_basura_nombres(const string& ruta) {
    regex basuras(R"(\s*\[.*?\])");
    vector<pair<filesystem::path, filesystem::path>> renombrados;

    for (const auto& entry : filesystem::recursive_directory_iterator(ruta)) {
        string filename = entry.path().filename().string();
        string new_filename = regex_replace(filename, basuras, "");
        if (filename != new_filename) {
            renombrados.push_back({entry.path(), entry.path().parent_path() / new_filename});
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
    char buffer[128]; string last_perc = "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        string line(buffer); size_t pos = line.find('%');
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

vector<string> separar_linea(const string& str, char delim) {
    vector<string> tokens; stringstream ss(str); string token;
    while (getline(ss, token, delim)) {
        size_t first = token.find_first_not_of(" \t");
        size_t last = token.find_last_not_of(" \t");
        if (first == string::npos) token = "";
        else token = token.substr(first, (last - first + 1));
        tokens.push_back(token);
    }
    return tokens;
}

void mover_archivo(const string& origen, const string& destino) {
    if (!filesystem::exists(origen)) return;
    try {
        if (filesystem::exists(destino) && filesystem::is_regular_file(destino)) filesystem::remove(destino);
        filesystem::rename(origen, destino); 
    } catch (...) {
        filesystem::copy(origen, destino, filesystem::copy_options::recursive | filesystem::copy_options::overwrite_existing);
        filesystem::remove_all(origen);
    }
}