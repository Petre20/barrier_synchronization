//Barrier synchronization problem - o implementare generica folosind Boost! Threads
//Echivalent al variantei POSIX cu pthread_barrier_t

//compilare: g++ -std=c++17 -O2 barrier_boost.cpp -o barrier_boost -lboost_thread -lboost_system -lboost_chrono -lpthread
//ex rulare: ./barrier_boost 3 2

#include <boost/thread/barrier.hpp>
#include <boost/thread/thread.hpp>
#include <boost/thread/mutex.hpp>
#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <chrono>

using namespace std;

// Echivalente Boost vs POSIX:
//
//   pthread_barrier_t bar;               ->  boost::barrier bar(n);
//   pthread_barrier_init(&bar, NULL, n)  ->  (in constructor)
//   pthread_barrier_wait(&bar)           ->  bar.wait()     (returneaza bool)
//   PTHREAD_BARRIER_SERIAL_THREAD        ->  true (valoarea returnata de wait)
//   pthread_barrier_destroy(&bar)        ->  (in destructor, automat)
//   pthread_t / pthread_create           ->  boost::thread_group / create_thread
//   pthread_join                         ->  tg.join_all()
//   unsigned int seed + rand_r(&seed)    ->  boost::thread-local seed + rand_r


// Bariera globala - sincronizeaza toate thread-urile la sfarsitul fiecarei faze
// Spre deosebire de POSIX unde e necesara initializarea explicita cu
// pthread_barrier_init, boost::barrier se initializeaza direct in constructor.

boost::barrier* g_barrier   = nullptr;
boost::mutex g_cout_mtx; // mutex pentru output neamestecat

// Structura pentru argumentele fiecarui thread
// (identica cu ThreadArg din varianta POSIX)
struct ThreadArg {
    int id;
    int num_phases;
};

// Returneaza diferenta dintre 2 momente de timp in milisecunde
// Folosim chrono in loc de clock_gettime + struct timespec
static double get_time_diff(chrono::steady_clock::time_point start, chrono::steady_clock::time_point end){
    return chrono::duration<double, milli>(end - start).count();
}

// Simuleaza "munca" unui thread (identic cu varianta POSIX)
static void simulate_work(int id, int phase, int work_ms) {
    {
        boost::mutex::scoped_lock lk(g_cout_mtx);
        cout << "[Thread " << id << "] Faza " << phase<< ": A inceput executia... (" << work_ms << " ms)\n";
        cout.flush();
    }
 
    // nanosleep -> this_thread::sleep_for (portabil in Boost/C++11)
    boost::this_thread::sleep_for(boost::chrono::milliseconds(work_ms));
}


// Sincronizeaza thread-urile la bariera si raporteaza timpul de asteptare
//
// POSIX:  pthread_barrier_wait(&barrier)  returna PTHREAD_BARRIER_SERIAL_THREAD
//         pentru exact un thread, 0 pentru restul.
// Boost:  bar.wait()  returneaza true pentru exact un thread, false pentru restul.
// Comportamentul este identic - folosim rezultatul pentru mesajul global.
static void wait_at_barrier(int id, int phase) {
    {
        boost::mutex::scoped_lock lk(g_cout_mtx);
        cout << "Thread " << id << " Faza " << phase << ": A terminat executia. Asteapta la bariera.\n";
        cout.flush();
    }
    auto wait_start = chrono::steady_clock::now();
 
    // Blocare pana ajung toate thread-urile
    // Returneaza true pentru un singur thread (echivalent PTHREAD_BARRIER_SERIAL_THREAD)
    bool is_serial = g_barrier->wait();
    auto wait_end = chrono::steady_clock::now();
    if (is_serial) {
        boost::mutex::scoped_lock lk(g_cout_mtx);
        cout << "\nBARIERA DESCHISA pentru Faza " << phase<< ". Trecem la urmatoarea.\n\n";
        cout.flush();
    }
    double wait_ms = get_time_diff(wait_start, wait_end);
    {
        boost::mutex::scoped_lock lk(g_cout_mtx);
        cout << "Thread " << id << " Faza " << phase<< ": A trecut bariera. Timp asteptat: " << fixed << wait_ms << " ms\n";
        cout.flush();
    }
}

// Functia executata de fiecare thread
// (identica logic cu thread_work din varianta POSIX)
static void thread_work(ThreadArg arg) {  // arg by value (nu mai e nevoie de free)
    int id         = arg.id;
    int num_phases = arg.num_phases;
    unsigned int seed = static_cast<unsigned int>(time(nullptr)) + id;
    for (int phase = 1; phase <= num_phases; ++phase) {
        int work_ms = (rand_r(&seed) % 4500) + 500;  // 500..5000 ms, identic POSIX
        simulate_work(id, phase, work_ms);
        wait_at_barrier(id, phase);
    }
}

// Valideaza argumentele din linia de comanda
static bool validate_args(int argc, char* argv[], int& num_threads, int& num_phases) {
    if (argc != 3) {
        cerr << "Utilizare: " << argv[0]<< " <numar_threaduri> <numar_etape>\n";
        return false;
    }
    num_threads = atoi(argv[1]);
    num_phases  = atoi(argv[2]);
    if (num_threads <= 0 || num_phases <= 0) {
        cerr << "Eroare: valorile trebuie sa fie > 0.\n";
        return false;
    }
    return true;
}

int main(int argc, char* argv[]) {
    int num_threads, num_phases;
 
    if (!validate_args(argc, argv, num_threads, num_phases))
        return EXIT_FAILURE;
 
    // Initializare bariera Boost
    // POSIX: pthread_barrier_init(&barrier, NULL, num_threads)
    // Boost: constructorul face totul; pastram pe heap pentru a controla
    //        durata de viata (distrugere explicita inainte de return)
    g_barrier = new boost::barrier(num_threads);
 
    cout << "INCEPUT SIMULARE: " << num_threads << " Thread-uri si " << num_phases << " Faze\n\n";
 
    // Cream thread-urile cu boost::thread_group
    // POSIX: array de pthread_t + pthread_create in bucla
    // Boost: thread_group gestioneaza automat colectia de thread-uri
    boost::thread_group tg;
    for (int i = 0; i < num_threads; ++i) {
        ThreadArg arg{ i, num_phases };
        tg.create_thread([arg]() { thread_work(arg); });
    }
 
    // Asteptam terminarea tuturor thread-urilor
    // POSIX: pthread_join in bucla
    // Boost: join_all() face acelasi lucru pentru toate thread-urile din grup
    tg.join_all();
 
    // Distrugere bariera
    // POSIX: pthread_barrier_destroy(&barrier)
    // Boost: delete apeleaza destructorul care face cleanup
    delete g_barrier;
    g_barrier = nullptr;
 
    cout << "SIMULARE FINALIZATA\n";
    return EXIT_SUCCESS;
}