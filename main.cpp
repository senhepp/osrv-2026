#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <sys/mman.h>

#include <string.h>
#include <fcntl.h>
#include <limits.h>

struct InputData {
    char inputFile[PATH_MAX + 1];
    char outputFile[PATH_MAX + 1];

    size_t size;

    uint64_t x0;
    uint64_t a;
    uint64_t c;
    uint64_t m;

    char *bloknot;
};

struct Context {
    pthread_t id,
    pthread_barrier_t* barrier;

    size_t start{};
    size_t end{};
    size_t length{};
    char*  bloknot{};
    char*  input{};
    char*  output{};
};

int getRand(int *arg) {
    auto* inp = static_cast<InputData*>(arg);
    uint64_t x = inp->x0;
    uint64_t a = inp->a;
    uint64_t c = inp->c;
    uint64_t m = inp->m;
    for (size_t i = 0; i < inp->size; ++i) {
        inp->bloknot[i] = static_cast<char>(x);
        x = (a * x + c) % m;
    }
    return nullptr;
}

void *worker(void *arg) {
    auto* ctx = static_cast<Context*>(arg);
    for (size_t i = ctx->start; i < ctx->end; ++i) {
        ctx->output[i] = ctx->input[i] ^ ctx->bloknot[i];
    }
    pthread_barrier_wait(ctx->barrier);
    return nullptr;
}

int main(int argc, char *argv[])
{
    // otp  -i /path/to/text.txt -o -/path/to/cypher.txt -x 4212 -a 84589 -c 45989 -m 217728

    InputData inpData;
    int opt;

    while ((opt = getopt(argc, argv, "i:o:x:a:c:m:")) != -1) {
        switch (opt) {
        case 'i':
            inpData.inputFile = optarg;
            break;
        case 'o':
            inpData.outFile = optarg;
            break;
        case 'x':
            inpData.x0 = atoi(optarg);
            break;
        case 'a':
            inpData.a = atoi(optarg);
            break;
        case 'c':
            inpData.c = atoi(optarg);
            break;
        case 'm':
            inpData.m = atoi(optarg);
            break;
        default:
            fprintf(stderr, "no valid argument\n");
            return nullptr;
        }
    }

    int ifd;
    int ofd;    

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    unsigned char *buffer = (unsigned char*)malloc(fileSize);
    if (buffer == NULL) {
        perror("error allocating memory");
        return EXIT_FAILURE;
    }

    size_t bytesRead = fread(buffer, 1, fileSize, file);
    if (bytesRead != fileSize) {
        perror("error reading file");
        return EXIT_FAILURE;
    }

    free(buffer);

    if ((ifd = open(InputData.inputFile, O_RDONLY)) == -1) {
        perror("input file");
        return EXIT_FAILURE;
    }
    if ((ofd = open(InputData.ofile, O_WRONLY | O_CREAT, S_IREAD | S_IWRITE)) == -1) {
        perror("output file");
        return EXIT_FAILURE;
    }

    struct stat istat;
    if (fstat(ifd, &istat) == -1) {
        perror("input file");
        return EXIT_FAILURE;
    }
    inpData.size = (size_t)istat.st_size;

    char *input = mmap(NULL, InputData.size, PROT_READ, MAP_PRIVATE, ifd, 0);
    if (input == MAP_FAILED) {
        perror("input file");
        return EXIT_FAILURE;
    }
    char *output = calloc(sizeof(char), inpData.size);

    close(ifd);

    if ((inpData.bloknot = calloc(sizeof(char), inpData.size)) == NULL) {
        perror("bloknot error");
        return EXIT_FAILURE;
    }

    pthread_t th_getRand;
    if (pthread_create(&th_getRand, NULL, getRand, (void*) &inpData) != 0) {
        return EXIT_FAILURE;
    }
    pthread_join(th_lcg_gen, NULL);

    int workers_count = get_nprocs();
    Context **workers = calloc(sizeof(Context*), workers_count);
    size_t avg_block_len = inpData.size / workers_count; 

    pthread_barrier_t barrier;
    if (pthread_barrier_init(&barrier, NULL, workers_count + 1) != 0) {
        return EXIT_FAILURE;
    }

    for (int i = 0; i < workers_count; i++) {
        workers[i] = calloc(sizeof(Context), 1);
        workers[i]->barrier = &barrier;
        workers[i]->bloknot = inpData.bloknot;
        workers[i]->input = input;
        workers[i]->output = output;
        workers[i]->start = i * avg_block_len;
        if (i == workers_count - 1) {
            workers[i]->end = inpData.size;
        } else {
            workers[i]->end = (i + 1) * avg_block_len;
        }
        workers[i]->length = workers[i]->end - workers[i]->start;
        pthread_create(&workers[i]->id, NULL, worker, (void*)workers[i]);
    }

    pthread_barrier_wait(&barrier);
    pthread_barrier_destroy(&barrier);
    munmap(input, inpData.size);

    for (int i = 0; i < workers_count; i++) {
        free(workers[i]);
    }
    free(workers);
    free(inpData.bloknot);

    write(ofd, output, inpData.size);
    close(ofd);
    free(output);
}
