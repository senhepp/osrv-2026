#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <cstring>
#include <thread>
#include <barrier>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <cstdint>
#include <limits.h>

using u64 = std::uint64_t;
using u8 = std::uint8_t;

struct InputData {
    std::string inputFile;
    std::string outputFile;
    size_t fileSize = 0;
    u64 x0 = 0;
    u64 a = 0;
    u64 c = 0;
    u64 m = 0;
    std::unique_ptr<u8[]> bloknot;
};

struct WorkerContext {
    std::barrier<> *barrier;
    const u8 *input;
    u8 *bloknot;
    u8 *output;
    size_t start = 0;
    size_t end = 0;
};

void generateBloknot(InputData& data) {
    u64 x = data.x0;
    auto* bloknot = data.bloknot.get();
    for (size_t i = 0; i < data.fileSize; ++i) {
        bloknot[i] = static_cast<u8>(x);
        x = (data.a * x + data.c) % data.m;
    }
}

void workerThread(WorkerContext ctx) {
    for (size_t i = ctx.start; i < ctx.end; ++i) {
        ctx.output[i] = ctx.input[i] ^ ctx.bloknot[i];
    }
    ctx.barrier->arrive_and_wait();
}

std::vector<WorkerContext> createWorkerContexts(
    size_t fileSize, 
    int numWorkers,
    const u8* input, 
    u8* bloknot, 
    u8* output,
    std::barrier<>* barrier
) {
    std::vector<WorkerContext> contexts(numWorkers);
    size_t blockSize = fileSize / numWorkers;
    
    for (int i = 0; i < numWorkers; ++i) {
        contexts[i].barrier = barrier;
        contexts[i].input = input;
        contexts[i].bloknot = bloknot;
        contexts[i].output = output;
        
        contexts[i].start = i * blockSize;
        if (i == numWorkers - 1) {
            contexts[i].end = fileSize;
        } else {
            contexts[i].end = (i + 1) * blockSize;
        }
    }
    return contexts;
}

int main(int argc, char* argv[]) {
    InputData data;
    int opt;

    while ((opt = getopt(argc, argv, "i:o:x:a:c:m:")) != -1) {
        switch (opt) {
            case 'i':
                data.inputFile = optarg;
                break;
            case 'o':
                data.outputFile = optarg;
                break;
            case 'x':
                data.x0 = std::atoll(optarg);
                break;
            case 'a':
                data.a = std::atoll(optarg);
                break;
            case 'c':
                data.c = std::atoll(optarg);
                break;
            case 'm':
                data.m = std::atoll(optarg);
                break;
            default:
                return EXIT_FAILURE;
        }
    }

    int inputFd = open(data.inputFile.c_str(), O_RDONLY);
    if (inputFd == -1) {
        perror("Ошибка открытия входного файла");
        return EXIT_FAILURE;
    }

    struct stat statbuf;
    if (fstat(inputFd, &statbuf) == -1) {
        perror("Ошибка получения размера файла");
        close(inputFd);
        return EXIT_FAILURE;
    }

    data.fileSize = statbuf.st_size;
    if (data.fileSize == 0) {
        std::cerr << "Файл пуст";
        close(inputFd);
        return EXIT_FAILURE;
    }

    u8* inputData = static_cast<u8*>(
        mmap(nullptr, data.fileSize, PROT_READ, MAP_PRIVATE, inputFd, 0)
    );
    if (inputData == MAP_FAILED) {
        perror("Ошибка mmap входного файла");
        close(inputFd);
        return EXIT_FAILURE;
    }
    close(inputFd);

    data.bloknot = std::make_unique<u8[]>(data.fileSize);
    auto outputData = std::make_unique<u8[]>(data.fileSize);
    generateBloknot(data);

    int numWorkers = std::min(static_cast<int>(get_nprocs()), 64);
    
    auto barrier = std::barrier<>(numWorkers + 1);
    auto contexts = createWorkerContexts(
        data.fileSize, numWorkers, inputData, 
        data.bloknot.get(), outputData.get(), &barrier
    );
    
    std::vector<std::thread> workers;
    workers.reserve(numWorkers);

    for (auto& ctx : contexts) {
        workers.emplace_back(workerThread, std::move(ctx));
    }

    barrier.arrive_and_wait();

    int outputFd = open(data.outputFile.c_str(), 
                       O_WRONLY | O_CREAT | O_TRUNC, 
                       S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (outputFd == -1) {
        perror("Ошибка создания выходного файла");
        munmap(inputData, data.fileSize);
        return EXIT_FAILURE;
    }

    ssize_t bytesWritten = write(outputFd, outputData.get(), data.fileSize);
    close(outputFd);

    if (bytesWritten != static_cast<ssize_t>(data.fileSize)) {
        perror("Ошибка записи в выходной файл");
        return EXIT_FAILURE;
    }

    for (auto& t : workers) {
        t.join();
    }
    
    munmap(inputData, data.fileSize);    
    return EXIT_SUCCESS;
}
