#include <iostream>
#include <fstream>
#include <string>
#include <chrono>

void searchInFile(const std::string& filePath, const std::string& searchString) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cerr << "Error: File not found." << std::endl;
        return;
    }

    std::string line;
    int lineNumber = 0;
    bool found = false;

    // Start timing
    auto start = std::chrono::high_resolution_clock::now();

    while (std::getline(file, line)) {
        lineNumber++;
        if (line == searchString) {  // Check for exact match
            std::cout << "String found at line " << lineNumber << ": " << line << std::endl;
            found = true;
            break;
        }
    }

    // Stop timing
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;

    if (!found) {
        std::cout << "String not found in the file." << std::endl;
    }
    std::cout << "Time taken to search: " << elapsed.count() << " seconds" << std::endl;
}

int main() {
    std::string filePath;
    std::string searchString;

    std::cout << "Enter the file path: ";
    std::getline(std::cin, filePath);

    std::cout << "Enter the string to search: ";
    std::getline(std::cin, searchString);

    searchInFile(filePath, searchString);

    return 0;
}
