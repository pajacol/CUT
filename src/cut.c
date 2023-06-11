#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "read_file.h"
#include "queue.h"

/* #define HANG            \
if(rand() % 10 == 4)    \
{                       \
    sleep_internal(10); \
} */

extern int nanosleep(const struct timespec*, struct timespec*);
extern ssize_t readlink(const char*, char*, size_t);

struct proc_stat
{
    int user;
    int nice;
    int system;
    int idle;
    int iowait;
    int irq;
    int softirq;
    int steal;
    int guest;
    int guest_nice;
};

/* Threads */
static pthread_t threads[6];

/* Variable to notify threads to finish */
static volatile int finish = 0;

/* Queues for communication between threads */
static struct queue *queue_reader_analyzer;
static struct queue *queue_analyzer_printer;
static struct queue *queue_all_watchdog;
static struct queue *queue_all_logger;

/* Number of cores */
static long int cores;

/* Heap allocated variables to clean at exit */
static void *allocations[5][8];

/* Opened descriptors */
static int files[5];

/* Wait times */
static double waits[5];

/* Thread done */
static int work_done[4];

static void sleep_internal(double seconds)
{
    int full_seconds = (int)seconds;
    nanosleep((struct timespec[]){{full_seconds, (int)((seconds - full_seconds) * 1000000000)}}, NULL);
    return;
}

static double cpu_use(const struct proc_stat *current, const struct proc_stat *previous)
{
    int Idle, NonIdle, Total;
    int PrevIdle, PrevNonIdle, PrevTotal;
    int TotalD, IdleD;

    Idle = current->idle + current->iowait;
    PrevIdle = previous->idle + previous->iowait;

    NonIdle = current->user + current->nice + current->system + current->irq + current->softirq + current->steal;
    PrevNonIdle = previous->user + previous->nice + previous->system + previous->irq + previous->softirq + previous->steal;

    Total = Idle + NonIdle;
    PrevTotal = PrevIdle + PrevNonIdle;

    TotalD = Total - PrevTotal;
    IdleD = Idle - PrevIdle;

    return 100.0 * (TotalD - IdleD) / TotalD;
}

static void* cleanup(void *arg)
{
    /* Give threads time to react */
    sleep_internal(1);
    /* Kill blocked threads on queues */
    /* Reader */
    if(!work_done[0])
    {
        pthread_cancel(threads[0]);
    }
    /* Analyzer */
    if(!work_done[1])
    {
        pthread_cancel(threads[1]);
    }
    /* Printer */
    if(!work_done[2])
    {
        pthread_cancel(threads[2]);
    }
    /* Logger */
    if(!work_done[3])
    {
        pthread_cancel(threads[4]);
    }
    /* Watchdog */
    pthread_cancel(threads[3]);
    return 0;
    (void)arg;
}

static void handler(int signum)
{
    finish = 1;
    pthread_create(&threads[5], NULL, cleanup, NULL);
    return;
    (void)signum;
}

static void* reader(void *arg)
{
    int fd = open("/proc/stat", O_RDONLY | O_NONBLOCK);
    char *analyzer_buf = malloc(0x10000), *logger_buf = malloc(0x10000);
    int watchdog_buf = 0, len;
    allocations[0][0] = analyzer_buf;
    allocations[0][1] = logger_buf;
    files[0] = fd;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while(!finish)
    {
        /* Read whole /proc/stat */
        len = read_file(fd, analyzer_buf);
        /* Copy for Logger */
        *logger_buf = 0;
        strcpy(logger_buf + 1, analyzer_buf);
        /* Send to Reader-Analyzer queue */
        enqueue(queue_reader_analyzer, analyzer_buf, len);
        /* Notify Logger */
        enqueue(queue_all_logger, logger_buf, len + 1);
        /* Notify Watchdog */
        enqueue(queue_all_watchdog, &watchdog_buf, sizeof(int));
        /* Wait some time */
        sleep_internal(waits[0]);
    }
    work_done[0] = 1;
    return 0;
    (void)arg;
}

static void* analyzer(void *arg)
{
    char *reader_buf = malloc(0x10000), *printer_buf = malloc(0x1000), *logger_buf = malloc(0x1000);
    int watchdog_buf = 1, len, i, offset, size;
    /* For calculation of usage */
    struct proc_stat *current = malloc((unsigned int)cores * sizeof(struct proc_stat));
    struct proc_stat *previous = calloc((unsigned int)cores, sizeof(struct proc_stat));
    double *use = malloc((unsigned int)cores * sizeof(double));
    allocations[1][0] = reader_buf;
    allocations[1][1] = printer_buf;
    allocations[1][2] = logger_buf;
    allocations[1][3] = current;
    allocations[1][4] = previous;
    allocations[1][5] = use;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while(!finish)
    {
        /* Read from Reader-Analyzer queue */
        dequeue(queue_reader_analyzer, reader_buf, &len);
        /* Omit first line */
        sscanf(reader_buf, "%*s%*d%*d%*d%*d%*d%*d%*d%*d%*d%*d%n", &offset);
        /* Read for each core */
        for(i = 0; i < cores; ++i)
        {
            /* Read data for i-th core */
            sscanf(&reader_buf[offset], "%*s%d%d%d%d%d%d%d%d%d%d%n", &current[i].user, &current[i].nice, &current[i].system, &current[i].idle, &current[i].iowait, &current[i].irq, &current[i].softirq, &current[i].steal, &current[i].guest, &current[i].guest_nice, &size);
            /* CPU use for i-th core */
            use[i] = cpu_use(&current[i], &previous[i]);
            /* Next line offset */
            offset += size;
            /* Copy for later calculation */
            previous[i] = current[i];
        }
        /* Write use of each core to buffer */
        offset = 0;
        for(i = 0; i < cores; ++i)
        {
            sprintf(&printer_buf[offset], "%lf %n", use[i], &size);
            offset += size;
        }
        /* Copy data for Logger */
        *logger_buf = 1;
        strcpy(logger_buf + 1, printer_buf);
        /* Send to Analyzer-Printer queue */
        enqueue(queue_analyzer_printer, printer_buf, offset);
        /* Notify Logger */
        enqueue(queue_all_logger, logger_buf, offset + 1);
        /* Notify Watchdog */
        enqueue(queue_all_watchdog, &watchdog_buf, sizeof(int));
        /* Wait some time */
        sleep_internal(waits[1]);
    }
    work_done[1] = 1;
    return 0;
    (void)arg;
}

static void* printer(void *arg)
{
    char *analyzer_buf = malloc(0x1000), *logger_buf = malloc(0x1000);
    int watchdog_buf = 2, len, i, offset, size, offset2, size2;
    double use;
    long int current_time, previous_time = 0x7FFFFFFF;
    struct tm *tm;
    allocations[2][0] = analyzer_buf;
    allocations[2][1] = logger_buf;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while(!finish)
    {
        /* Read from Analyzer-Printer queue */
        dequeue(queue_analyzer_printer, analyzer_buf, &len);
        /* Got data from queue, print to stdout if second changed */
        current_time = time(NULL);
        if(current_time > previous_time)
        {
            /* Print data & copy data for Logger */
            offset = 0;
            offset2 = 1;
            for(i = 0; i < cores; ++i)
            {
                sscanf(&analyzer_buf[offset], "%lf%n", &use, &size);
                printf("CPU %d - %lf%%\n", i + 1, use);
                sprintf(&logger_buf[offset2], "CPU %d - %lf%%\n%n", i + 1, use, &size2);
                offset += size;
                offset2 += size2;
            }
            /* Print time */
            tm = localtime(&current_time);
            strftime(analyzer_buf, 20, "%Y-%m-%d %H:%M:%S", tm);
            puts(analyzer_buf);
            printf("\n");
            /* Notify Logger */
            *logger_buf = 2;
            enqueue(queue_all_logger, logger_buf, offset2);
        }
        /* Set time for compare */
        previous_time = current_time;
        /* Notify Watchdog */
        enqueue(queue_all_watchdog, &watchdog_buf, sizeof(int));
        /* Wait some time */
        sleep_internal(waits[2]);
    }
    work_done[2] = 1;
    return 0;
    (void)arg;
}

static void* watchdog(void *arg)
{
    char *logger_buf = malloc(0x1000);
    int buf, i, len;
    struct timeval threads_times[4] = {{0x7FFFFFFF, 0}, {0x7FFFFFFF, 0}, {0x7FFFFFFF, 0}, {0x7FFFFFFF, 0}}, tv;
    allocations[3][0] = logger_buf;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while(!finish)
    {
        gettimeofday(&tv, NULL);
        for(i = 0; i < 4; ++i)
        {
            if(tv.tv_sec - threads_times[i].tv_sec > 2 || (tv.tv_sec - threads_times[i].tv_sec == 2 && tv.tv_usec - threads_times[i].tv_usec >= 0))
            {
                /* Program hanged */
                finish = 2;
                /* Give threads some time to react */
                sleep_internal(1);
                logger_buf[0] = 3;
                logger_buf[1] = 0;
                /* Kill Reader */
                if(!work_done[0])
                {
                    pthread_cancel(threads[0]);
                    strcpy(&logger_buf[strlen(logger_buf)], "Reader hanged!\n");
                }
                /* Kill Analyzer */
                if(!work_done[1])
                {
                    pthread_cancel(threads[1]);
                    strcpy(&logger_buf[strlen(logger_buf)], "Analyzer hanged!\n");
                }
                /* Kill Printer */
                if(!work_done[2])
                {
                    pthread_cancel(threads[2]);
                    strcpy(&logger_buf[strlen(logger_buf)], "Printer hanged!\n");
                }
                /* Check if Logger is responsive */
                if((len = queue_length(queue_all_logger)) == 0)
                {
                    /* Notify Logger */
                    enqueue(queue_all_logger, logger_buf, (int)strlen(logger_buf));
                }
                else
                {
                    /* Give Logger time to process queue */
                    sleep_internal(1);
                    if(queue_length(queue_all_logger) < len)
                    {
                        enqueue(queue_all_logger, logger_buf, (int)strlen(logger_buf));
                    }
                }
                /* Give Logger time to react */
                sleep_internal(1);
                /* Kill Logger */
                pthread_cancel(threads[4]);
                /* Terminate */
                return 0;
            }
        }
        /* Non-blocking read from queue */
        if(queue_length(queue_all_watchdog) > 0)
        {
            dequeue(queue_all_watchdog, &buf, &len);
            threads_times[buf] = tv;
        }
        /* Wait some time */
        sleep_internal(waits[3]);
    }
    return 0;
    (void)arg;
}

static void* logger(void *arg)
{
    char *buf = malloc(0x10000);
    int watchdog_buf = 3, len, fd;
    readlink("/proc/self/exe", buf, 0x10000);
    strcpy(strrchr(buf, '/'), "/logger.txt");
    fd = open(buf, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    allocations[4][0] = buf;
    files[4] = fd;
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    while(finish != 1)
    {
        /* Non-blocking read from All-Logger queue */
        if(queue_length(queue_all_watchdog) > 0)
        {
            dequeue(queue_all_logger, buf, &len);
            /* Write to file */
            switch(*buf)
            {
                case 0:
                    write(fd, "Data from Reader\n", 17);
                    break;
                case 1:
                    write(fd, "Data from Analyzer\n", 19);
                    break;
                case 2:
                    write(fd, "Data from Printer\n", 18);
                    break;
                case 3:
                    write(fd, "Data from Watchdog\n", 19);
                    break;
            }
            write(fd, buf + 1, (unsigned long)len - 1);
            write(fd, "\n\n", 2);
        }
        /* Notify Watchdog */
        enqueue(queue_all_watchdog, &watchdog_buf, sizeof(int));
        /* Wait some time */
        sleep_internal(waits[4]);
    }
    work_done[3] = 1;
    return 0;
    (void)arg;
}

int main(int argc, char *argv[])
{
    int i, j;

    /* Wait times for threads */
    srand((unsigned int)time(NULL));
    for(i = 0; i < 5; ++i)
    {
        waits[i] = rand() % 1000 / 1000.0;
    }
    /* Watchdog wait */
    waits[3] = 0.1;
    /* Logger wait */
    waits[4] = 0.1;
    /* Print waits */
    fputs("Random wait times:\n", stderr);
    for(i = 0; i < 5; ++i)
    {
        fprintf(stderr, "%lf\n", waits[i]);
    }
    fputs("\n", stderr);

    /* Number of cores */
    cores = sysconf(_SC_NPROCESSORS_ONLN);

    /* Handler for SIGTERM */
    signal(SIGTERM, handler);

    /* Queue for Reader-Analyzer */
    queue_reader_analyzer = new_queue();
    /* Queue for Analyzer-Printer */
    queue_analyzer_printer = new_queue();
    /* Queue for ALL-Watchdog */
    queue_all_watchdog = new_queue();
    /* Queue for ALL-Logger */
    queue_all_logger = new_queue();

    /* Reader */
    pthread_create(&threads[0], NULL, reader, NULL);
    /* Analyzer */
    pthread_create(&threads[1], NULL, analyzer, NULL);
    /* Printer */
    pthread_create(&threads[2], NULL, printer, NULL);
    /* Watchdog */
    pthread_create(&threads[3], NULL, watchdog, NULL);
    /* Logger */
    pthread_create(&threads[4], NULL, logger, NULL);

    /* Wait for threads */
    for(i = 0; i < 5; ++i)
    {
        pthread_join(threads[i], NULL);
    }

    if(threads[5])
    {
        pthread_join(threads[5], NULL);
    }

    /* Remove queues */
    delete_queue(queue_reader_analyzer);
    delete_queue(queue_analyzer_printer);
    delete_queue(queue_all_watchdog);
    delete_queue(queue_all_logger);

    /* Deallocate memory from heap, close files */
    for(i = 0; i < 5; ++i)
    {
        /* Deallocate memory from heap */
        for(j = 0; j < (int)(sizeof(allocations[0]) / sizeof(allocations[0][0])); ++j)
        {
            if(allocations[i][j] != NULL)
            {
                free(allocations[i][j]);
            }
        }
        /* Close files */
        if(files[i] != 0)
        {
            close(files[i]);
        }
    }

    return 0;
    (void)argc;
    (void)argv;
}
