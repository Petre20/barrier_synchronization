/*
    Barrier Synchronization - Ridicarea la putere a unei matrici (varianta naiva)
    Implementare folosind Boost! Threads

    Compilare:
        g++ -std=c++17 -O2 matrix_multiplication.cpp -o matrix_multiplication -lboost_thread -lboost_system -lboost_chrono -lpthread
    
    Rulare:
        ./matrix_multiplication <dimensiune_matrice> <putere> <numar_threaduri>
    
    Exemplu:
        ./matrix_multiplication 4 5 2 -> M^5 cu matrice 4x4, 2 threaduri
 */

#include <boost/thread/barrier.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>

#include <iostream>
#include <vector>
#include <iomanip>
#include <chrono>
#include <cstdlib>

using namespace std;

// Tipuri
using Matrix = vector<vector<double>>;

// Utilitare matrice
Matrix make_zero(int n) {
    return Matrix(n, vector<double>(n, 0.0));
}

void print_matrix(const Matrix& M, const string& label) {
    cout << label << ":\n";
    int n = (int)M.size();
    for (int i = 0; i < n; ++i) {
        cout << "  [";
        for (int j = 0; j < n; ++j)
            cout << setw(12) << fixed << setprecision(2) << M[i][j];
        cout << " ]\n";
    }
    cout << "\n";
}

// Structura de date partajata intre thread-uri
// La fiecare pas calculam: result = current * original
// Dupa pas facem swap(current, result) prin swap de pointeri (fara copiere).
// original ramane neschimbat pe toata durata calculului.
struct SharedData {
    int n;           // dimensiunea matricii
    int num_threads;
    int power;       // puterea la care ridicam

    const Matrix* original;  // M (matricea initiala, doar citire)
    Matrix*       current;   // M^k (rezultatul pasului curent)
    Matrix*       result;    // buffer pentru M^(k+1)

    boost::barrier* bar;
    boost::mutex    cout_mtx;
};

// Functia executata de fiecare thread
void thread_func(SharedData* sd, int tid) {
    int n           = sd->n;
    int num_threads = sd->num_threads;

    // Partitionare linii: thread-ul tid se ocupa de [row_start, row_end)
    int chunk     = (n + num_threads - 1) / num_threads;
    int row_start = tid * chunk;
    int row_end   = min(row_start + chunk, n);

    {
        boost::mutex::scoped_lock lk(sd->cout_mtx);
        cout << "[Thread " << tid << "] Linii responsabile: ["
             << row_start << ", " << row_end - 1 << "]\n";
    }

    // p-1 pasi: la fiecare pas calculam result = current * original
    for (int step = 1; step < sd->power; ++step) {

        // CALCUL: result[i][j] = sum_k current[i][k] * original[k][j]
        // Fiecare thread scrie exclusiv in liniile [row_start, row_end)
        for (int i = row_start; i < row_end; ++i) {
            for (int j = 0; j < n; ++j) {
                (*sd->result)[i][j] = 0.0;
                for (int k = 0; k < n; ++k)
                    (*sd->result)[i][j] += (*sd->current)[i][k] * (*sd->original)[k][j];
            }
        }

        {
            boost::mutex::scoped_lock lk(sd->cout_mtx);
            cout << "[Thread " << tid << "] Pasul " << step << "/" << sd->power - 1 << ": calcul terminat. Astept bariera 1...\n";
        }

        // BARIERA 1: toti au terminat de scris in result.
        // Thread-ul serial face swap(current, result).
        bool is_serial = sd->bar->wait();

        if (is_serial) {
            swap(sd->current, sd->result);

            boost::mutex::scoped_lock lk(sd->cout_mtx);
            cout << "\n>>> Pasul " << step << ": swap efectuat." << " current = M^" << step + 1 << "\n\n";
        }

        // BARIERA 2: toti vad noii pointeri inainte de urmatorul pas.
        sd->bar->wait();
    }

    {
        boost::mutex::scoped_lock lk(sd->cout_mtx);
        cout << "[Thread " << tid << "] Toate pasurile finalizate.\n";
    }
}

// Verificare secventiala
static Matrix mat_mul(const Matrix& A, const Matrix& B) {
    int n = (int)A.size();
    Matrix C = make_zero(n);
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < n; ++k)
            for (int j = 0; j < n; ++j)
                C[i][j] += A[i][k] * B[k][j];
    return C;
}

static Matrix sequential_matrix_power(Matrix current, const Matrix& M, int power) {
    for (int step = 1; step < power; ++step)
        current = mat_mul(current, M);
    return current;
}

static bool matrices_equal(const Matrix& A, const Matrix& B, double eps = 1e-3) {
    int n = (int)A.size();
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (abs(A[i][j] - B[i][j]) > eps) return false;
    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 4) {
        cerr << "Utilizare: " << argv[0] << " <dimensiune_matrice> <putere> <numar_threaduri>\n" << "Exemplu:  " << argv[0] << " 4 5 2\n";
        return EXIT_FAILURE;
    }

    int n = atoi(argv[1]);
    int power = atoi(argv[2]);
    int num_threads = atoi(argv[3]);

    if (n <= 0 || power <= 0 || num_threads <= 0) {
        cerr << "Eroare: toti parametrii trebuie sa fie > 0.\n";
        return EXIT_FAILURE;
    }
    if (num_threads > n) {
        cerr << "Atentie: num_threads redus la " << n << " (= dimensiunea matricii).\n";
        num_threads = n;
    }

    cout << "INCEPUT: M^" << power  << ", matrice " << n << "x" << n << ", " << num_threads << " thread-uri" << ", " << power - 1 << " pasi\n\n";

    // Initializam M cu valori simple: M[i][j] = i + j + 1
    Matrix M = make_zero(n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            M[i][j] = (double)(i + j + 1);

    print_matrix(M, "Matricea initiala M");

    // Cele doua buffere
    Matrix current = M;            // M^k, initial M^1
    Matrix result  = make_zero(n); // buffer pentru M^(k+1)

    boost::barrier bar(num_threads);

    SharedData sd;
    sd.n = n;
    sd.num_threads = num_threads;
    sd.power = power;
    sd.original = &M;
    sd.current = &current;
    sd.result = &result;
    sd.bar = &bar;

    auto t_start = chrono::steady_clock::now();

    boost::thread_group tg;
    for (int i = 0; i < num_threads; ++i)
        tg.create_thread([&sd, i]() { thread_func(&sd, i); });

    tg.join_all();

    auto t_end = chrono::steady_clock::now();
    double elapsed_ms = chrono::duration<double, milli>(t_end - t_start).count();

    cout << "\nTimp executie paralela: " << fixed << setprecision(2) << elapsed_ms << " ms\n\n";

    // Rezultatul este in *sd.current (dupa ultimul swap)
    print_matrix(*sd.current, "Rezultat paralel M^" + to_string(power));

    // Validare
    Matrix ref = sequential_matrix_power(M, M, power);
    print_matrix(ref, "Referinta secventiala M^" + to_string(power));

    if (matrices_equal(*sd.current, ref))
        cout << "VALIDARE OK: rezultatul paralel == referinta secventiala\n";
    else
        cout << "EROARE: diferente intre rezultatul paralel si referinta!\n";

    return EXIT_SUCCESS;
}