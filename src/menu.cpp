#include "../include/menu.hpp"
#include <iostream>
#include "../include/downloader.hpp"

using namespace std;

void mostrar_menu() {
    int opcion = 0;
    
    while (opcion != 5) {
        cout << "\n======================================" << endl;
        cout << "  🎬 TELEGRAM MEDIA DOWNLOADER v2.0" << endl;
        cout << "======================================" << endl;
        cout << "1. Descargar usando URL de mensajes" << endl;
        cout << "2. Descargar usando un archivo de texto" << endl;
        cout << "3. Renombrado masivo de series" << endl;
        cout << "4. Descompresión de archivos por lotes (Buffer)" << endl;
        cout << "5. Salir" << endl;
        cout << "======================================" << endl;
        cout << "Elige una opción: ";
        
        cin >> opcion;

        // Limpiar la pantalla (opcional, queda más bonito)
        // system("clear"); 

        switch (opcion) {
            case 1: 
                opcion_descarga_rango();
                break;
            case 2: 
                cout << "\n[Leyendo archivo txt... (Próximamente)]" << endl;
                break;
            case 3: 
                cout << "\n[Iniciando renombrado... (Próximamente)]" << endl;
                break;
            case 4: 
                cout << "\n[Descomprimiendo Buffer... (Próximamente)]" << endl;
                break;
            case 5: 
                cout << "\n👋 ¡Hasta pronto! Cerrando el programa..." << endl;
                break;
            default: 
                cout << "\n❌ Opción no válida. Inténtalo de nuevo." << endl;
        }
    }
}