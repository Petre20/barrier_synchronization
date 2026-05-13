#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdlib.h>
#include <time.h>
/*
Definirea barierei
*/
typedef struct
{
    sem_t mutex;     // semafor mutex care protejeaza arrived si departed
    sem_t entry_sem; // semafor de intrare in bariera
    sem_t exit_sem;  // semafor de iesire din barierra
    int arrived;     // procese ajunse la bariera
    int departed;    // procese care au intrat in bariera si asteapta sa iasa
    int total;       // numarul total de procese
} Barrier;

/*
Definirea memoriei partajate intre procese
*/
typedef struct
{
    Barrier barrier;  // bariera
    int nr_processes; // numarul de procese
    int nr_phases;    // numarul de faze
} Shared;

void init_barrier(Barrier *b, int total)
{
    b->total = total;              // initializarea barierei cu numarul total de procese
    b->arrived = 0;                // initializarea numarului de procese ajunse cu 0
    b->departed = 0;               // initializarea numarului de procese intrate in bariera si care asteapta sa iasa cu 0
    sem_init(&b->mutex, 1, 1);     // initializarea mutexului
    sem_init(&b->entry_sem, 1, 0); // initializarea semaforului de intrare in bariera
    sem_init(&b->exit_sem, 1, 0);  // initializarea semaforului de iesire din bariera
}

void barrier_destroy(Barrier *b)
{
    sem_destroy(&b->mutex);     // distrugerea mutexului
    sem_destroy(&b->entry_sem); // distrugerea semaforului de intrare din bariera
    sem_destroy(&b->exit_sem);  // distrugerea semaforului de iesire din bariera
}
/*
Functia care defineste comportamentul procesului la bariera
Bariera foloseste doua faze pentru a preveni un proces rapid
sa intre in faza urmatoare si sa ajunga din nou la bariera
inainte ca celelalte procese sa fi iesit din faza curenta:

entry_sem: sincronizeaza intrarea (toti au ajuns)
exit_sem:  sincronizeaza iesirea (toti au consumat semnalul de intrare)
*/
double barrier_wait(Barrier *b, int phase)
{
    // Masurarea timpului
    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    // Faza de intrare in bariera
    // Blocarea mutexului - intrarea in zona critica
    sem_wait(&b->mutex);
    b->arrived++;               // incrementam numarul de procese intrate in bariera
    if (b->arrived == b->total) // daca toate procesele au ajuns la bariera
    {
        b->arrived = 0; // resetam numarul de procese ajunse
        printf("BARIERA - Faza %d: toate procesele au ajuns. Deschid intrarea.\n\n", phase);
        for (int i = 0; i < b->total; i++) // trimitem semnalul de trezire pentru intrarea in bariera tuturor proceselor
            sem_post(&b->entry_sem);
    }
    sem_post(&b->mutex); // iesirea din zona critica a mutexului

    sem_wait(&b->entry_sem); // blocat pana cand toate procesele au ajuns

    // Faza de iesire din bariera
    sem_wait(&b->mutex);         // se intra din nou in zona critica pentru actualizarea departed
    b->departed++;               // incrementam numarul de procese care asteapta sa iasa din bariera
    if (b->departed == b->total) // daca toate procese au ajuns sa astepta sa iasa pentru a intra in urmatoarea faza
    {
        b->departed = 0; // resetam
        printf("BARIERA - Faza %d: toate procesele au trecut. Deschid iesirea.\n\n", phase);
        for (int i = 0; i < b->total; i++) // trimitem semnalul de iesire din bariera pentru toate procesele
            sem_post(&b->exit_sem);
    }
    sem_post(&b->mutex); // iesirea din zona critica

    sem_wait(&b->exit_sem); // blocat pana cand toate procesele au consumat tokenul de intrare

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    return (t_end.tv_sec - t_start.tv_sec) +
           (t_end.tv_nsec - t_start.tv_nsec) / 1e9;
}
/*
Definirea zonei de memorie partajata
*/
Shared *init_shared_memory(int nrProcese, int nrFaze)
{
    int shmid = shmget(IPC_PRIVATE, sizeof(Shared), IPC_CREAT | 0666); // crearea sau obtinerea segmentului de memorie partajata
    if (shmid < 0)                                                     // eroare la crearea segmentului
    {
        perror("shmget");
        exit(1);
    }

    Shared *shm = shmat(shmid, NULL, 0); // atasam segmentul de memorie partajata
    if (shm == (void *)-1)               // eroare la atasarea segmentului
    {
        perror("shmat");
        exit(1);
    }

    shm->nr_processes = nrProcese;          // atribuim numarul de procese
    shm->nr_phases = nrFaze;                // atribuim numarul de etape de executie
    init_barrier(&shm->barrier, nrProcese); // initializare bariera
    shmctl(shmid, IPC_RMID, NULL);          // marcarea pentru stergere automata a segmentului de memorie partajata dupa ce toate procesele se detaseaza

    return shm;
}

/*
Functie apelata de un proces pentru executarea etapelor de lucru
*/
void worker(int id, Shared *shm)
{
    int n_phases = shm->nr_phases;
    srand(getpid()); // pentru generarea random, avem nevoie de un seed

    for (int i = 0; i < n_phases; i++) // parcurgem etapele de executie
    {

        // calculez timpul de lucru pentru un proces
        int worker_time = id % 10 + rand() % 5 + 1;
        printf("Proces %d Faza %d: lucreaza %ds...\n", id, i, worker_time);
        sleep(worker_time); // simulez o etapa de lucru, fiecare proces va dormi un numar de secunde
        printf("Proces %d Faza %d: terminat lucrul\n", id, i);
        // asteptarea la bariera
        double waitingTime = barrier_wait(&shm->barrier, i);
        // afisam timpul cat a asteptat pana a trecut de bariera
        printf("Procesul %d - Faza %d: a asteptat %.3f secunde\n", id, i, waitingTime);
    }
    // procesul a terminat toate fazele de lucru
    printf("Proces %d a terminat toate fazele. Iesire.\n", id);
    exit(0);
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL); // dezactivarea bufferului stdout astfel incat fiecare proces sa afiseze direct, se afiseaza direct pe ecran, nu se mai pastreaza in buffer

    if (argc != 3) // validarea numarului de argumente
    {
        fprintf(stderr, "Utilizare: %s <nr_procese> <nr_faze>\n", argv[0]);
        exit(1);
    }

    int nrProcese = atoi(argv[1]); // luam numarul de procese din argumente
    int faze = atoi(argv[2]);      // luam numarul de faze din argumente

    // Validarea argumentelor, e nevoie de minim 2 procese si cel putin o faza
    if (nrProcese > 20 || faze > 20 || nrProcese < 2 || faze < 1)
    {
        fprintf(stderr, "Argumente invalide: procese 2-20, faze 1-20\n");
        exit(1);
    }
    // Initializarea zonei de memorie partajata
    Shared *shm = init_shared_memory(nrProcese, faze);

    // Procesul parinte creeaza nrProcese procese si fiecare proces este pus la munca
    for (int i = 0; i < nrProcese; i++)
    {
        pid_t pid = fork(); // clonam procesul, se creeaza un proces copil
        if (pid < 0)        // a aparut eroare la clonare
        {
            perror("fork");
            exit(1);
        }
        if (pid == 0) // daca procesul curent e copilul
        {
            // se intra in functia atribuita, si incepe executie etapelor
            worker(i, shm);
        }
    }

    for (int i = 0; i < nrProcese; i++)
        wait(NULL); // procesul parinte asteapta ca toate procesele copil sa termine

    printf("Toate procesele au terminat.\n");
    barrier_destroy(&shm->barrier); // distrugerea barierei
    shmdt(shm);                     // detasarea segmentului de memorie partajata
    return 0;
}