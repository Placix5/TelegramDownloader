#pragma once
#include <string>
#include <vector>

void limpiar_basura_nombres(const std::string& ruta);
bool extraer_con_progreso(const std::string& archivo_origen, const std::string& carpeta_destino);
std::vector<std::string> separar_linea(const std::string& str, char delim);
void mover_archivo(const std::string& origen, const std::string& destino);