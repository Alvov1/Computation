#include <iostream>
#include <fstream>
#include <pthread.h>
#include <semaphore.h>
#include <vector>
#include <cmath>
#include <string>
#include <ratio>
#include <chrono>

/* Число, для которого ищем разложения */
unsigned Number = 0;
/* Количество потоков */
unsigned threadAmount = 0;
/* Массив, хранящий используемые потоки */
std::vector<pthread_t*> Threads;
/* Найденное количество решений */
long solutionsNumber = 0;
/* Время, потраченное на работу алгоритма */
std::chrono::high_resolution_clock::time_point timeMark;
std::chrono::duration<double, std::milli> timeSpent;

sem_t sema;

/* Общее количество слагаемых */
unsigned termsNumber = 0;
/* Сколько слагаемых считает каждый поток */
unsigned termsForThread = 0;
unsigned remainTerms = 0;

/* ------------------------------------------------------------------------------------------------------- */
const unsigned numbers[31] = {1, 1, 2, 3, 5, 7, 11, 15, 22, 30,
                              42, 56, 77, 101, 135, 176, 231, 297,
                              385, 490, 627, 792, 1002, 1255, 1575,
                              1958, 2436, 3010, 3718, 4565, 5604};
/* Находим непосредственно значение функции P(n) */
long pFromN(const int N){
    if(N < 0)
        return 0;
    if(N >= 0 && N <= 30)
        return numbers[N];
    switch(N){
        case 40:
            return 37338;
        case 50:
            return 204226;
        case 60:
            return 966467;
        default:
            /* (−1)^(q+1)(p(n − (3q^2 − q) / 2) + p(n − (3q^2 + q) / 2)) */
            long result = 0;
            for(int q = 1; 2 * N >= 3 * q * q - q; q++){
                /* Для нечетных q, добавляем. Для четных - вычитаем */
                int firstTerm = N - ((3 * q * q - q) / 2);
                int secondTerm = N - ((3 * q * q + q) / 2);
                if(q % 2 == 1){
                    result += pFromN(firstTerm);
                    result += pFromN(secondTerm);
                } else {
                    result -= pFromN(firstTerm);
                    result -= pFromN(secondTerm);
                }
            }
            return result;
    }
}

/* По номеру слагаемого в сумме, вычисляем значение его аргумента */
int argument(const unsigned N, const unsigned termNumber, const unsigned Q){
    if(termNumber % 2 == 1)
        return N - (3 * Q * Q - Q) / 2;
    else
        return N - (3 * Q * Q + Q) / 2;
}

class term{
public:
    explicit term(const unsigned number = 0, const bool add = true) : number(number), add(add){};
    unsigned number;
    bool add;
};
/* Указатель на массив с элементами term. */
typedef std::vector<term*>* ArrayPtr;
std::vector<ArrayPtr> termsArrays;

void* thread_entry(void* param){
    auto* array = (ArrayPtr) param;
    term* temp;

    long result = 0;
    for(auto & i : *array){
        temp = i;
        if(temp != nullptr)
            if(temp->add)
                result += pFromN(temp->number);
            else
                result -= pFromN(temp->number);
    }

    /* ------------------------------------------------------------------ */
    sem_wait(&sema);

    solutionsNumber += result;

    sem_post(&sema);
    /* ------------------------------------------------------------------ */

    return nullptr;
}

int threadsCreate(){
    Threads.reserve(threadAmount);

    /* Каждому потоку передаем номера слагаемых, которые ему надо вычислить.
     * К примеру, для числа 38:
     * P(38) = p(37) + p(36) - p(33) - p(31) + p(26) + p(23) - p(16) - p(12) + p(3)
     * Для 5 потоков будет следующее распределение:
     * Поток 1 - p(37) и p(23)
     * Поток 2 - p(36) и p(16)
     * Поток 3 - p(33) и p(12)
     * Поток 4 - p(31) и p(3)
     * Поток 5 - p(26)*/
    termsArrays.reserve(threadAmount);
    ArrayPtr array;

    for(unsigned threadNumber = 0; threadNumber < threadAmount; threadNumber++){
        array = new std::vector<term*>;
        /* Сколько слагаемых будет в векторе */
        const unsigned thisThreadTerms = termsForThread + (remainTerms != 0 ? 1 : 0);
        array->reserve(thisThreadTerms);

        for(unsigned i = 0; i < termsForThread; i++) {
            /* Номер слагаемого в формуле */
            const unsigned termNumber = i * threadAmount + threadNumber + 1;
            /* Значение Q для него */
            const unsigned q = termNumber / 2 + termNumber % 2;
            int arg = argument(Number, termNumber, q);

            term* temp = nullptr;
            if(arg >= 0) {
                if (q % 2 == 1)
                    temp = new term(arg, true);
                else
                    temp = new term(arg, false);
            }
            array->push_back(temp);
        }
        if(remainTerms != 0){
            /* Номер слагаемого в формуле */
            const unsigned termNumber = termsForThread * threadAmount + threadNumber + 1;
            /* Значение Q для него */
            const unsigned q = termNumber / 2 + termNumber % 2;
            const int arg = argument(Number, termNumber, q);
            term* temp = nullptr;
            if(arg >= 0) {
                if (q % 2 == 1)
                    temp = new term(arg, true);
                else
                    temp = new term(arg, false);
            }
            array->push_back(temp);
        }

        /* ------------------------------------------------------------------ */
        termsArrays.push_back(array);
//        std::cout << "Thread " << threadNumber << " - ";
//        for(auto & i : *array)
//            if(i != nullptr)
//                std::cout << "p(" << i->number << "), ";
//        std::cout << std::endl;
        /* ------------------------------------------------------------------ */

    }

    timeMark = std::chrono::high_resolution_clock::now();

    for(unsigned threadNumber = 0; threadNumber < threadAmount; threadNumber++){
        array = termsArrays[threadNumber];
        auto* temp = new pthread_t;
        if(pthread_create(temp, nullptr, thread_entry, (void*) array) != 0){
            std::cout << "Thread creation failed. " << std::endl;
            return -1;
        }
        Threads.push_back(temp);

    }
    return 0;
}
/* ------------------------------------------------------------------------------------------------------- */
/* Очищаем память, выделенную под потоки, а также память других переменных */
int threadsDelete(){
    for(unsigned i = 0; i < threadAmount; i++){
        pthread_t* temp = Threads[i];
//        pthread_exit(temp);
        pthread_join(*temp, nullptr);
        delete temp;
    }
    Threads.clear();

    term* temp;
    for(auto & termsArray : termsArrays) {
        for (auto & j : *termsArray) {
            temp = j;
            delete temp;
        }
        delete termsArray;
    }

    timeSpent = (std::chrono::high_resolution_clock::now() - timeMark);
    return 0;
}

/* Считываем аргументы из входного файла */
int readArguments(const std::string &filename = "input.txt"){
    std::ifstream input(filename);
    input >> threadAmount >> Number;
    input.close();
    if(Number < 2 || Number > 100){
        std::cout << "Error with input number" << std::endl;
        return -1;
    }
    return 0;
}
/* Выводим полученные результаты */
void writeResults(const std::string &outputName = "output.txt", const std::string &timeName = "time.txt"){
    std::ofstream output(outputName);
    output << threadAmount << std::endl << Number << std::endl;
    /* Алгоритм находит количество разложений на числа, не превосходящие N.
     * Чтобы получить количество разложений на числа, меньшие N,
     * требуется из полученного результата вычесть 1: */
    output << solutionsNumber - 1;
    output.close();
    std::ofstream time(timeName);
    time << timeSpent.count();
    time.close();
}
/* ------------------------------------------------------------------------------------------------------- */

/* Воспользуемся следующим алгоритмом:
 * https://neerc.ifmo.ru/wiki/index.php?title=Нахождение_количества_разбиений_числа_на_слагаемые.
 * Для алгоритма сложности O(n√n), для вычисления p(n) - количества разбиений числа n на слагаемые
 * используется следующая формула:
 * p(n) = p(n−1) + p(n−2) +...+ (−1)^(q+1)(p(n − (3q^2 − q)/2) + p(n − (3q^2 + q)/2)).
 * Для нахождения общего количества, требуется найти значение q.
 * Рассмотрим неравенство n - (3q^2 + q)/2 >= 0
 * 3q^2 - q - 2n >= 0, при этом и n и q - натуральные числа.
 * После решения неравенства, получаем:
 * 0 < q < (1 + sqrt(1 + 24n)) / 6.
 * Требуется найти наибольшее натуральное q, удовлетворяющее данным условиям.
 * Относительно q, в сумме имеем 2q слагаемых, которые предстоит найти.
*/

unsigned getQfromN(const unsigned N){
    double root = (1 + sqrt(1 + 24 * N)) / 6;
    return int(root);
}

int main(int argc, char** argv){
//    readArguments();
//    Number = 20;
//    threadAmount = 1;
    if(argc < 2){
        std::cout << "Arguments" << std::endl;
        return 0;
    }
    Number = std::stoi(std::string(argv[1]));
    threadAmount = std::stoi(std::string(argv[2]));

    std::cout << "Number - " << Number << std::endl;
    std::cout << threadAmount << " threads" << std::endl << std::endl;

    const unsigned Q = getQfromN(Number);
    termsNumber = Q * 2;
    termsForThread = termsNumber / threadAmount;
    remainTerms = termsNumber % threadAmount;

    sem_init(&sema, 0, 1);

    threadsCreate();
    threadsDelete();
    std::cout << solutionsNumber - 1 << std::endl;
    std::cout << "Time - " << timeSpent.count() << std::endl;
    writeResults();

    return 0;
}
