# Process & Thread Barrier Synchronization

Implementare a unei bariere de sincronizare în C în două variante — bazată pe **procese** cu memorie partajată și semafoare POSIX, și bazată pe **thread-uri** cu `pthread_barrier_t`. Bariera garantează că toate unitățile de execuție termină o fază înainte ca oricare dintre ele să treacă la faza următoare.

---

## Cuprins

- [Descriere](#descriere)
- [Varianta cu procese](#varianta-cu-procese)
- [Varianta cu thread-uri](#varianta-cu-thread-uri)
- [Comparație](#comparație)
- [Compilare](#compilare)
- [Utilizare](#utilizare)
- [Exemple de rulare](#exemple-de-rulare)
- [Resurse](#resurse)

---

## Descriere

Ambele variante creează `N` unități de execuție care parcurg `M` faze de lucru simulate. La sfârșitul fiecărei faze, unitățile se sincronizează la o barieră — niciun participant nu poate trece la faza `i+1` până când toți nu au terminat faza `i`.

---

## Varianta cu procese

### Arhitectură

```
main
 ├── init_shared_memory()       # aloca si initializeaza memoria partajata
 ├── fork() x N                 # creeaza N procese copil
 │    └── worker()              # fiecare copil executa M faze
 │         └── barrier_wait()   # sincronizare la sfarsitul fiecarei faze
 └── wait() x N                 # parintele asteapta toti copiii
```

### Mecanismul barierei — Turnstile dublu

Bariera folosește două semafoare pentru a preveni race condition-ul în care un proces rapid ar putea consuma tokenul destinat unui proces lent din faza anterioară, cauzând deadlock.

**De ce un singur semafor nu este suficient:**

Cu un singur semafor, după ce ultimul proces postează `N` tokeni, semaforul nu știe pentru ce fază sunt destinați. Un proces rapid poate consuma propriul token, executa faza `i+1` și consuma un token destinat altui proces din faza `i`, lăsând acel proces blocat pentru totdeauna.

**Soluția — două semafoare:**

```
Proces 0  ──┐
Proces 1  ──┤──► entry_sem ──► exit_sem ──► faza i+1
Proces 2  ──┘
            ▲                  ▲
         toti au            toti au
          ajuns             trecut
```

**Faza de intrare (`entry_sem`):** fiecare proces incrementează `arrived` și se blochează. Când ultimul ajunge (`arrived == total`), deschide `entry_sem` pentru toți simultan.

**Faza de ieșire (`exit_sem`):** fiecare proces care a trecut de `entry_sem` incrementează `departed` și se blochează. Când ultimul ajunge (`departed == total`), deschide `exit_sem` pentru toți simultan.

Niciun proces nu poate intra în faza `i+1` înainte ca toți să fi trecut ambele puncte de control.

### Structuri de date

```c
typedef struct {
    sem_t mutex;      // protejeaza arrived si departed
    sem_t entry_sem;  // turnstile de intrare
    sem_t exit_sem;   // turnstile de iesire
    int arrived;      // procese ajunse la bariera
    int departed;     // procese intrate care asteapta sa iasa
    int total;        // numarul total de procese
} Barrier;

typedef struct {
    Barrier barrier;  // bariera de sincronizare
    int nr_processes; // numarul de procese
    int nr_phases;    // numarul de faze
} Shared;
```

### Apeluri de sistem folosite

| Apel | Rol |
|------|-----|
| `fork` | Crearea proceselor copil |
| `wait` | Așteptarea terminării copiilor |
| `shmget` | Crearea segmentului de memorie partajată |
| `shmat` | Atașarea segmentului |
| `shmctl` | Marcare pentru ștergere automată |
| `shmdt` | Detașarea segmentului |
| `clock_gettime` | Măsurarea timpului de așteptare |
| `getpid` | Seed unic pentru generarea aleatoare |
| `sleep` | Simularea muncii |

### Intervalul de timp de lucru

Timpul de lucru per proces per fază este calculat ca `id % 10 + rand() % 5 + 1`, rezultând o plajă de **1 până la 14 secunde**.

---

## Varianta cu thread-uri

### Arhitectură

```
main
 ├── pthread_barrier_init()     # initializare bariera POSIX
 ├── spawn_threads() x N        # creeaza N thread-uri
 │    └── thread_work()         # fiecare thread executa M faze
 │         ├── simulate_work()  # simuleaza munca cu nanosleep
 │         └── wait_at_barrier() # sincronizare la sfarsitul fazei
 └── join_threads()             # asteapta toate thread-urile
      └── cleanup()             # distruge bariera si elibereaza memoria
```

### Mecanismul barierei — `pthread_barrier_t`

Varianta cu thread-uri folosește bariera nativă POSIX `pthread_barrier_t`, implementată direct în kernel, fără a fi nevoie de o implementare manuală cu semafoare.

`pthread_barrier_wait` blochează thread-ul apelant până când toate `N` thread-urile au apelat funcția. Un singur thread primește `PTHREAD_BARRIER_SERIAL_THREAD` ca valoare de retur — acesta este folosit pentru a afișa mesajul global de deschidere a barierei.

```c
int ret = pthread_barrier_wait(&barrier);
if (ret == PTHREAD_BARRIER_SERIAL_THREAD) {
    printf("\nBARIERA DESCHISA pentru Faza %d.\n", phase);
}
```

### Structuri de date

```c
pthread_barrier_t barrier;  // bariera globala POSIX

typedef struct {
    int id;
    int num_phases;
} ThreadArg;               // argumente trimise fiecarui thread
```

### Intervalul de timp de lucru

Timpul de lucru per thread per fază este `rand_r(&seed) % 4500 + 500`, rezultând o plajă de **500 până la 5000 milisecunde**. Fiecare thread folosește un seed unic (`time(NULL) + id`) prin `rand_r` — funcție thread-safe față de `rand`.

---

## Comparație

| Criteriu | Procese | Thread-uri |
|----------|---------|------------|
| Unitate de execuție | Proces (`fork`) | Thread (`pthread_create`) |
| Memorie | Separată, partajată explicit prin `shmget` | Partajată implicit |
| Barieră | Implementată manual cu 2 semafoare | `pthread_barrier_t` nativ |
| Comunicare | Memorie partajată (`Shared`) | Variabile globale / argumente |
| Overhead | Mai mare (procese separate) | Mai mic (același spațiu de adrese) |
| Izolare | Mai puternică (crash izolat per proces) | Mai slabă (un thread poate afecta toți) |
| Timp simulat | Secunde (`sleep`) | Milisecunde (`nanosleep`) |
| Corectitudine barieră | Garantată prin turnstile dublu | Garantată de implementarea kernel |

---

## Compilare

**Varianta cu procese:**
```bash
gcc -Wall -Wextra -o barrier_proc barrier_proc.c -lpthread
```

**Varianta cu thread-uri:**
```bash
gcc -Wall -Wextra -o barrier_thread barrier_thread.c -lpthread
```

---

## Utilizare

**Varianta cu procese:**
```bash
./barrier_proc <nr_procese> <nr_faze>
```

**Varianta cu thread-uri:**
```bash
./barrier_thread <nr_threaduri> <nr_faze>
```

| Parametru | Descriere | Procese | Thread-uri |
|-----------|-----------|---------|------------|
| `nr_procese` / `nr_threaduri` | Numărul de unități de execuție | 2 — 20 | > 0 |
| `nr_faze` | Numărul de faze de execuție | 1 — 20 | > 0 |

---

## Exemple de rulare

**Varianta cu procese:**
```bash
./barrier_proc 3 2
```
```
Proces 0 Faza 0: lucreaza 1s...
Proces 1 Faza 0: lucreaza 4s...
Proces 2 Faza 0: lucreaza 7s...
Proces 0 Faza 0: terminat lucrul
Proces 1 Faza 0: terminat lucrul
Proces 2 Faza 0: terminat lucrul
BARIERA - Faza 0: toate procesele au ajuns. Deschid intrarea.

BARIERA - Faza 0: toate procesele au trecut. Deschid iesirea.

Procesul 0 - Faza 0: a asteptat 6.001 secunde
Procesul 1 - Faza 0: a asteptat 3.001 secunde
Procesul 2 - Faza 0: a asteptat 0.000 secunde
Toate procesele au terminat.
```

**Varianta cu thread-uri:**
```bash
./barrier_thread 3 2
```
```
INCEPUT SIMULARE: 3 Thread-uri si 2 Faze

[Thread 0] Faza 1: A inceput executia... (1243 ms)
[Thread 1] Faza 1: A inceput executia... (3512 ms)
[Thread 2] Faza 1: A inceput executia... (4891 ms)
Thread 0 Faza 1: A terminat executia. Asteapta la bariera.
Thread 1 Faza 1: A terminat executia. Asteapta la bariera.
Thread 2 Faza 1: A terminat executia. Asteapta la bariera.

BARIERA DESCHISA pentru Faza 1. Trecem la urmatoarea.

Thread 0 Faza 1: A trecut bariera. Timp asteptat: 3648.21 ms
Thread 1 Faza 1: A trecut bariera. Timp asteptat: 1379.05 ms
Thread 2 Faza 1: A trecut bariera. Timp asteptat: 0.12 ms
SIMULARE FINALIZATA
```

---

## Resurse

- [POSIX Semaphores — man sem_init](https://man7.org/linux/man-pages/man3/sem_init.3.html)
- [pthread_barrier_wait — man page](https://man7.org/linux/man-pages/man3/pthread_barrier_wait.3p.html)
- [Shared Memory — man shmget](https://man7.org/linux/man-pages/man2/shmget.2.html)
- [The Little Book of Semaphores — Allen B. Downey](https://greenteapress.com/wp/semaphores/)
