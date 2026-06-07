#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <string.h>

/*
Definirea barierei
*/
typedef struct
{
   int mutex_id; // id ul semaforului 
   int entry_id; // id ul semaforului de iesire
   int exit_id; // id ul semaforului de iesire
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

// Creare semafor
int sem_create(int val)
{
    union semun { int val; } arg;
 
    int semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666); 
    if (semid < 0) { perror("semget"); exit(1); }
 
    arg.val = val;
    semctl(semid, 0, SETVAL, arg);
    return semid;
}

// Stergere semafor
void sem_remove(int semid)
{
    semctl(semid, 0, IPC_RMID);
}

// wait
void sem_p(int semid)
{
    struct sembuf op = {0, -1, 0}; // -1 pentru operatia de decrementare
    while (semop(semid, &op, 1) == -1 && errno == EINTR);
}
// signal
void sem_v(int semid)
{
    struct sembuf op = {0, +1, 0}; // +1 pentru incrementare => se vor trezi procese care astepta la acest semanfor
    semop(semid, &op, 1);
}

void sem_v_n(int semid,int n)
{
    struct sembuf op = { 0, n, 0};
    semop(semid,&op,1);
}



/*
Initializarea barierei
*/
void init_barrier(Barrier *b, int total)
{
    b->mutex_id = sem_create(1);
    b->entry_id = sem_create(0);  
    b->exit_id  = sem_create(0);  
    b->arrived  = 0;
    b->departed = 0;
    b->total    = total;
}
 /*
 Distrugerea barierei
 */
void barrier_destroy(Barrier *b)
{
    sem_remove(b->mutex_id);
    sem_remove(b->entry_id);
    sem_remove(b->exit_id);
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
    sem_p(b->mutex_id);
    b->arrived++;               // incrementam numarul de procese intrate in bariera
    if (b->arrived == b->total) // daca toate procesele au ajuns la bariera
    {
        b->arrived = 0; // resetam numarul de procese ajunse
        printf("BARIERA - Faza %d: toate procesele au ajuns. Deschid intrarea.\n\n", phase);
        sem_v_n(b->entry_id,b->total);
    }
    sem_v(b->mutex_id); // iesirea din zona critica a mutexului

    sem_p(b->entry_id); // blocat pana cand toate procesele au ajuns

    // Faza de iesire din bariera
    sem_p(b->mutex_id);         // se intra din nou in zona critica pentru actualizarea departed
    b->departed++;               // incrementam numarul de procese care asteapta sa iasa din bariera
    if (b->departed == b->total) // daca toate procese au ajuns sa astepta sa iasa pentru a intra in urmatoarea faza
    {
        b->departed = 0; // resetam
        printf("BARIERA - Faza %d: toate procesele au trecut. Deschid iesirea.\n\n", phase);
        sem_v_n(b->exit_id,b->total);
    }
    sem_v(b->mutex_id); // iesirea din zona critica

    sem_p(b->exit_id); // blocat pana cand toate procesele au consumat tokenul de intrare

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    return (t_end.tv_sec - t_start.tv_sec) +
           (t_end.tv_nsec - t_start.tv_nsec) / 1e9;
}


Shared *init_shared_memory(int nrProcese, int nrFaze)
{
    int shmid = shmget(IPC_PRIVATE, sizeof(Shared), IPC_CREAT | 0666);
    if (shmid < 0) { perror("shmget"); exit(1); }
 
    Shared *shm = shmat(shmid, NULL, 0);
    if (shm == (void *)-1) { perror("shmat"); exit(1); }
 
    shm->nr_processes = nrProcese;
    shm->nr_phases    = nrFaze;
    init_barrier(&shm->barrier, nrProcese);
 
    shmctl(shmid, IPC_RMID, NULL);   /* ștergere automată la ultima detașare */
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


int main(int argc, char *argv[]){

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
    shmdt(shm);   
    return 0;
}