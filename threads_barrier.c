#define _GNU_SOURCE     //necesar pentru a expune extensiile POSIX
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

pthread_barrier_t barrier;  //bariera globala -> sincronizeaza toate thread-urile la sfarsitul fiecarei faze

// structura pentru a incamsula argumentele trimise fiecarui thread
typedef struct {
    int id;
    int num_phases;
} ThreadArg;

// returneaza diferenta dintre 2 momente de timp in milisecunde
double get_time_diff(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

// functie care simuleaza "munca" unui thread
static void simulate_work(int id, int phase, int work_ms){
    printf("[Thread %d] Faza %d: A inceput executia... (%d ms)\n", id, phase, work_ms);
    fflush(stdout);
    struct timespec ts = {.tv_sec  = work_ms / 1000, .tv_nsec = (work_ms % 1000) * 1000000L};

    nanosleep(&ts, NULL);
}

// sincronizeaza thread-urile la bariera si raporteaza timpul de asteptare
static void wait_at_barrier(int id, int phase){
    struct timespec wait_start, wait_end;
    printf("Thread %d Faza %d: A terminat executia. Asteapta la bariera.\n", id, phase);
    fflush(stdout);

    clock_gettime(CLOCK_MONOTONIC, &wait_start);
    int ret = pthread_barrier_wait(&barrier);   // blocare pana ajung toate thread-urile
    clock_gettime(CLOCK_MONOTONIC,&wait_end);
    // PTHREAD_BARRIER_SERIAL_THREAD este returnat unui singur thread - folosit pentru mesajul global
    if(ret == PTHREAD_BARRIER_SERIAL_THREAD){
        printf("\nBARIERA DESCHISA pentru Faza %d. Trecem la urmatoarea.\n\n", phase);
        fflush(stdout);
    }

    double wait_ms=get_time_diff(wait_start, wait_end);
    
    printf("Thread %d Faza %d: A trecut bariera. Timp asteptat: %.2f ms\n", id, phase, wait_ms);
    fflush(stdout);
}

// functie executata de fiecare thread
void* thread_work(void *arg){
    ThreadArg *targ=(ThreadArg *)arg;
    int id=targ->id;
    int num_phases=targ->num_phases;
    free(targ); // argumentul a fost alocat dinamic in spawn_threads
    unsigned int seed=time(NULL)+id;    // seed unic per thread pentru rand_r
    for(int phase=1; phase<=num_phases; phase++){
        int work_ms=(rand_r(&seed)%4500)+500;   // durata aleatoare intre 500 si 5000 ms
        simulate_work(id, phase, work_ms);
        wait_at_barrier(id, phase);
    }
    return NULL;
}

// valideaza numarul si continutul argumentelor din linia de comanda
static int validate_args(int argc, char *argv[], int *num_threads, int *num_phases) {
    if (argc != 3) {
        fprintf(stderr, "Utilizare: %s <numar_threaduri> <numar_etape>\n", argv[0]);
        return 0;
    }
    *num_threads = atoi(argv[1]);
    *num_phases  = atoi(argv[2]);
    if (*num_threads <= 0 || *num_phases <= 0) {
        fprintf(stderr, "Eroare: valorile trebuie sa fie > 0.\n");
        return 0;
    }
    return 1;
}

// aloca si porneste num_threads threaduri, fiecare thread are propriul ThreadArg
static pthread_t *spawn_threads(int num_threads, int num_phases){
    pthread_t *threads=malloc(num_threads * sizeof(pthread_t));
    if(!threads){
        perror("malloc threads");
        return NULL;
    }

    for(int i=0;i<num_threads;i++){
        ThreadArg *targ=malloc(sizeof(ThreadArg));
        if(!targ){
            perror("malloc arg");
            return NULL;
        }
        targ->id=i;
        targ->num_phases=num_phases;
        if(pthread_create(&threads[i], NULL, thread_work, targ)!=0){
            perror("pthread create");
            free(targ);
            free(threads);
            return NULL;
        }
    }
    return threads;
}

// asteapta terminarea tuturor thread-urilor
static void join_threads(pthread_t *threads, int num_threads){
    for(int i=0;i<num_threads;i++)
        pthread_join(threads[i],NULL);
}

// functie de cleanup ("distruge" bariera si elibereaza vectorul de threads)
static void cleanup(pthread_t *threads){
    pthread_barrier_destroy(&barrier);
    free(threads);
}

int main(int argc, char *argv[]) {
    int num_threads, num_phases;

    if(!validate_args(argc, argv, &num_threads, &num_phases))
        return EXIT_FAILURE;

    pthread_barrier_init(&barrier, NULL, num_threads);  // initializare bariera - asteapta num_threads thread-uri

    printf("INCEPUT SIMULARE: %d Thread-uri si %d Faze\n\n",num_threads, num_phases);

    pthread_t *threads=spawn_threads(num_threads, num_phases);

    if(!threads){
        pthread_barrier_destroy(&barrier);
        return EXIT_FAILURE;
    }
    
    join_threads(threads, num_threads);
    cleanup(threads);
    printf("SIMULARE FINALIZATA\n");

    return 0;
}