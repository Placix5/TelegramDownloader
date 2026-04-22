#include "../include/config.hpp"
#include "../include/menu.hpp"
#include "../include/downloader.hpp"
#include <iostream>

int main() {
    if (!cargar_configuracion()) return 1; 

    // Arrancamos Telegram y esperamos a estar autenticados
    if (iniciar_telegram()) {
        mostrar_menu(); // Una vez conectados, abrimos el menú
    }

    // Al salir del menú, cerramos todo limpiamente
    std::cout << "Cerrando servicios..." << std::endl;
    detener_telegram();
    
    return 0;
}