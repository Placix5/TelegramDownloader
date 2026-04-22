#pragma once
#include <string>

// Variables globales accesibles desde cualquier archivo
extern int api_id;
extern std::string api_hash;
extern std::string download_path;
extern std::string user_download_path;

// Función para cargar el JSON
bool cargar_configuracion();