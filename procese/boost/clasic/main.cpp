#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace bip = boost::interprocess;

struct Barrier
{
    bip::interprocess_semaphore mutex{1};     // porneste liber
    bip::interprocess_semaphore entry_sem{0}; // blocat initial
    bip::interprocess_semaphore exit_sem{0};  // blocat initial
    int arrived{0};
    int departed{0};
    int total{0};
};

struct Shared
{
    Barrier barrier;
    int nr_processes{0};
    int nr_phases{0};
};

// Numele segmentului de shared memory in sistemul de fisiere

static const char *SHM_NAME = "boost_barrier_shm";

// barrier_wait - logica identica cu varianta POSIX
// Doua turnstile pentru a preveni ca un proces rapid
// sa reintre in bariera inainte ca ceilalti sa fi iesit
double barrier_wait(Barrier *b, int phase)
{
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    // Zona critica: incrementam arrived
    b->mutex.wait();
    b->arrived++;
    if (b->arrived == b->total)
    {
        b->arrived = 0;
        std::cout << "BARIERA - Faza " << phase
                  << ": toate procesele au ajuns. Deschid intrarea.\n\n";
        // deschidem trecerea pentru toate procesele
        for (int i = 0; i < b->total; i++)
            b->entry_sem.post();
    }
    b->mutex.post();

    b->entry_sem.wait(); // blocat pana cand toate au ajuns

    // Zona critica: incrementam departed
    b->mutex.wait();
    b->departed++;
    if (b->departed == b->total)
    {
        b->departed = 0;
        std::cout << "BARIERA - Faza " << phase
                  << ": toate procesele au trecut. Deschid iesirea.\n\n";
        for (int i = 0; i < b->total; i++)
            b->exit_sem.post();
    }
    b->mutex.post();

    b->exit_sem.wait(); // blocat pana cand toti au consumat tokenul

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    return (t_end.tv_sec - t_start.tv_sec) +
           (t_end.tv_nsec - t_start.tv_nsec) / 1e9;
}

// worker
void worker(int id, Shared *shm)
{
    srand(getpid());
    int n_phases = shm->nr_phases;

    for (int i = 0; i < n_phases; i++)
    {
        int work_time = id % 10 + rand() % 5 + 1;
        printf("Proces %d Faza %d: lucreaza %ds...\n", id, i, work_time);
        sleep(work_time);
        printf("Proces %d Faza %d: terminat lucrul\n", id, i);

        double waited = barrier_wait(&shm->barrier, i); // asteptarea la bariera
        printf("Procesul %d - Faza %d: a asteptat %.3f secunde\n", id, i, waited);
    }

    printf("Proces %d a terminat toate fazele. Iesire.\n", id);
    exit(0);
}

// Clasa RAII pentru shared memory
// In varianta POSIX faceai manual: shmget -> shmat -> shmctl(IPC_RMID)
// Aici constructorul creeaza + mapeaza, destructorul curata automat
// chiar daca procesul crapa (spre deosebire de IPC_PRIVATE din POSIX
// care necesita detasare explicita)
struct SharedMemory
{
    bip::shared_memory_object shm_obj;
    bip::mapped_region region;
    Shared *ptr;

    SharedMemory(int nr_processes, int nr_phases)
        : shm_obj(bip::create_only, SHM_NAME, bip::read_write)
    {
        shm_obj.truncate(sizeof(Shared));
        region = bip::mapped_region(shm_obj, bip::read_write);

        // placement new - construim Shared in memoria deja alocata
        // fara asta semafoarele Boost nu ar fi initializate corect
        ptr = new (region.get_address()) Shared();

        ptr->nr_processes = nr_processes;
        ptr->nr_phases = nr_phases;
        ptr->barrier.total = nr_processes;
    }

    ~SharedMemory()
    {
        // distrugem obiectul Shared construit cu placement new
        ptr->~Shared();
        // stergem segmentul din /dev/shm
        bip::shared_memory_object::remove(SHM_NAME);
    }
};

// Procesele copil se ataseaza la shared memory deja creata
// de parinte - folosesc open_only in loc de create_only
Shared *attach_shared_memory()
{
    static bip::shared_memory_object shm_obj(bip::open_only, SHM_NAME, bip::read_write);
    static bip::mapped_region region(shm_obj, bip::read_write);
    return static_cast<Shared *>(region.get_address());
}

int main(int argc, char *argv[])
{
    if (argc != 3) // verificare argumente
    {
        std::cerr << "Utilizare: " << argv[0]
                  << " <nr_procese> <nr_faze>\n";
        return 1;
    }

    int nr_procese = std::atoi(argv[1]);
    int nr_faze = std::atoi(argv[2]);

    if (nr_procese < 2 || nr_procese > 20 || nr_faze < 1 || nr_faze > 20)
    {
        std::cerr << "Argumente invalide: procese 2-20, faze 1-20\n";
        return 1;
    }

    // Curatam eventuale resturi dintr-o rulare anterioara
    bip::shared_memory_object::remove(SHM_NAME);

    // Dezactivam bufferingul - echivalent cu setbuf(stdout, NULL)
    std::cout << std::unitbuf;

    try
    {
        SharedMemory manager(nr_procese, nr_faze);

        std::cout << "START: " << nr_procese
                  << " procese, " << nr_faze << " faze\n\n";

        for (int i = 0; i < nr_procese; i++)
        {
            pid_t pid = fork();
            if (pid < 0)
            {
                std::cerr << "Eroare fork\n";
                return 1;
            }
            if (pid == 0)
            {
                // Copilul se ataseaza la shared memory existenta
                Shared *shm = attach_shared_memory();
                worker(i, shm);
                // worker apeleaza exit(0), nu ajungem niciodata aici
            }
        }

        for (int i = 0; i < nr_procese; i++)
            wait(nullptr);

        std::cout << "Toate procesele au terminat.\n";
    }
    catch (bip::interprocess_exception &e)
    {
        std::cerr << "Eroare Boost.Interprocess: " << e.what() << "\n";
        bip::shared_memory_object::remove(SHM_NAME);
        return 1;
    }

    return 0;
}