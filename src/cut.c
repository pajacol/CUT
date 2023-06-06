#include <threads.h>
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

/* Variable to notify threads to finish */
volatile int finish = 0;

/* Queues for communication between threads */
struct queue *queue_reader_analyzer;
struct queue *queue_analyzer_printer;
struct queue *queue_all_watchdog;
struct queue *queue_all_logger;

/* Number of cores */
int cores;

double cpu_use(const struct proc_stat *current, const struct proc_stat *previous)
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

void handler(int signum)
{
    finish = 1;
    return;
    (void)signum;
}

int reader(void *arg)
{
    int fd = open("/proc/stat", O_RDONLY | O_NONBLOCK);
    char *buf, *logger_buf;
    int *watchdog_buf;
    while(!finish)
    {
        /* Allocate buffers */
        buf = malloc(0x4000);
        logger_buf = malloc(0x4000);
        watchdog_buf = malloc(sizeof(int));
        /* Read whole /proc/stat */
        read_file(fd, buf);
        /* Copy for Logger */
        strcpy(logger_buf, buf);
        /* Set for Watchdog */
        *watchdog_buf = 0;
        /* Check Reader-Analyzer queue */
        if(enqueue(queue_reader_analyzer, buf))
        {
            /* Discard data, deallocate buffer */
            free(buf);
        }
        /* Notify Logger */
        if(enqueue(queue_all_logger, logger_buf))
        {
            /* Discard data, deallocate buffer */
            free(logger_buf);
        }
        /* Notify Watchdog */
        if(enqueue(queue_all_watchdog, watchdog_buf))
        {
            /* Watchdog queue full, discard data */
            free(watchdog_buf);
        }
        /* Wait some time */
        nanosleep((struct timespec[]){{0, 200000000}}, NULL);
    }
    close(fd);
    return 0;
    (void)arg;
}

int analyzer(void *arg)
{
    char *buf, *logger_buf;
    int i, offset, size, *watchdog_buf;
    /* For calculation of usage */
    struct proc_stat *current = malloc(cores * sizeof(struct proc_stat));
    struct proc_stat *previous = calloc(cores, sizeof(struct proc_stat));
    double *use = malloc(cores * sizeof(double));
    while(!finish)
    {
        while((buf = dequeue(queue_reader_analyzer)) != NULL)
        {
            /* Got data from queue */
            /* Omit first line */
            sscanf(buf, "%*s%*d%*d%*d%*d%*d%*d%*d%*d%*d%*d%n", &offset);
            for(i = 0; i < cores; ++i)
            {
                /* Read data for i-th core */
                sscanf(&buf[offset], "%*s%d%d%d%d%d%d%d%d%d%d%n", &current[i].user, &current[i].nice, &current[i].system, &current[i].idle, &current[i].iowait, &current[i].irq, &current[i].softirq, &current[i].steal, &current[i].guest, &current[i].guest_nice, &size);
                /* CPU use for i-th core */
                use[i] = cpu_use(&current[i], &previous[i]);
                /* Next line offset */
                offset += size;
                /* Copy for later calculation */
                previous[i] = current[i];
            }
            /* Deallocate buffer */
            free(buf);
            /* Allocate buffer */
            buf = malloc(0x1000);
            /* Write use of each core to buffer */
            offset = 0;
            for(i = 0; i < cores; ++i)
            {
                sprintf(&buf[offset], "%lf %n", use[i], &size);
                offset += size;
            }
            /* Allocate buffer */
            logger_buf = malloc(0x1000);
            /* Copy data for Logger */
            strcpy(logger_buf, buf);
            /* Check Analyzer-Printer queue */
            if(enqueue(queue_analyzer_printer, buf))
            {
                /* Discard data, deallocate buffer */
                free(buf);
            }
            /* Notify Logger */
            if(enqueue(queue_all_logger, logger_buf))
            {
                /* Discard data, deallocate buffer */
                free(logger_buf);
            }
        }
        /* Notify Watchdog */
        watchdog_buf = malloc(sizeof(int));
        *watchdog_buf = 1;
        if(enqueue(queue_all_watchdog, watchdog_buf))
        {
            /* Watchdog queue full, discard data */
            free(watchdog_buf);
        }
        /* Wait some time */
        nanosleep((struct timespec[]){{0, 200000000}}, NULL);
    }
    /* Deallocate memory */
    free(current);
    free(previous);
    free(use);
    return 0;
    (void)arg;
}

int printer(void *arg)
{
    char *buf, *logger_buf;
    int i, offset, size, offset2, size2, *watchdog_buf;
    double use;
    long int current_time, previous_time = 0x7FFFFFFF;
    struct tm *tm;
    while(!finish)
    {
        while((buf = dequeue(queue_analyzer_printer)) != NULL)
        {
            /* Got data from queue, print to stdout if second changed */
            current_time = time(NULL);
            if(current_time > previous_time)
            {
                /* Allocate buffer for Logger */
                logger_buf = malloc(0x1000);
                /* Print data */
                offset = 0;
                offset2 = 0;
                for(i = 0; i < cores; ++i)
                {
                    sscanf(&buf[offset], "%lf%n", &use, &size);
                    printf("CPU %d - %lf%%\n", i + 1, use);
                    sprintf(&logger_buf[offset2], "CPU %d - %lf%%\n%n", i + 1, use, &size2);
                    offset += size;
                    offset2 += size2;
                }
                /* Notify Logger */
                if(enqueue(queue_all_logger, logger_buf))
                {
                    /* Discard data, deallocate buffer */
                    free(logger_buf);
                }
                /* Print time */
                tm = localtime(&current_time);
                strftime(buf, 20, "%Y-%m-%d %H:%M:%S", tm);
                puts(buf);
                printf("\n");
            }
            /* Deallocate buffer */
            free(buf);
            /* Set time for compare */
            previous_time = current_time;
        }
        /* Notify Watchdog */
        watchdog_buf = malloc(sizeof(int));
        *watchdog_buf = 2;
        if(enqueue(queue_all_watchdog, watchdog_buf))
        {
            /* Watchdog queue full, discard data */
            free(watchdog_buf);
        }
        /* Wait some time */
        nanosleep((struct timespec[]){{0, 200000000}}, NULL);
    }
    return 0;
    (void)arg;
}

int watchdog(void *arg)
{
    int *buf, i;
    struct timeval threads[4] = {{0x7FFFFFFF, 0}, {0x7FFFFFFF, 0}, {0x7FFFFFFF, 0}, {0x7FFFFFFF, 0}};
    struct timeval tv;
    while(!finish)
    {
        gettimeofday(&tv, NULL);
        for(i = 0; i < 4; ++i)
        {
            if(tv.tv_sec - threads[i].tv_sec > 2 || (tv.tv_sec - threads[i].tv_sec == 2 && tv.tv_usec - threads[i].tv_usec >= 0))
            {
                /* Program hanged */
                fputs("Program hanged!\n", stderr);
                exit(1);
            }
        }
        buf = dequeue(queue_all_watchdog);
        if(buf != NULL)
        {
            /* Got data from queue */
            threads[*buf] = tv;
            /* Deallocate buffer */
            free(buf);
        }
        /* Wait 20 ms */
        nanosleep((struct timespec[]){{0, 20000000}}, NULL);
    }
    return 0;
    (void)arg;
}

int logger(void *arg)
{
    char *buf = malloc(0x1000);
    int len, fd, *watchdog_buf;
    readlink("/proc/self/exe", buf, 0x1000);
    strcpy(strrchr(buf, '/'), "/logger.txt");
    fd = open(buf, O_WRONLY | O_TRUNC | O_CREAT, 0644);
    free(buf);
    while(!finish)
    {
        while((buf = dequeue(queue_all_logger)) != NULL)
        {
            /* Got data from queue */
            len = strlen(buf);
            /* Write to file */
            write(fd, buf, len);
            write(fd, "\n\n", 2);
            /* Deallocate buffer */
            free(buf);
        }
        /* Notify Watchdog */
        watchdog_buf = malloc(sizeof(int));
        *watchdog_buf = 3;
        if(enqueue(queue_all_watchdog, watchdog_buf))
        {
            /* Watchdog queue full, discard data */
            free(watchdog_buf);
        }
        /* Wait 50 ms */
        nanosleep((struct timespec[]){{0, 50000000}}, NULL);
    }
    close(fd);
    return 0;
    (void)arg;
}

int main(int argc, char *argv[])
{
    int i;
    thrd_t threads[5];

    /* Number of cores */
    cores = sysconf(_SC_NPROCESSORS_ONLN);

    /* Handler for SIGTERM */
    signal(SIGINT, handler);

    /* Queue for Reader-Analyzer */
    queue_reader_analyzer = new_queue();
    /* Queue for Analyzer-Printer */
    queue_analyzer_printer = new_queue();
    /* Queue for ALL-Watchdog */
    queue_all_watchdog = new_queue();
    /* Queue for ALL-Logger */
    queue_all_logger = new_queue();

    /* Reader */
    thrd_create(&threads[0], reader, NULL);
    /* Analyzer */
    thrd_create(&threads[1], analyzer, NULL);
    /* Printer */
    thrd_create(&threads[2], printer, NULL);
    /* Watchdog */
    thrd_create(&threads[3], watchdog, NULL);
    /* Logger */
    thrd_create(&threads[4], logger, NULL);

    /* Wait for threads */
    for(i = 0; i < 5; ++i)
    {
        thrd_join(threads[i], NULL);
    }

    /* Remove queues */
    delete_queue(queue_reader_analyzer);
    delete_queue(queue_analyzer_printer);
    delete_queue(queue_all_watchdog);
    delete_queue(queue_all_logger);

    return 0;
    (void)argc;
    (void)argv;
}
