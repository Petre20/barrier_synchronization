#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>

/*
Definirea barierei
*/
typedef struct
{
    sem_t mutex;
    sem_t entry_sem;
    sem_t exit_sem;
    int arrived;
    int departed;
    int total;
} Barrier;

/*
Initializarea barierei
*/
void init_barrier(Barrier *b, int total)
{
    b->total = total;
    b->arrived = 0;
    b->departed = 0;
    sem_init(&b->mutex, 1, 1);
    sem_init(&b->entry_sem, 1, 0);
    sem_init(&b->exit_sem, 1, 0);
}

/*
Distrugerea barierei
*/
void barrier_destroy(Barrier *b)
{
    sem_destroy(&b->mutex);
    sem_destroy(&b->entry_sem);
    sem_destroy(&b->exit_sem);
}

/*
Interschimbare si resetare
*/
void swap_and_reset(long long *current, long long *result, int N)
{
    memcpy(current, result, N * N * sizeof(long long));
    memset(result, 0, N * N * sizeof(long long));
}

/*
Asteptarea la bariera.
Ultimul proces care ajunge face swap-ul inainte sa elibereze ceilalti,
in acel moment toti sunt blocati la entry_sem, deci nimeni nu mai citeste
din current sau result, swap-ul e safe fara o a doua bariera.
*/
void barrier_wait(Barrier *b, int phase, long long *current, long long *result, int N)
{
    sem_wait(&b->mutex);
    b->arrived++;
    if (b->arrived == b->total)
    {
        b->arrived = 0;
        printf("\n  BARIERA  Faza %d: toate procesele au terminat."
               " Deschid intrarea.\n",
               phase);
        swap_and_reset(current, result, N);
        for (int i = 0; i < b->total; i++)
            sem_post(&b->entry_sem);
    }
    sem_post(&b->mutex);
    sem_wait(&b->entry_sem);

    sem_wait(&b->mutex);
    b->departed++;
    if (b->departed == b->total)
    {
        b->departed = 0;
        printf("  BARIERA Faza %d: toate procesele au trecut."
               " Deschid iesirea.\n\n",
               phase);
        for (int i = 0; i < b->total; i++)
            sem_post(&b->exit_sem);
    }
    sem_post(&b->mutex);
    sem_wait(&b->exit_sem);
}

/*
Definirea memoriei partajate.
Avem 3 matrici: A (read-only), current (acumulatorul), result
*/
typedef struct
{
    Barrier barrier;
    int nr_procese;
    int nr_faze;
    int N;
} Shared;

/*
Fnctii pentru calcularea offsetrurilor deoarece N nu e cunoscut la compilare, fiecare sare peste o parte din memorie partajata
*/
long long *get_A_orig(Shared *hdr)
{
    return (long long *)((char *)hdr + sizeof(Shared));
}

long long *get_current(Shared *hdr)
{
    return (long long *)((char *)hdr + sizeof(Shared) + hdr->N * hdr->N * sizeof(long long));
}

long long *get_result(Shared *hdr)
{
    return (long long *)((char *)hdr + sizeof(Shared) + 2 * hdr->N * hdr->N * sizeof(long long));
}

#define MAT(ptr, N, i, j) ((ptr)[(i) * (N) + (j)]) // Ne ofera elementul de la pozitia (i,j)

/*
Generare matrice
*/
void generate_matrix(long long *A, int N, unsigned int seed)
{
    srand(seed);
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            MAT(A, N, i, j) = rand() % 5 + 1; // Generam o matrice cu valori intre 1-5

    printf("MATRICE ORIGINALA\n");
    for (int i = 0; i < N; i++)
    {
        for (int j = 0; j < N; j++)
            printf("%lld ", MAT(A, N, i, j));
        printf("\n");
    }
    printf("\n");
}

/*
Genereaza matricea identitate — current porneste din I astfel incat
dupa k faze de inmultire cu A_orig, current = A^k
*/
void generate_identity(long long *I, int N)
{
    memset(I, 0, N * N * sizeof(long long));
    for (int i = 0; i < N; i++)
        MAT(I, N, i, i) = 1;
}

/*
Afisare matrice
*/
void afis_matrix(char *label, long long *A, int N)
{
    printf("%s (%dx%d):\n", label, N, N);
    for (int i = 0; i < N; i++)
    {
        printf("  ");
        for (int j = 0; j < N; j++)
            printf("%lld ", MAT(A, N, i, j));
        printf("\n");
    }
    printf("\n");
}

/*
Face inmultirile si calculele necesare pentru randurile aferente id-ului procesului ciclic.
Calculeaza result = current * A_orig pe liniile procesului.
A_orig e read-only — toate procesele il citesc simultan fara sincronizare.
*/
void multiply_rows(long long *A, long long *B, long long *C, int N,
                   int proc_id, int nr_procese)
{
    for (int i = proc_id; i < N; i += nr_procese)
    {
        for (int j = 0; j < N; j++)
        {
            long long sum = 0;
            for (int k = 0; k < N; k++)
                sum += MAT(A, N, i, k) * MAT(B, N, k, j);
            MAT(C, N, i, j) = sum;
        }
    }
}

void worker(int id, Shared *hdr)
{
    int N = hdr->N;
    int nr_procese = hdr->nr_procese;
    int nr_faze = hdr->nr_faze;
    long long *A_orig  = get_A_orig(hdr);  // niciodata modificat
    long long *current = get_current(hdr);
    long long *result  = get_result(hdr);

    for (int faza = 0; faza < nr_faze; faza++)
    {
        printf("  Proces %d | Faza %d: calculeaza randurile", id, faza + 1);
        for (int i = id; i < N; i += nr_procese)
            printf(" %d", i);
        printf("\n");

        // Lucrul efectiv: result = current * A_orig (inmultire liniara cu A original)
        multiply_rows(current, A_orig, result, N, id, nr_procese);

        printf("  Proces %d | Faza %d: terminat\n", id, faza + 1);

        barrier_wait(&hdr->barrier, faza + 1, current, result, N);
    }

    printf("  Proces %d: toate fazele terminate. Iesire.\n", id);
    exit(0);
}

/*
Calculul normal al ridicarii la putere prin inmultire liniara cu A la fiecare pas.
Porneste din matricea identitate, dupa nr_faze pasi obtine A^nr_faze.
*/
void normal_power(long long *A, long long *out, int N, int nr_faze)
{
    // Avem nevoie de 2 matrici: current porneste din I, temp retine rezultatul fazei curente
    long long *temp    = malloc(N * N * sizeof(long long));
    long long *current = malloc(N * N * sizeof(long long));
    generate_identity(current, N); // pornim din I, nu din A

    for (int faza = 0; faza < nr_faze; faza++) // calcularea A^nr_faze prin inmultire repetata cu A
    {
        for (int i = 0; i < N; i++)
            for (int j = 0; j < N; j++)
            {
                long long sum = 0;
                for (int k = 0; k < N; k++)
                    sum += MAT(current, N, i, k) * MAT(A, N, k, j); // temp e matricea calculata la etapa curenta
                MAT(temp, N, i, j) = sum;
            }
        memcpy(current, temp, N * N * sizeof(long long)); // si la final ii dam la current temp - matricea curenta se va actualiza
    }

    memcpy(out, current, N * N * sizeof(long long));
    free(temp);
    free(current);
}

/*
Verifica ca matricea generata in mod paralel e egala cu cea generata simplu, fara paralelism
*/
int check(long long *parallel, long long *normal, int N)
{
    for (int i = 0; i < N; i++)
        for (int j = 0; j < N; j++)
            if (MAT(parallel, N, i, j) != MAT(normal, N, i, j))
            {
                printf("  Eroare la (%d,%d): paralel=%lld normal=%lld\n",
                       i, j,
                       MAT(parallel, N, i, j),
                       MAT(normal, N, i, j));
                return 0;
            }
    return 1;
}

int main(int argc, char *argv[])
{
    setbuf(stdout, NULL);

    if (argc != 4) // Trebuie transmis 4 argumente
    {
        fprintf(stderr, "Utilizare: %s <nr_procese> <N> <putere>\n", argv[0]);
        fprintf(stderr, "  nr_procese : numarul de procese (2-16)\n");
        fprintf(stderr, "  N          : dimensiunea matricei NxN (2-1000)\n");
        fprintf(stderr, "  putere     : A^putere, orice valoare (1-20)\n");
        fprintf(stderr, "Exemplu: %s 4 4 3  =>  A^3\n\n", argv[0]);
        return 1;
    }

    int nr_procese = atoi(argv[1]);
    int N          = atoi(argv[2]);
    int nr_faze    = atoi(argv[3]);

    // Verificam argumentele transmise
    if (nr_procese < 2 || nr_procese > 16)
    {
        fprintf(stderr, "nr_procese: 2-16\n");
        return 1;
    }
    if (N < 2 || N > 1000)
    {
        fprintf(stderr, "N: 2-1000\n");
        return 1;
    }
    if (nr_faze < 1 || nr_faze > 20)
    {
        fprintf(stderr, "putere: 1-20\n");
        return 1;
    }

    printf("  Ridicare la putere cu bariera de sincronizare\n");
    printf("  Matrice: %dx%d\n", N, N);
    printf("  Putere: A^%d\n", nr_faze);
    printf("  Procese: %d\n\n", nr_procese);

    // Crearea segmentului de memorie partajata — avem nevoie de 3 matrici:
    // A_orig (read-only), current (acumulator), result (scratch)
    size_t shm_size = sizeof(Shared) + 3 * N * N * sizeof(long long);
    int shmid = shmget(IPC_PRIVATE, shm_size, IPC_CREAT | 0666);
    if (shmid < 0)
    {
        perror("shmget");
        return 1;
    }

    Shared *hdr = shmat(shmid, NULL, 0); // atasarea segmentului de mem partajata
    if (hdr == (void *)-1)
    {
        perror("shmat");
        return 1;
    }
    shmctl(shmid, IPC_RMID, NULL); // marcare spre stergere

    hdr->nr_procese = nr_procese;
    hdr->nr_faze    = nr_faze;
    hdr->N          = N;
    init_barrier(&hdr->barrier, nr_procese); // initializam bariera

    long long *A_orig  = get_A_orig(hdr);
    long long *current = get_current(hdr);
    long long *result  = get_result(hdr);

    generate_matrix(A_orig, N, 10);               // generam o matrice de dimensiune NxN
    generate_identity(current, N);                // current porneste din I
    memset(result, 0, N * N * sizeof(long long)); // setam bitii in result

    long long *A_original = malloc(N * N * sizeof(long long)); // alocam memorie pentru matricea originala
    memcpy(A_original, A_orig, N * N * sizeof(long long));

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    for (int i = 0; i < nr_procese; i++)
    {
        pid_t pid = fork(); // creare copii si punere la munca :(
        if (pid < 0)
        {
            perror("fork");
            return 1;
        }
        if (pid == 0)
            worker(i, hdr);
    }

    for (int i = 0; i < nr_procese; i++)
        wait(NULL); // astept ca toti copii sa termine munca

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (t_end.tv_sec - t_start.tv_sec) +
                     (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    printf("Calcul paralel terminat in %.4f secunde\n\n", elapsed);
    afis_matrix("Rezultat paralel A^k", current, N);

    printf("Verificare cu calcul normal\n\n");
    long long *normal_result = malloc(N * N * sizeof(long long));
    normal_power(A_original, normal_result, N, nr_faze);
    afis_matrix("Rezultat normal A^k", normal_result, N);

    if (check(current, normal_result, N))
        printf("  Verificare: OK - rezultatul paralel coincide cu cel normal\n\n");
    else
        printf("  Verificare: EROARE - rezultatele difera\n\n");

    // Clean up
    free(A_original);
    free(normal_result);
    barrier_destroy(&hdr->barrier); // distrugerea barierei, eliberarea memoriei
    shmdt(hdr);                     // detasarea segmentului de memorie partajata

    return 0;
}