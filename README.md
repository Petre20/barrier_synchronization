# Process Barrier Synchronization

Implementare a unei bariere de sincronizare pentru procese în C, folosind memorie partajată și semafoare POSIX. Bariera garantează că toate procesele termină o fază de execuție înainte ca oricare dintre ele să treacă la faza următoare.

---

## Cuprins

- [Descriere](#descriere)
- [Arhitectură](#arhitectură)
- [Mecanismul barierei](#mecanismul-barierei)
- [Structuri de date](#structuri-de-date)
- [Compilare](#compilare)
- [Utilizare](#utilizare)
- [Exemplu de rulare](#exemplu-de-rulare)

---

## Descriere

Programul creează `N` procese copil prin `fork`, fiecare executând `M` faze de lucru. La sfârșitul fiecărei faze, procesele se sincronizează la o barieră — niciun proces nu poate trece la faza `i+1` până când toate procesele nu au terminat faza `i`.

Implementarea folosește un **turnstile dublu** (două semafoare) pentru a preveni race condition-ul în care un proces rapid ar putea consuma tokenul destinat unui proces lent din faza anterioară, cauzând deadlock.

---

## Arhitectură

```
main
 ├── init_shared_memory()       # aloca si initializeaza memoria partajata
 ├── fork() x N                 # creeaza N procese copil
 │    └── worker()              # fiecare copil executa M faze
 │         └── barrier_wait()   # sincronizare la sfarsitul fiecarei faze
 └── wait() x N                 # parintele asteapta toti copiii
```

---

## Mecanismul barierei

Bariera folosește două semafoare pentru sincronizare, formând un turnstile dublu:

**Faza de intrare (`entry_sem`):**
Fiecare proces incrementează `arrived` și se blochează la `entry_sem`. Când ultimul proces ajunge (`arrived == total`), deschide `entry_sem` pentru toate procesele simultan.

**Faza de ieșire (`exit_sem`):**
Fiecare proces care a trecut de `entry_sem` incrementează `departed` și se blochează la `exit_sem`. Când ultimul proces ajunge (`departed == total`), deschide `exit_sem` pentru toate procesele simultan.

Această abordare garantează că niciun proces nu poate intra în faza `i+1` înainte ca toate procesele să fi finalizat complet faza `i`, eliminând posibilitatea de deadlock prezentă într-o implementare cu un singur semafor.

```
Proces 0  ──┐
Proces 1  ──┤──► entry_sem ──► exit_sem ──► faza i+1
Proces 2  ──┘
            ▲                  ▲
         toti au            toti au
          ajuns             trecut
```

---

## Structuri de date

### `Barrier`
```c
typedef struct {
    sem_t mutex;      // protejeaza arrived si departed
    sem_t entry_sem;  // turnstile de intrare
    sem_t exit_sem;   // turnstile de iesire
    int arrived;      // procese ajunse la bariera
    int departed;     // procese intrate care asteapta sa iasa
    int total;        // numarul total de procese
} Barrier;
```

### `Shared`
```c
typedef struct {
    Barrier barrier;  // bariera de sincronizare
    int nr_processes; // numarul de procese
    int nr_phases;    // numarul de faze
} Shared;
```

---

## Compilare

```bash
gcc -Wall -Wextra -o barrier barrier.c -lpthread
```

| Flag | Descriere |
|------|-----------|
| `-Wall` | Activează avertismentele comune |
| `-Wextra` | Activează avertismente suplimentare |
| `-lpthread` | Leagă biblioteca POSIX pentru semafoare |

---

## Utilizare

```bash
./barrier <nr_procese> <nr_faze>
```

| Parametru | Descriere | Interval |
|-----------|-----------|----------|
| `nr_procese` | Numărul de procese create | 2 — 20 |
| `nr_faze` | Numărul de faze de execuție | 1 — 20 |

---

## Exemplu de rulare

```bash
./barrier 3 2
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
Proces 0 Faza 1: lucreaza 2s...
Proces 1 Faza 1: lucreaza 5s...
Proces 2 Faza 1: lucreaza 8s...
...
Toate procesele au terminat.
```

Timpul de așteptare raportat de fiecare proces reprezintă intervalul de la momentul sosirii la barieră până la primirea tokenului de ieșire — procesul care ajunge primul așteaptă cel mai mult, cel care ajunge ultimul trece aproape instant.

---

## Resurse

- [POSIX Semaphores — man sem_init](https://man7.org/linux/man-pages/man3/sem_init.3.html)
- [Shared Memory — man shmget](https://man7.org/linux/man-pages/man2/shmget.2.html)
- [The Little Book of Semaphores — Allen B. Downey](https://greenteapress.com/wp/semaphores/)
