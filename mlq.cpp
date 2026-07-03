#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <climits>

using namespace std;

// Politica de una cola: Round Robin, Shortest Job First o Prioridad.
enum Politica { RR, SJF, PRIORIDAD };

// ------------------------------------------------------------
//  Clase Proceso: guarda los datos de entrada y las metricas.
// ------------------------------------------------------------
class Proceso {
public:
    string etiqueta;
    int BT, AT, Q, Pr;      // rafaga, llegada, cola, prioridad
    int restante;           // tiempo de CPU que le falta
    int WT, CT, RT, TAT;    // metricas de salida
    bool iniciado;          // ya tuvo CPU al menos una vez?

    Proceso(string e, int bt, int at, int q, int pr)
        : etiqueta(e), BT(bt), AT(at), Q(q), Pr(pr),
          restante(bt), WT(0), CT(0), RT(0), TAT(0), iniciado(false) {}
};

// ------------------------------------------------------------
//  Clase PlanificadorMLQ: contiene los procesos y ejecuta el
//  algoritmo de colas multinivel para el esquema elegido.
// ------------------------------------------------------------
class PlanificadorMLQ {
    vector<Proceso> procesos;
    Politica politica[3];   // politica de cada cola (Q1,Q2,Q3)
    int quantum[3];         // quantum de cada cola (0 = no aplica)

public:
    // Configura las 3 colas segun el numero de esquema (1 o 2).
    PlanificadorMLQ(int esquema) {
        if (esquema == 1) {
            politica[0] = RR;  quantum[0] = 1;
            politica[1] = RR;  quantum[1] = 3;
            politica[2] = SJF; quantum[2] = 0;
        } else { // esquema 2
            politica[0] = RR;        quantum[0] = 3;
            politica[1] = RR;        quantum[1] = 5;
            politica[2] = PRIORIDAD; quantum[2] = 0;
        }
    }

    // Lee el archivo de entrada (ignora comentarios y lineas vacias).
    bool cargar(const string& ruta) {
        ifstream in(ruta);
        if (!in) { cerr << "No se pudo abrir: " << ruta << "\n"; return false; }
        string linea;
        while (getline(in, linea)) {
            // quita espacios y saltos raros
            linea.erase(remove(linea.begin(), linea.end(), ' '), linea.end());
            linea.erase(remove(linea.begin(), linea.end(), '\r'), linea.end());
            if (linea.empty() || linea[0] == '#') continue;

            stringstream ss(linea);
            string et, bt, at, q, pr;
            getline(ss, et, ';'); getline(ss, bt, ';');
            getline(ss, at, ';'); getline(ss, q,  ';');
            getline(ss, pr, ';');
            procesos.emplace_back(et, stoi(bt), stoi(at), stoi(q), stoi(pr));
        }
        return true;
    }

    // Mete a la cola FIFO 'cola' los procesos que ya llegaron (AT<=t),
    // aun no estan encolados y les falta CPU. Respeta orden de llegada.
    void encolarLlegados(int t, vector<vector<int>>& cola, vector<bool>& enq) {
        int n = procesos.size();
        for (int i = 0; i < n; i++)
            if (!enq[i] && procesos[i].restante > 0 && procesos[i].AT <= t) {
                cola[procesos[i].Q - 1].push_back(i);
                enq[i] = true;
            }
    }

    // Dentro de una cola no-RR (SJF o Prioridad) escoge la posicion del
    // mejor proceso; en RR simplemente se atiende el frente (posicion 0).
    int posicionElegida(const vector<int>& cola, Politica pol) {
        int mejor = 0;
        for (int k = 1; k < (int)cola.size(); k++) {
            if (pol == SJF && procesos[cola[k]].restante < procesos[cola[mejor]].restante)
                mejor = k;
            else if (pol == PRIORIDAD && procesos[cola[k]].Pr < procesos[cola[mejor]].Pr)
                mejor = k; // menor numero = mayor prioridad
        }
        return mejor;
    }

    // Ejecuta la simulacion completa.
    void ejecutar() {
        int tiempoActual = 0, terminados = 0, n = procesos.size();
        vector<vector<int>> cola(3);      // cola FIFO de indices por nivel
        vector<bool> enq(n, false);       // ya fue encolado?

        while (terminados < n) {
            encolarLlegados(tiempoActual, cola, enq);

            // Cola de mayor prioridad (Q1>Q2>Q3) que tenga procesos.
            int colaActual = -1;
            for (int q = 0; q < 3; q++)
                    if (!cola[q].empty()) { colaActual = q; break; }

            // Nadie listo: avanza el tiempo a la proxima llegada.
            if (colaActual == -1) {
                int prox = INT_MAX;
                for (int i = 0; i < n; i++)
                    if (procesos[i].restante > 0) prox = min(prox, procesos[i].AT);
                tiempoActual = prox;
                continue;
            }

            Politica pol = politica[colaActual];
            int qtm      = quantum[colaActual];
            int pos      = (pol == RR) ? 0 : posicionElegida(cola[colaActual], pol);
            int indice     = cola[colaActual][pos];
            Proceso& p   = procesos[indice];
            cola[colaActual].erase(cola[colaActual].begin() + pos); // sale de la cola

            // Primer uso de CPU -> Response Time.
            if (!p.iniciado) { p.iniciado = true; p.RT = tiempoActual - p.AT; }

            // RR corre un quantum; SJF/Prioridad corren hasta terminar.
            int ejec = (pol == RR) ? min(qtm, p.restante) : p.restante;
            tiempoActual += ejec;
            p.restante -= ejec;

            // Encola los que llegaron durante este turno (antes de rotar).
            encolarLlegados(tiempoActual, cola, enq);

            if (p.restante == 0) {                 // termino
                p.CT  = tiempoActual;                // tiempo de completacion
                p.TAT = p.CT - p.AT;
                p.WT  = p.TAT - p.BT;
                terminados++;
            } else if (pol == RR) {                // quantum agotado: rota al final
                cola[colaActual].push_back(indice);    // vuelve a la cola
            }
        }
    }

    // Escribe resultados en archivo y los muestra en pantalla.
    void reportar(const string& salida) {
        double sWT = 0, sCT = 0, sRT = 0, sTAT = 0;
        int n = procesos.size();

        ofstream out(salida);
        string cab = "# etiqueta; BT; AT; Q; Pr; WT; CT; RT; TAT";
        out << cab << "\n";
        cout << cab << "\n";

        for (auto& p : procesos) {
            stringstream ss;
            ss << p.etiqueta << ";" << p.BT << ";" << p.AT << ";" << p.Q << ";"
               << p.Pr << ";" << p.WT << ";" << p.CT << ";" << p.RT << ";" << p.TAT;
            out << ss.str() << "\n";
            cout << ss.str() << "\n";
            sWT += p.WT; sCT += p.CT; sRT += p.RT; sTAT += p.TAT;
        }

        stringstream prom;
        prom.setf(ios::fixed); prom.precision(2);
        prom << "WT=" << sWT / n << "; CT=" << sCT / n
             << "; RT=" << sRT / n << "; TAT=" << sTAT / n;
        out << prom.str() << "\n";
        cout << prom.str() << "\n";
        cout << "\nSalida guardada en: " << salida << "\n";
    }
};

int main(int argc, char* argv[]) {
    if (argc < 3) {
        cerr << "Uso: " << argv[0] << " <entrada.txt> <esquema:1|2> [salida.txt]\n";
        return 1;
    }
    string entrada = argv[1];
    int esquema    = stoi(argv[2]);
    string salida  = (argc >= 4) ? argv[3] : "salida.txt";

    PlanificadorMLQ plan(esquema);
    if (!plan.cargar(entrada)) return 1;
    plan.ejecutar();
    plan.reportar(salida);
    return 0;
}