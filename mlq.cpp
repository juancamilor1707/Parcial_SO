#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <climits>
#include <ctime>
#include <filesystem>

using namespace std;
namespace fs = filesystem;

// Constantes
constexpr int NUM_COLAS = 3;
constexpr int HEADER_SIZE = 80;

// Politica de una cola: Round Robin, Shortest Job First o Prioridad.
enum Politica { RR, SJF, PRIORIDAD };

// Crear carpeta de salida
bool crearCarpetaSalida() {
    error_code ec;
    if (!fs::create_directories("salida", ec) && ec) {
        cerr << "No se pudo crear la carpeta 'salida': " << ec.message() << "\n";
        return false;
    }
    return true;
}

// Generar nombre único con timestamp
string generarNombreSalida(const string& base) {
    auto now = time(nullptr);
    auto tm = localtime(&now);
    char buffer[64];
    strftime(buffer, sizeof(buffer), "salida/%Y%m%d_%H%M%S", tm);
    return string(buffer) + ".txt";
}

// Clase Proceso: guarda datos y métricas
class Proceso {
public:
    string etiqueta;
    int BT, AT, Q, Pr;      // ráfaga, llegada, cola, prioridad
    int restante;           // tiempo CPU faltante
    int WT, CT, RT, TAT;    // métricas: espera, completación, respuesta, tiempo total
    bool iniciado;

    Proceso(const string& e, int bt, int at, int q, int pr)
        : etiqueta(e), BT(bt), AT(at), Q(q), Pr(pr),
          restante(bt), WT(0), CT(0), RT(0), TAT(0), iniciado(false) {}
};

// Planificador MLQ
class PlanificadorMLQ {
    vector<Proceso> procesos;
    Politica politica[NUM_COLAS];
    int quantum[NUM_COLAS];

public:
    PlanificadorMLQ(int esquema) {
        if (esquema == 1) {
            politica[0] = RR;  quantum[0] = 1;
            politica[1] = RR;  quantum[1] = 3;
            politica[2] = SJF; quantum[2] = 0;
        } else {
            politica[0] = RR;        quantum[0] = 3;
            politica[1] = RR;        quantum[1] = 5;
            politica[2] = PRIORIDAD; quantum[2] = 0;
        }
    }

    bool cargar(const string& ruta) {
        ifstream in(ruta);
        if (!in) {
            cerr << "No se pudo abrir: " << ruta << "\n";
            return false;
        }

        string linea;
        while (getline(in, linea)) {
            // Eliminar espacios y caracteres especiales
            linea.erase(remove_if(linea.begin(), linea.end(), 
                        [](char c) { return c == ' ' || c == '\r'; }), linea.end());
            
            // Ignorar comentarios y líneas vacías
            if (linea.empty() || linea[0] == '#') continue;

            // Parsear línea
            stringstream ss(linea);
            string et, bt, at, q, pr;
            if (getline(ss, et, ';') && getline(ss, bt, ';') &&
                getline(ss, at, ';') && getline(ss, q, ';') &&
                getline(ss, pr, ';')) {
                procesos.emplace_back(et, stoi(bt), stoi(at), stoi(q), stoi(pr));
            }
        }
        return true;
    }

    void encolarLlegados(int t, vector<vector<int>>& colas, vector<bool>& encolados) {
        for (size_t i = 0; i < procesos.size(); i++) {
            if (!encolados[i] && procesos[i].restante > 0 && procesos[i].AT <= t) {
                colas[procesos[i].Q - 1].push_back(i);
                encolados[i] = true;
            }
        }
    }

    int elegirProceso(const vector<int>& cola, Politica pol) {
        if (pol == RR) return 0;  // FIFO para Round Robin

        int mejor = 0;
        if (pol == SJF) {
            for (size_t i = 1; i < cola.size(); i++)
                if (procesos[cola[i]].restante < procesos[cola[mejor]].restante)
                    mejor = i;
        } else { // PRIORIDAD
            for (size_t i = 1; i < cola.size(); i++)
                if (procesos[cola[i]].Pr < procesos[cola[mejor]].Pr)
                    mejor = i;
        }
        return mejor;
    }

    void ejecutar() {
        int tiempoActual = 0, terminados = 0;
        size_t n = procesos.size();
        vector<vector<int>> colas(NUM_COLAS);
        vector<bool> encolados(n, false);

        while (terminados < (int)n) {
            encolarLlegados(tiempoActual, colas, encolados);

            // Buscar cola con mayor prioridad que tenga procesos
            int colaActual = -1;
            for (int q = 0; q < NUM_COLAS; q++) {
                if (!colas[q].empty()) {
                    colaActual = q;
                    break;
                }
            }

            // Si no hay procesos listos, saltar al siguiente
            if (colaActual == -1) {
                int prox = INT_MAX;
                for (size_t i = 0; i < n; i++)
                    if (procesos[i].restante > 0)
                        prox = min(prox, procesos[i].AT);
                tiempoActual = prox;
                continue;
            }

            // Ejecutar proceso
            Politica pol = politica[colaActual];
            int qtm = quantum[colaActual];
            int pos = elegirProceso(colas[colaActual], pol);
            int idx = colas[colaActual][pos];
            Proceso& p = procesos[idx];
            colas[colaActual].erase(colas[colaActual].begin() + pos);

            // Primera ejecución -> Response Time
            if (!p.iniciado) {
                p.iniciado = true;
                p.RT = tiempoActual - p.AT;
            }

            // Ejecutar
            int ejecucion = (pol == RR) ? min(qtm, p.restante) : p.restante;
            tiempoActual += ejecucion;
            p.restante -= ejecucion;

            // Enfilar llegados durante la ejecución
            encolarLlegados(tiempoActual, colas, encolados);

            if (p.restante == 0) {
                p.CT = tiempoActual;
                p.TAT = p.CT - p.AT;
                p.WT = p.TAT - p.BT;
                terminados++;
            } else if (pol == RR) {
                colas[colaActual].push_back(idx);
            }
        }
    }

    void reportar(const string& salida) {
        ofstream out(salida);
        if (!out) {
            cerr << "No se pudo escribir: " << salida << "\n";
            return;
        }

        double sWT = 0, sCT = 0, sRT = 0, sTAT = 0;
        int n = procesos.size();

        string cabecera = "# etiqueta; BT; AT; Q; Pr; WT; CT; RT; TAT";
        out << cabecera << "\n";
        cout << cabecera << "\n";

        for (auto& p : procesos) {
            out << p.etiqueta << ";" << p.BT << ";" << p.AT << ";" << p.Q << ";"
                << p.Pr << ";" << p.WT << ";" << p.CT << ";" << p.RT << ";" << p.TAT << "\n";
            cout << p.etiqueta << ";" << p.BT << ";" << p.AT << ";" << p.Q << ";"
                 << p.Pr << ";" << p.WT << ";" << p.CT << ";" << p.RT << ";" << p.TAT << "\n";
            sWT += p.WT; sCT += p.CT; sRT += p.RT; sTAT += p.TAT;
        }

        char promedio[128];
        snprintf(promedio, sizeof(promedio), "WT=%.2f; CT=%.2f; RT=%.2f; TAT=%.2f",
                 sWT / n, sCT / n, sRT / n, sTAT / n);
        out << promedio << "\n";
        cout << promedio << "\n";
        cout << "\nSalida guardada en: " << salida << "\n";
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Uso: " << argv[0] << " <entrada.txt> <esquema:1|2> [salida.txt]\n";
        return 1;
    }

    if (!crearCarpetaSalida()) return 1;

    string entrada = argv[1];
    int esquema = stoi(argv[2]);
    string salida = (argc >= 4) ? 
                    string("salida/") + argv[3] : 
                    generarNombreSalida("resultado");

    PlanificadorMLQ plan(esquema);
    if (!plan.cargar(entrada)) return 1;
    plan.ejecutar();
    plan.reportar(salida);
    return 0;
}