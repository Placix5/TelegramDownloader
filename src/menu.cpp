#include <iostream>
#include <cstdlib> 
#include "../include/downloader.hpp"
#include "../include/renamer.hpp"
#include "../include/extractor.hpp"

using namespace std;

void mostrar_menu() {
    int opcion;
    
    do {
        // Limpiamos la pantalla para que el menú siempre se vea impecable
        system("clear"); 
        
        cout << "\n======================================";
        cout << "\n  🎬 TELEGRAM MEDIA DOWNLOADER v2.0";
        cout << "\n======================================";
        cout << "\n1. Descargar usando URL de mensajes";
        cout << "\n2. Descargar usando un archivo de texto";
        cout << "\n3. Renombrado masivo de series";
        cout << "\n4. Descompresión de archivos por lotes (Buffer)";
        cout << "\n5. Salir";
        cout << "\n======================================";
        cout << "\nElige una opción: ";
        
        // Control de errores por si el usuario escribe una letra en vez de un número
        if (!(cin >> opcion)) {
            cin.clear();
            cin.ignore(256, '\n');
            continue;
        }

        switch (opcion) {
            case 1:
                opcion_descarga_rango();
                break;
            case 2:
                // ¡Aquí conectamos la nueva función automática!
                opcion_descarga_archivo(); 
                break;
            case 3:
                renombrado_masivo();
                break;
            case 4:
                descompresion_lotes();
                break;
            case 5:
                cout << "\n👋 ¡Cerrando sesión y apagando TDLib! Hasta pronto.\n";
                break;
            default:
                cout << "\n❌ Opción no válida. Introduce un número del 1 al 5.\n";
                cout << "Presiona Intro para volver...";
                cin.ignore(); cin.get();
                break;
        }
    } while (opcion != 5);
}