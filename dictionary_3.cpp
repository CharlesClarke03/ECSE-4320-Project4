#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <immintrin.h>

std::mutex dictMutex;

// Thread-safe function to encode lines in batches and update the global dictionary
void encodeBatch(std::ifstream& inputFile, std::unordered_map<std::string, int>& dictionary, 
                 std::vector<int>& encodedData, int& dictID, int startLine, int endLine) {
    std::unordered_map<std::string, int> localDict;
    std::vector<int> localEncodedData;
    std::string line;
    int currentLine = 0;

    inputFile.clear();
    inputFile.seekg(0, std::ios::beg);

    while (std::getline(inputFile, line)) {
        if (currentLine < startLine) {
            currentLine++;
            continue;
        }
        if (currentLine >= endLine) break;

        int id;
        if (localDict.find(line) == localDict.end()) {
            id = dictID++;
            localDict[line] = id;
        } else {
            id = localDict[line];
        }
        localEncodedData.push_back(id);
        currentLine++;
    }

    // Update global dictionary and encoded data
    std::lock_guard<std::mutex> lock(dictMutex);
    for (const auto& [key, id] : localDict) {
        if (dictionary.find(key) == dictionary.end()) {
            dictionary[key] = id;
        }
    }
    encodedData.insert(encodedData.end(), localEncodedData.begin(), localEncodedData.end());
}

void encodeFile(const std::string& inputFilename, const std::string& outputFilename, int numThreads) {
    auto start = std::chrono::high_resolution_clock::now();

    std::ifstream inputFile(inputFilename);
    if (!inputFile) {
        std::cerr << "Error opening input file.\n";
        return;
    }

    // Calculate total number of lines in the file
    int totalLines = 0;
    std::string line;
    while (std::getline(inputFile, line)) {
        totalLines++;
    }
    
    int linesPerThread = totalLines / numThreads;
    int remainingLines = totalLines % numThreads;
    int dictID = 0;
    std::unordered_map<std::string, int> dictionary;
    std::vector<int> encodedData;
    std::vector<std::thread> threads;

    for (int i = 0; i < numThreads; ++i) {
        int startLine = i * linesPerThread;
        int endLine = startLine + linesPerThread + (i == numThreads - 1 ? remainingLines : 0);
        
        threads.emplace_back(encodeBatch, std::ref(inputFile), std::ref(dictionary), 
                             std::ref(encodedData), std::ref(dictID), startLine, endLine);
    }

    for (auto& thread : threads) {
        thread.join();
    }
    inputFile.close();

    // Write encoded data to output file
    std::ofstream outputFile(outputFilename, std::ios::binary);
    int dictSize = dictionary.size();
    outputFile.write(reinterpret_cast<char*>(&dictSize), sizeof(int));
    for (const auto& [key, id] : dictionary) {
        outputFile << key << '\0' << id << '\n';
    }
    for (int id : encodedData) {
        outputFile.write(reinterpret_cast<char*>(&id), sizeof(int));
    }
    outputFile.close();

    auto end = std::chrono::high_resolution_clock::now();
    std::cout << "Encoding completed in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms.\n";
}

bool simdPrefixMatch(const std::string& key, const std::string& prefix) {
    size_t prefixLen = prefix.size();
    if (key.size() < prefixLen) return false;

    size_t i = 0;
    __m256i prefixVec, keyVec;

    // Compare 32 characters (256 bits) at a time
    for (; i + 32 <= prefixLen; i += 32) {
        prefixVec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(prefix.data() + i));
        keyVec = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(key.data() + i));

        __m256i cmp = _mm256_cmpeq_epi8(prefixVec, keyVec);
        if (_mm256_movemask_epi8(cmp) != 0xFFFFFFFF) return false; // Not all bytes match
    }

    // Handle the remaining characters
    for (; i < prefixLen; ++i) {
        if (key[i] != prefix[i]) return false;
    }

    return true;
}

void queryFile(const std::string& encodedFilename, const std::string& query, bool usePrefix = false, bool useSIMD = false) {
    std::ifstream encodedFile(encodedFilename, std::ios::binary);
    if (!encodedFile) {
        std::cerr << "Error opening encoded file.\n";
        return;
    }

    int dictSize;
    encodedFile.read(reinterpret_cast<char*>(&dictSize), sizeof(int));
    std::unordered_map<std::string, std::vector<int>> dictionaryHash;
    std::string key;
    int id;
    for (int i = 0; i < dictSize; ++i) {
        std::getline(encodedFile, key, '\0');
        encodedFile >> id;
        encodedFile.ignore(1);
        dictionaryHash[key].push_back(id);
    }

    bool found = false;
    auto start = std::chrono::high_resolution_clock::now();
    if (usePrefix) {
        std::cout << "Prefix matches for \"" << query << "\":\n";
        for (const auto& [key, indices] : dictionaryHash) {
            bool isMatch = useSIMD ? simdPrefixMatch(key, query) : key.compare(0, query.size(), query) == 0;

            if (isMatch) {
                std::cout << "Found prefix \"" << query << "\" in " << key << " at indices: ";
                for (int index : indices) {
                    std::cout << index << " ";
                }
                std::cout << '\n';
                found = true;
            }
        }
        if (!found) std::cout << "No entries found with prefix \"" << query << "\".\n";
    } else {
        if (dictionaryHash.find(query) != dictionaryHash.end()) {
            std::cout << "Found " << query << " at indices: ";
            for (int index : dictionaryHash[query]) {
                std::cout << index << " ";
            }
            std::cout << '\n';
            found = true;
        } else {
            std::cout << "No entries found for \"" << query << "\".\n";
        }
    }
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Query completed in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " ms.\n";
}

int main(int argc, char* argv[]) {
    if (argc < 6) {
        std::cerr << "Usage: " << argv[0] << " <input file> <output file> <num threads> <SIMD on/off> <operation> [query] [prefix]\n";
        return 1;
    }

    std::string inputFilename = argv[1];
    std::string outputFilename = argv[2];
    int numThreads = std::stoi(argv[3]);
    bool useSIMD = (std::string(argv[4]) == "on");
    std::string operation = argv[5];

    if (operation == "encode") {
        encodeFile(inputFilename, outputFilename, numThreads);
    } else if (operation == "query" && argc >= 7) {
        std::string query = argv[6];
        bool usePrefix = (argc >= 8 && std::string(argv[7]) == "prefix");
        queryFile(outputFilename, query, usePrefix);
    } else {
        std::cerr << "Invalid operation or missing query for 'query' operation.\n";
        return 1;
    }

    return 0;
}
