#include <Windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm> // Add this header for std::transform
#include <fstream>
#include <iomanip>
#include <cstring>
#include <dirent.h>
#include <unordered_set>
#include <sys/stat.h>
#ifdef _WIN32

#else
#include <dirent.h>
#endif

std::unordered_set<std::string> uniqueFiles;
CRITICAL_SECTION cs; // Critical section to synchronize access to 'uniqueFiles'

void deleteDuplicates(const std::string &directory);
struct ThreadParams
{
    std::wstring directory;
    std::unordered_map<std::wstring, ULONGLONG> &pathSpaceUsage;
    std::unordered_map<std::wstring, ULONGLONG> &fileTypeSpaceUsage;
};
struct ThreadParam
{
    std::string directory;
    HANDLE hThread;
};
struct FileInfo
{
    std::string path;
    std::string hash;
    uintmax_t size;
};
bool isRegularFile(const std::string &path)
{
    DWORD fileAttributes = GetFileAttributes(path.c_str());
    return (fileAttributes != INVALID_FILE_ATTRIBUTES) && !(fileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}
int countFiles(const std::string &directory)
{
    int fileCount = 0;
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile((directory + "\\*").c_str(), &findFileData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::string name = findFileData.cFileName;
            if (name != "." && name != ".." && (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                fileCount++;
            }
        } while (FindNextFile(hFind, &findFileData) != 0);

        FindClose(hFind);
    }
    return fileCount;
}
void deleteExcessFiles2(const std::string &directory, int maxFiles)
{
    std::vector<std::string> filesToDelete;

    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile((directory + "\\*").c_str(), &findFileData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::string name = findFileData.cFileName;
            if (name != "." && name != ".." && (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            {
                filesToDelete.push_back(name);
            }
        } while (FindNextFile(hFind, &findFileData) != 0);

        FindClose(hFind);
    }

    int fileCount = filesToDelete.size();
    if (fileCount > maxFiles)
    {
        int filesToDeleteCount = fileCount - maxFiles;
        std::sort(filesToDelete.begin(), filesToDelete.end());

        for (int i = 0; i < filesToDeleteCount; ++i)
        {
            std::string filePath = directory + "\\" + filesToDelete[i];
            if (isRegularFile(filePath))
            {
                if (DeleteFile(filePath.c_str()))
                {
                    std::cout << "Deleted file: " << filePath << std::endl;
                }
                else
                {
                    std::cerr << "Error deleting file: " << filePath << std::endl;
                }
            }
        }
    }
}
DWORD WINAPI processFilesDeepDownThread2(LPVOID lpParam)
{
    const std::pair<std::string, int> *params = static_cast<std::pair<std::string, int> *>(lpParam);
    std::string directory = params->first;
    int maxFiles = params->second;
    delete params;

    deleteExcessFiles2(directory, maxFiles);

    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile((directory + "\\*").c_str(), &findFileData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::string name = findFileData.cFileName;
            if (name != "." && name != ".." && (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::string subdirPath = directory + "\\" + name;

                // Create a new thread for the subdirectory
                std::pair<std::string, int> *subParams = new std::pair<std::string, int>(subdirPath, maxFiles);
                HANDLE hThread = CreateThread(NULL, 0, processFilesDeepDownThread2, subParams, 0, NULL);
                if (hThread)
                {
                    CloseHandle(hThread); // Close the thread handle to avoid resource leak
                }
            }
        } while (FindNextFile(hFind, &findFileData) != 0);

        FindClose(hFind);
    }

    return 0;
}
void processFilesDeepDown(const std::string &directory, int maxFiles)
{
    deleteExcessFiles2(directory, maxFiles);

    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile((directory + "\\*").c_str(), &findFileData);
    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::string name = findFileData.cFileName;
            if (name != "." && name != ".." && (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::string subdirPath = directory + "\\" + name;

                // Create a new thread for each subdirectory
                std::pair<std::string, int> *params = new std::pair<std::string, int>(subdirPath, maxFiles);
                HANDLE hThread = CreateThread(NULL, 0, processFilesDeepDownThread2, params, 0, NULL);
                if (hThread)
                {
                    WaitForSingleObject(hThread, INFINITE); // Wait for the subdirectory thread to finish
                    CloseHandle(hThread);
                }
            }
        } while (FindNextFile(hFind, &findFileData) != 0);

        FindClose(hFind);
    }
}

int64_t getFileSize(const std::string &path)
{
    WIN32_FILE_ATTRIBUTE_DATA fileData;
    if (GetFileAttributesEx(path.c_str(), GetFileExInfoStandard, &fileData))
    {
        LARGE_INTEGER fileSize;
        fileSize.HighPart = fileData.nFileSizeHigh;
        fileSize.LowPart = fileData.nFileSizeLow;
        return static_cast<int64_t>(fileSize.QuadPart);
    }
    return -1;
}
void deleteExcessFiles(const std::string &directory, int64_t thresholdSize)
{
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile((directory + "\\*").c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Error opening directory." << std::endl;
        return;
    }

    std::vector<std::string> filesToDelete;

    do
    {
        std::string name = findFileData.cFileName;
        if (name != "." && name != "..")
        {
            std::string filePath = directory + "\\" + name;
            int64_t fileSize = getFileSize(filePath);
            if (fileSize > thresholdSize)
            {
                filesToDelete.push_back(name);
            }
        }
    } while (FindNextFile(hFind, &findFileData) != 0);

    FindClose(hFind);

    for (const std::string &fileName : filesToDelete)
    {
        std::string filePath = directory + "\\" + fileName;
        if (isRegularFile(filePath))
        {
            if (DeleteFile(filePath.c_str()))
            {
                std::cout << "Deleted file: " << filePath << std::endl;
            }
            else
            {
                std::cerr << "Error deleting file: " << filePath << std::endl;
            }
        }
    }
}

DWORD WINAPI processFilesDeepDownThread(LPVOID lpParam)
{
    std::string fullPath = *static_cast<std::string *>(lpParam);
    delete static_cast<std::string *>(lpParam);

    int64_t thresholdSize = 1024 * 1024 * 1024; // 1 GB.
    deleteExcessFiles(fullPath, thresholdSize);

    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile((fullPath + "\\*").c_str(), &findFileData);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Error opening directory: " << fullPath << std::endl;
        return 0;
    }

    std::vector<HANDLE> threads;

    do
    {
        std::string name = findFileData.cFileName;
        if (name != "." && name != "..")
        {
            std::string subFullPath = fullPath + "\\" + name;

            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                // Create a new thread to process each subdirectory
                std::string *param = new std::string(subFullPath);
                HANDLE hThread = CreateThread(NULL, 0, processFilesDeepDownThread, param, 0, NULL);
                if (hThread)
                {
                    threads.push_back(hThread);
                }
            }
            else if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_NORMAL)
            {
                int64_t fileSize = getFileSize(subFullPath);
                if (fileSize > thresholdSize)
                {
                    if (isRegularFile(subFullPath))
                    {
                        if (DeleteFile(subFullPath.c_str()))
                        {
                            std::cout << "Deleted file: " << subFullPath << std::endl;
                        }
                        else
                        {
                            std::cerr << "Error deleting file: " << subFullPath << std::endl;
                        }
                    }
                }
            }
        }
    } while (FindNextFile(hFind, &findFileData) != 0);

    FindClose(hFind);

    // Wait for all threads to finish
    WaitForMultipleObjects(static_cast<DWORD>(threads.size()), threads.data(), TRUE, INFINITE);

    // Close thread handles
    for (HANDLE hThread : threads)
    {
        CloseHandle(hThread);
    }

    return 0;
}

DWORD WINAPI deleteDuplicatesThread(LPVOID lpParam)
{
    ThreadParam *param = static_cast<ThreadParam *>(lpParam);
    deleteDuplicates(param->directory);
    CloseHandle(param->hThread); // Close the thread handle once the thread is done
    delete param;
    return 0;
}
bool hasFileExtension(const std::wstring &filePath, const std::wstring &extension)
{
    size_t pos = filePath.find_last_of(L".");
    if (pos != std::wstring::npos)
    {
        std::wstring ext = filePath.substr(pos + 1);
        std::wstring lowercaseExt = ext;
        std::transform(lowercaseExt.begin(), lowercaseExt.end(), lowercaseExt.begin(), ::tolower);
        std::wstring lowercaseExtension = extension;
        std::transform(lowercaseExtension.begin(), lowercaseExtension.end(), lowercaseExtension.begin(), ::tolower);
        return lowercaseExt == lowercaseExtension;
    }
    return false;
}

void ScanFilesRecursive(const std::wstring &dirPath, const std::vector<std::wstring> &fileExtensionsToScan, bool &foundFiles)
{
    WIN32_FIND_DATAW findFileData;
    HANDLE hFind = FindFirstFileW((dirPath + L"\\*").c_str(), &findFileData);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::wstring filePath = dirPath + L"\\" + findFileData.cFileName;

                // Check if the file has any of the specified extensions and print if matched
                for (const auto &extension : fileExtensionsToScan)
                {
                    if (hasFileExtension(findFileData.cFileName, extension))
                    {
                        std::wcout << L"Found file with extension " << extension << L": " << filePath << std::endl;
                        foundFiles = true; // Set the flag to true if a file with the specified extension is found
                        break;             // Move to the next file after finding one matching extension
                    }
                }
            }
            else if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && wcscmp(findFileData.cFileName, L".") != 0 && wcscmp(findFileData.cFileName, L"..") != 0)
            {
                std::wstring subDirectory = dirPath + L"\\" + findFileData.cFileName;
                ScanFilesRecursive(subDirectory, fileExtensionsToScan, foundFiles);
            }
        } while (FindNextFileW(hFind, &findFileData) != 0);

        if (GetLastError() != ERROR_NO_MORE_FILES)
        {
            std::wcerr << L"Error occurred while scanning files: " << GetLastError() << std::endl;
        }

        FindClose(hFind);
    }
    else
    {
        std::wcerr << L"Unable to open directory: " << dirPath << std::endl;
    }
}

DWORD WINAPI ScanFilesThread(LPVOID lpParam)
{
    std::wstring dirPath = *static_cast<std::wstring *>(lpParam);
    delete static_cast<std::wstring *>(lpParam);

    std::vector<std::wstring> fileExtensionsToScan;
    std::wstring userInput;
    std::wcin.ignore();
    std::wcout << L"Enter file extensions to scan in " << dirPath << L" (separated by spaces, e.g., 'mp4 jpg avi'): ";
    std::getline(std::wcin, userInput);

    // Parse the user input and store the file extensions in the vector
    size_t startPos = 0;
    size_t endPos = 0;
    while ((endPos = userInput.find(L' ', startPos)) != std::wstring::npos)
    {
        fileExtensionsToScan.push_back(userInput.substr(startPos, endPos - startPos));
        startPos = endPos + 1;
    }
    fileExtensionsToScan.push_back(userInput.substr(startPos)); // Add the last extension

    bool foundFiles = false; // Flag to track if files with specified extensions were found
    ScanFilesRecursive(dirPath, fileExtensionsToScan, foundFiles);

    if (!foundFiles) // Display the message if no files with specified extensions are found
    {
        std::wcout << L"Sorry, no files with the specified extension(s) exist in this drive." << std::endl;
    }

    return 0;
}

bool isDirectory(const std::string &path)
{
    DWORD attrib = GetFileAttributesA(path.c_str());
    return (attrib != INVALID_FILE_ATTRIBUTES) && (attrib & FILE_ATTRIBUTE_DIRECTORY);
}

void deleteExcessSubdirectories(const std::string &directory, int maxSubdirectories)
{
    std::vector<std::string> subdirsToDelete;

    DIR *dir = opendir(directory.c_str());
    if (!dir)
    {
        std::cerr << "Error opening directory." << std::endl;
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != nullptr)
    {
        std::string name = entry->d_name;
        if (name != "." && name != ".." && isDirectory(directory + "/" + name))
        {
            subdirsToDelete.push_back(name);
        }
    }
    closedir(dir);

    int subdirCount = subdirsToDelete.size();
    if (subdirCount > maxSubdirectories)
    {
        int subdirsToDeleteCount = subdirCount - maxSubdirectories;
        std::sort(subdirsToDelete.begin(), subdirsToDelete.end());

        for (int i = 0; i < subdirsToDeleteCount; ++i)
        {
            std::string subdirPath = directory + "/" + subdirsToDelete[i];
            deleteExcessSubdirectories(subdirPath, maxSubdirectories); // Recursively process subdirectory
            RemoveDirectory(subdirPath.c_str());                       // Delete the directory (non-empty directories will also be deleted)
            std::cout << "Deleted directory: " << subdirPath << std::endl;
        }
    }
}

DWORD WINAPI processSubdirectoriesDeepDownThread(LPVOID lpParam)
{
    const auto params = static_cast<std::pair<std::string, int> *>(lpParam);
    const std::string directory = params->first;
    const int maxSubdirectories = params->second;
    delete params;

    deleteExcessSubdirectories(directory, maxSubdirectories);

    DIR *dir = opendir(directory.c_str());
    if (dir)
    {
        struct dirent *entry;
        while ((entry = readdir(dir)) != nullptr)
        {
            std::string name = entry->d_name;
            if (name != "." && name != ".." && isDirectory(directory + "/" + name))
            {
                std::string subdirPath = directory + "/" + name;

                // Create a new thread for each subdirectory
                std::pair<std::string, int> *subParams = new std::pair<std::string, int>(subdirPath, maxSubdirectories);
                HANDLE hThread = CreateThread(NULL, 0, processSubdirectoriesDeepDownThread, subParams, 0, NULL);
                if (hThread)
                {
                    // Close the thread handle to avoid resource leak
                    CloseHandle(hThread);
                }
            }
        }
        closedir(dir);
    }

    return 0;
}

// Helper function to check if a file is a video file
bool isVideoFile(const std::wstring &filePath)
{
    return hasFileExtension(filePath, L"mp4") || hasFileExtension(filePath, L"avi") || hasFileExtension(filePath, L"mkv");
}

// Helper function to check if a file is an image file
bool isImageFile(const std::wstring &filePath)
{
    return hasFileExtension(filePath, L"jpg") || hasFileExtension(filePath, L"png") || hasFileExtension(filePath, L"gif");
}
std::wstring getFileExtension(const std::wstring &fileName)
{
    size_t dotPos = fileName.find_last_of(L".");
    if (dotPos != std::wstring::npos && dotPos < fileName.length() - 1)
    {
        return fileName.substr(dotPos + 1);
    }
    return L"";
}
// Function to calculate space utilization for a given directory
DWORD WINAPI calculateSpaceUtilization(LPVOID lpParam)
{
    ThreadParams *params = static_cast<ThreadParams *>(lpParam);
    std::wstring directory = params->directory;

    WIN32_FIND_DATAW findFileData;
    HANDLE hFind = FindFirstFileW((directory + L"\\*").c_str(), &findFileData);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                ULONGLONG fileSize = (static_cast<ULONGLONG>(findFileData.nFileSizeHigh)
                        << (sizeof(findFileData.nFileSizeLow) * 8)) +
                                     findFileData.nFileSizeLow;

                // Calculate space utilization for paths
                std::wstring path = directory + L"\\" + findFileData.cFileName;
                params->pathSpaceUsage[path] += fileSize;

                // Calculate space utilization for file types (using extensions)
                std::wstring fileExtension = getFileExtension(findFileData.cFileName);
                params->fileTypeSpaceUsage[fileExtension] += fileSize;
            }
        } while (FindNextFileW(hFind, &findFileData) != 0);

        FindClose(hFind);
    }
}

void processDirectory(const std::wstring &currentPath, const std::vector<std::wstring> &userExtensions, std::unordered_map<std::wstring, ULONGLONG> &fileTypeSpaceUsage)
{
    std::wstring path = currentPath + L"\\*";

    WIN32_FIND_DATAW findFileData;
    HANDLE hFind = FindFirstFileW(path.c_str(), &findFileData);

    if (hFind != INVALID_HANDLE_VALUE)
    {
        do
        {
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                if (wcscmp(findFileData.cFileName, L".") != 0 && wcscmp(findFileData.cFileName, L"..") != 0)
                {
                    std::wstring subPath = currentPath + L"\\" + findFileData.cFileName;
                    processDirectory(subPath, userExtensions, fileTypeSpaceUsage);
                }
            }
            else
            {
                ULONGLONG fileSize = (static_cast<ULONGLONG>(findFileData.nFileSizeHigh) << (sizeof(findFileData.nFileSizeLow) * 8)) + findFileData.nFileSizeLow;

                // Calculate space utilization for file types
                bool isFileTypeIncluded = false;
                for (const auto &ext : userExtensions)
                {
                    if (hasFileExtension(findFileData.cFileName, ext))
                    {
                        fileTypeSpaceUsage[currentPath] += fileSize;
                        isFileTypeIncluded = true;
                        break;
                    }
                }

                // If the file doesn't match any specified extensions, categorize it as "other"
                if (!isFileTypeIncluded)
                {
                    fileTypeSpaceUsage[currentPath] += fileSize;
                }
            }
        } while (FindNextFileW(hFind, &findFileData) != 0);

        FindClose(hFind);
    }
}

DWORD WINAPI DeleteFileThread(LPVOID lpParam)
{
    std::wstring filePath = *static_cast<std::wstring *>(lpParam);
    delete static_cast<std::wstring *>(lpParam);

    if (DeleteFileW(filePath.c_str()))
    {
        std::wcout << "Deleted: " << filePath << std::endl;
    }
    else
    {
        std::wcout << "Failed to delete: " << filePath << std::endl;
    }

    return 0;
}

void deleteDuplicates(const std::string &directory)
{
    std::string searchPath = directory + "\\*.*";
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(searchPath.c_str(), &findFileData);

    if (hFind == INVALID_HANDLE_VALUE)
    {
        std::cerr << "Error: Could not open directory " << directory << std::endl;
        return;
    }

    std::vector<HANDLE> threadHandles; // Store handles of threads created for subdirectories

    do
    {
        if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (strcmp(findFileData.cFileName, ".") != 0 && strcmp(findFileData.cFileName, "..") != 0)
            {
                std::string subDir = directory + "\\" + findFileData.cFileName;
                ThreadParam *param = new ThreadParam;
                param->directory = subDir;
                param->hThread = CreateThread(NULL, 0, deleteDuplicatesThread, param, 0, NULL);
                if (param->hThread == NULL)
                {
                    std::cerr << "Error: Could not create thread for " << subDir << std::endl;
                    delete param;
                }
                else
                {
                    threadHandles.push_back(param->hThread);
                }
            }
        }
        else
        {
            std::string filePath = directory + "\\" + findFileData.cFileName;
            HANDLE hFile = CreateFile(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE)
            {
                DWORD fileSize = GetFileSize(hFile, nullptr);
                if (fileSize != INVALID_FILE_SIZE)
                {
                    char *fileData = new char[fileSize];
                    DWORD bytesRead;
                    if (ReadFile(hFile, fileData, fileSize, &bytesRead, nullptr) && bytesRead == fileSize)
                    {
                        // Calculate a unique hash for each file
                        std::string contents(fileData, fileData + fileSize);
                        EnterCriticalSection(&cs); // Enter critical section before accessing 'uniqueFiles'
                        std::hash<std::string> hasher;
                        size_t fileHash = hasher(contents);

                        // Check if the file's content hash has been seen before
                        if (uniqueFiles.find(contents) != uniqueFiles.end())
                        {
                            // Delete the duplicate file
                            std::cout << "Deleting duplicate file: " << filePath << std::endl;
                            CloseHandle(hFile);
                            DeleteFile(filePath.c_str());
                        }
                        else
                        {
                            // Store the unique file hash
                            uniqueFiles.insert(contents);
                        }
                        LeaveCriticalSection(&cs); // Leave critical section after accessing 'uniqueFiles'
                    }
                    delete[] fileData;
                }
                CloseHandle(hFile);
            }
            else
            {
                std::cerr << "Error: Could not read file " << filePath << std::endl;
            }
        }
    } while (FindNextFile(hFind, &findFileData) != 0);

    FindClose(hFind);

    // Wait for all thread handles to finish before continuing
    if (!threadHandles.empty())
    {
        WaitForMultipleObjects(static_cast<DWORD>(threadHandles.size()), &threadHandles[0], TRUE, INFINITE);
    }
}

class MenuDrivenProgram
{
private:
    // Structure to store file information
    struct FileInfo
    {
        std::string path;
        std::string hash;
        uintmax_t size;
    };

    static std::wstring formatBytes(ULONGLONG bytes)
    {
        const wchar_t *units[] = {L"Bytes", L"KB", L"MB", L"GB", L"TB", L"PB", L"EB", L"ZB", L"YB"};
        int unitIndex = 0;
        double size = static_cast<double>(bytes);

        while (size >= 1024 && unitIndex < sizeof(units) / sizeof(units[0]) - 1)
        {
            size /= 1024;
            unitIndex++;
        }

        wchar_t buffer[100];
        swprintf(buffer, sizeof(buffer), L"%.2f %s", size, units[unitIndex]);
        return buffer;
    }

    static DWORD WINAPI ScanFilesThread2(LPVOID lpParam)
    {
        std::wstring dirPath = *static_cast<std::wstring *>(lpParam);
        delete static_cast<std::wstring *>(lpParam);

        uintmax_t thresholdSize = 1024 * 1024 * 1024; // 1GB
        IdentifyLargeFiles(dirPath, thresholdSize);

        return 0;
    }
    static void IdentifyLargeFiles(const std::wstring &dirPath, uintmax_t thresholdSize)
    {
        WIN32_FIND_DATAW findData;
        HANDLE hFind = FindFirstFileW((dirPath + L"\\*").c_str(), &findData);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            std::wcout << L"Error opening directory." << std::endl;
            return;
        }

        do
        {
            if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
                continue;

            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                std::wstring subDirPath = dirPath + L"\\" + findData.cFileName;

                // Create a thread to scan the subdirectory
                HANDLE hThread = CreateThread(NULL, 0, ScanFilesThread2, new std::wstring(subDirPath), 0, NULL);
                if (hThread)
                {
                    WaitForSingleObject(hThread, INFINITE);
                    CloseHandle(hThread);
                }
                else
                {
                    std::wcerr << L"Error creating thread for subdirectory: " << subDirPath << std::endl;
                }
            }
            else
            {
                uintmax_t fileSize = static_cast<uintmax_t>(findData.nFileSizeHigh) << (sizeof(DWORD) * 8) | findData.nFileSizeLow;
                if (fileSize > thresholdSize)
                {
                    std::wcout << L"Large File: " << dirPath << L"\\" << findData.cFileName << L" (" << formatBytes(fileSize) << L")" << std::endl;
                }
            }

        } while (FindNextFileW(hFind, &findData) != 0);

        FindClose(hFind);
    }

    bool isDirectory(const std::string &path)
    {
        DWORD attrib = GetFileAttributesA(path.c_str());
        return (attrib != INVALID_FILE_ATTRIBUTES) && (attrib & FILE_ATTRIBUTE_DIRECTORY);
    }

    int countSubdirectories(const std::string &directory)
    {
        int subdirCount = 0;
        DIR *dir = opendir(directory.c_str());
        if (dir)
        {
            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr)
            {
                std::string name = entry->d_name;
                if (name != "." && name != ".." && isDirectory(directory + "/" + name))
                {
                    subdirCount++;
                }
            }
            closedir(dir);
        }
        return subdirCount;
    }

    // Function to calculate the hash of a file
    std::string calculateFileHash(const std::string &filePath)
    {
        std::ifstream file(filePath, std::ios::binary);
        if (!file)
        {
            return "";
        }

        constexpr size_t bufferSize = 8192;
        char buffer[bufferSize];
        std::hash<std::string> hasher;

        std::string hashResult;
        while (file.read(buffer, bufferSize))
        {
            hashResult += hasher(std::string(buffer, buffer + file.gcount()));
        }

        if (file.gcount() > 0)
        {
            hashResult += hasher(std::string(buffer, buffer + file.gcount()));
        }

        return hashResult;
    }

    //    void findDuplicateFilesRecursive(const std::string &directoryPath, std::unordered_map<std::string, std::vector<FileInfo>> &filesByHash);
    void FindDuplicateFilesThread(const std::string &dirPath)
    {
        std::unordered_map<std::string, std::vector<FileInfo>> filesByHash;
        findDuplicateFilesRecursive(dirPath, filesByHash);

        std::vector<std::vector<FileInfo>> duplicateFiles;
        duplicateFiles.reserve(filesByHash.size());

        // Remove non-duplicates from the map
        for (auto it = filesByHash.begin(); it != filesByHash.end();)
        {
            if (it->second.size() < 2)
            {
                it = filesByHash.erase(it);
            }
            else
            {
                duplicateFiles.push_back(std::move(it->second));
                ++it;
            }
        }

        // Print duplicate file information
        if (duplicateFiles.empty())
        {
            std::cout << "No duplicate files found in " << dirPath << std::endl;
        }
        else
        {
            std::cout << "Duplicate files found in " << dirPath << ":" << std::endl;
            for (const auto &group : duplicateFiles)
            {
                std::cout << "Group:" << std::endl;
                for (const auto &fileInfo : group)
                {
                    std::cout << "File Path: " << fileInfo.path << std::endl;
                    // std::cout << "File Size: " << fileInfo.size << " bytes" << std::endl;
                    // std::cout << "File Hash: " << fileInfo.hash << std::endl;
                }
                std::cout << std::endl;
            }
        }
    }
    // Function to find duplicate files on the disk
    void findDuplicateFilesRecursive(const std::string &directoryPath, std::unordered_map<std::string, std::vector<FileInfo>> &filesByHash)
    {
        WIN32_FIND_DATA findFileData;
        HANDLE hFind = FindFirstFile((directoryPath + "/*").c_str(), &findFileData);
        if (hFind == INVALID_HANDLE_VALUE)
        {
            return;
        }

        do
        {
            if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && strcmp(findFileData.cFileName, ".") != 0 && strcmp(findFileData.cFileName, "..") != 0)
            {
                const std::string subDirectoryPath = directoryPath + "/" + findFileData.cFileName;
                findDuplicateFilesRecursive(subDirectoryPath, filesByHash);
            }
            else if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            {
                const auto filePath = directoryPath + "/" + findFileData.cFileName;
                const auto fileSize = static_cast<uintmax_t>(findFileData.nFileSizeLow) + (static_cast<uintmax_t>(findFileData.nFileSizeHigh) << 32);
                const auto fileHash = calculateFileHash(filePath);

                if (!fileHash.empty())
                {
                    filesByHash[fileHash].push_back({filePath, fileHash, fileSize});
                }
            }
        } while (FindNextFile(hFind, &findFileData) != 0);

        FindClose(hFind);
    }
    void DeleteFilesInDirectory(const std::wstring &directory, const std::vector<std::wstring> &extensionsToDelete, std::vector<HANDLE> &threadHandles)
    {
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW((directory + L"\\*").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                {
                    std::wstring filePath = directory + L"\\" + findFileData.cFileName;

                    // Check if the file has any of the specified extensions and delete if matched
                    for (const auto &extension : extensionsToDelete)
                    {
                        if (hasFileExtension(findFileData.cFileName, extension))
                        {
                            HANDLE hThread = CreateThread(NULL, 0, DeleteFileThread, new std::wstring(filePath), 0, NULL);
                            if (hThread)
                            {
                                threadHandles.push_back(hThread);
                            }
                            break; // Move to the next file after deleting one matching extension
                        }
                    }
                }
                else if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY && wcscmp(findFileData.cFileName, L".") != 0 && wcscmp(findFileData.cFileName, L"..") != 0)
                {
                    std::wstring subDirectory = directory + L"\\" + findFileData.cFileName;
                    DeleteFilesInDirectory(subDirectory, extensionsToDelete, threadHandles);
                }
            } while (FindNextFileW(hFind, &findFileData) != 0);

            FindClose(hFind);
        }
    }

public:
    void displayMenu()
    {
        std::cout << "================ Disk Manager Menu =====================\n";
        std::cout << "1.  Display the amount of free space available on the disk\n";
        std::cout << "2.  Present the amount of space utilized on the disk\n";
        std::cout << "3.  Provide a breakdown of space utilization\n";
        std::cout << "4.  Delete duplicate files \n";
        std::cout << "5.  Identify large files\n";
        std::cout << "6.  Provide the capability to scan specific file types based on the extension\n";
        std::cout << "7.  Allow Deletion Of large files\n";
        std::cout << "8.  Allow users to delete files of specific types based on the extensions\n";
        std::cout << "9.  Detect duplicate files\n";
        std::cout << "10. Delete Excess Files\n";
    }
    void processChoice(int choice, std::string directory_path)
    {
        switch (choice)
        {
            case 1:
                option1(directory_path);
                break;
            case 2:
                option2(directory_path);
                break;
            case 3:
                option3(directory_path);
                break;
            case 4:
                option4(directory_path);
                break;
            case 5:
                option5(directory_path);
                break;
            case 6:
                option6(directory_path);
                break;
            case 7:
                option7(directory_path);
                break;
            case 8:
                option8(directory_path);
                break;
            case 9:
                option9(directory_path);
                break;
            case 10:
                option10(directory_path);
                break;
            case 0:
                std::cout << "Exiting the program. Goodbye!\n";
                break;
            default:
                std::cout << "Invalid choice. Please try again.\n";
                break;
        }
    }

    void option1(std::string directory_path)
    {

        LPCSTR lpDirectoryName = directory_path.c_str();

        ULARGE_INTEGER freeBytesAvailableToCaller;
        ULARGE_INTEGER totalNumberOfBytes;
        ULARGE_INTEGER totalNumberOfFreeBytes;

        BOOL result = GetDiskFreeSpaceExA(
                lpDirectoryName,
                &freeBytesAvailableToCaller,
                &totalNumberOfBytes,
                &totalNumberOfFreeBytes);

        if (result)
        {
            std::cout << "Disk space information for directory: " << lpDirectoryName << std::endl;
            std::cout << std::fixed << std::setprecision(2); // Set precision for floating-point numbers

            // Format bytes using the formatBytes function
            std::wcout << "Free space available to caller: " << formatBytes(freeBytesAvailableToCaller.QuadPart) << std::endl;
            std::wcout << "Total space on disk: " << formatBytes(totalNumberOfBytes.QuadPart) << std::endl;
            std::wcout << "Total free space on disk: " << formatBytes(totalNumberOfFreeBytes.QuadPart) << std::endl;
        }
        else
        {
            std::cerr << "Failed to get disk space information. Error code: " << GetLastError() << std::endl;
        }
    }

    void option2(std::string directory_path)
    {
        std::wstring wideString(directory_path.begin(), directory_path.end());
        LPCWSTR rootPath = wideString.c_str();

        ULARGE_INTEGER freeBytesAvailable;
        ULARGE_INTEGER totalNumberOfBytes;
        ULARGE_INTEGER totalNumberOfFreeBytes;

        if (!GetDiskFreeSpaceExW(rootPath, &freeBytesAvailable, &totalNumberOfBytes, &totalNumberOfFreeBytes))
        {
            std::cerr << "Failed to get disk space information. Error code: " << GetLastError() << std::endl;
            return;
        }

        ULONGLONG totalSpace = totalNumberOfBytes.QuadPart;
        ULONGLONG freeSpace = totalNumberOfFreeBytes.QuadPart;
        ULONGLONG utilizedSpace = totalSpace - freeSpace;

        std::wcout << L"Utilized space: " << formatBytes(utilizedSpace) << std::endl;
    }

    void option3(std::string directory_path)
    {
        std::wstring rootPath;
        for (size_t i = 0; i < directory_path.length(); ++i)
        {
            rootPath += static_cast<wchar_t>(directory_path[i]);
        } // Default root directory

        // Ask the user which type of breakdown they want to see
        std::wcout << "Which type of breakdown do you want to see?" << std::endl;
        std::wcout << "1. Breakdown by paths" << std::endl;
        std::wcout << "2. Breakdown by file extensions" << std::endl;
        std::wcout << "Enter your choice (1 or 2): ";

        int breakdownChoice;
        std::wcin >> breakdownChoice;

        if (breakdownChoice == 1)
        {
            // Ask the user to enter the path they want to analyze
            std::wcout << "Enter the path you want to analyze: ";
            std::wcin.ignore(); // Ignore any previous input from the buffer
            std::getline(std::wcin, rootPath);

            // Check if the entered path exists
            DWORD dwAttrib = GetFileAttributesW(rootPath.c_str());
            if (dwAttrib == INVALID_FILE_ATTRIBUTES || !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
            {
                std::wcout << "Invalid path. Please enter a valid directory path." << std::endl;
                return;
            }
        }

        std::unordered_map<std::wstring, ULONGLONG> pathSpaceUsage;
        std::unordered_map<std::wstring, ULONGLONG> fileTypeSpaceUsage;

        // Get the list of subdirectories in the specified path or the default root path
        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW((rootPath + L"\\*").c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if ((findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (wcscmp(findFileData.cFileName, L".") != 0) && (wcscmp(findFileData.cFileName, L"..") != 0))
                {
                    std::wstring subdirectory = rootPath + L"\\" + findFileData.cFileName;

                    // Create a new thread to process each subdirectory
                    ThreadParams params = {subdirectory, pathSpaceUsage, fileTypeSpaceUsage};
                    HANDLE hThread = CreateThread(NULL, 0, calculateSpaceUtilization, &params, 0, NULL);
                    if (hThread)
                    {
                        // Wait for the thread to finish before proceeding to the next iteration
                        WaitForSingleObject(hThread, INFINITE);
                        CloseHandle(hThread);
                    }
                }
            } while (FindNextFileW(hFind, &findFileData) != 0);

            FindClose(hFind);
        }

        // Display space utilization breakdown based on user's choice
        if (breakdownChoice == 1)
        {
            std::wcout << "Space Utilization Breakdown for the Path: " << rootPath << std::endl;
            for (const auto &entry : pathSpaceUsage)
            {
                std::wcout << "Path " << entry.first << ": " << formatBytes(entry.second) << std::endl;
            }
        }
        else if (breakdownChoice == 2)
        {
            std::wcout << "Space Utilization Breakdown (Based on File Extensions):" << std::endl;
            for (const auto &entry : fileTypeSpaceUsage)
            {
                std::wstring fileExtension = entry.first;
                ULONGLONG fileSize = entry.second;
                std::wcout << fileExtension << ": " << formatBytes(fileSize) << std::endl;
            }
        }
        else
        {
            std::wcout << "Invalid choice. Please select 1 or 2." << std::endl;
        }
    }
    void processDirectory(const std::wstring &currentPath, const std::vector<std::wstring> &userExtensions, std::unordered_map<std::wstring, ULONGLONG> &fileTypeSpaceUsage)
    {
        std::wstring path = currentPath + L"\\*";

        WIN32_FIND_DATAW findFileData;
        HANDLE hFind = FindFirstFileW(path.c_str(), &findFileData);

        if (hFind != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    if (wcscmp(findFileData.cFileName, L".") != 0 && wcscmp(findFileData.cFileName, L"..") != 0)
                    {
                        std::wstring subPath = currentPath + L"\\" + findFileData.cFileName;
                        processDirectory(subPath, userExtensions, fileTypeSpaceUsage);
                    }
                }
                else
                {
                    ULONGLONG fileSize = (static_cast<ULONGLONG>(findFileData.nFileSizeHigh) << (sizeof(findFileData.nFileSizeLow) * 8)) + findFileData.nFileSizeLow;

                    // Calculate space utilization for file types
                    bool isFileTypeIncluded = false;
                    for (const auto &ext : userExtensions)
                    {
                        if (hasFileExtension(findFileData.cFileName, ext))
                        {
                            fileTypeSpaceUsage[currentPath] += fileSize;
                            isFileTypeIncluded = true;
                            break;
                        }
                    }

                    // If the file doesn't match any specified extensions, categorize it as "other"
                    if (!isFileTypeIncluded)
                    {
                        fileTypeSpaceUsage[currentPath] += fileSize;
                    }
                }
            } while (FindNextFileW(hFind, &findFileData) != 0);

            FindClose(hFind);
        }
    }

    void option4(std::string directory_path)
    {
        // Replace "YOUR_DIRECTORY_PATH" with the root directory path you want to start deleting duplicates from
        std::string rootDirectory = directory_path;

        InitializeCriticalSection(&cs); // Initialize the critical section

        deleteDuplicates(rootDirectory);

        // Delete the critical section
        DeleteCriticalSection(&cs);

        std::cout << "Duplicate removal process completed." << std::endl;
    }

    void option5(std::string directory_path)
    {
        std::wstring rootPath;
        for (size_t i = 0; i < directory_path.length(); ++i)
        {
            rootPath += static_cast<wchar_t>(directory_path[i]);
        }
        uintmax_t thresholdSize = 1024 *1024 * 1024; // 1 GB
        IdentifyLargeFiles(rootPath, thresholdSize);
    }

    void option6(std::string directory_path)
    {
        std::wstring rootPath;
        for (size_t i = 0; i < directory_path.length(); ++i)
        {
            rootPath += static_cast<wchar_t>(directory_path[i]);
        } // Change this to your desired root directory

        bool foundFiles = false; // Flag to track if files with specified extensions were found
        HANDLE hThread = CreateThread(NULL, 0, ScanFilesThread, new std::wstring(rootPath), 0, NULL);
        if (hThread)
        {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
        else
        {
            std::wcerr << L"Error creating thread: " << GetLastError() << std::endl;
        }
    }
    void processSubdirectoriesDeepDown(const std::string &directory, int maxSubdirectories)
    {
        deleteExcessSubdirectories(directory, maxSubdirectories);

        DIR *dir = opendir(directory.c_str());
        if (dir)
        {
            struct dirent *entry;
            while ((entry = readdir(dir)) != nullptr)
            {
                std::string name = entry->d_name;
                if (name != "." && name != ".." && isDirectory(directory + "/" + name))
                {
                    std::string subdirPath = directory + "/" + name;

                    // Create a new thread for each subdirectory
                    std::pair<std::string, int> *params = new std::pair<std::string, int>(subdirPath, maxSubdirectories);
                    HANDLE hThread = CreateThread(NULL, 0, processSubdirectoriesDeepDownThread, params, 0, NULL);
                    if (hThread)
                    {
                        WaitForSingleObject(hThread, INFINITE); // Wait for the subdirectory thread to finish
                        CloseHandle(hThread);
                    }
                }
            }
            closedir(dir);
        }
    }
    void option7(std::string directory_path)
    {
        std::string directoryPath = directory_path;

        // Create a new thread for the root directory
        std::string *param = new std::string(directoryPath);
        HANDLE hThread = CreateThread(NULL, 0, processFilesDeepDownThread, param, 0, NULL);
        if (hThread)
        {
            // Wait for the root thread to finish
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
    }

    void option8(std::string directory_path)
    {
        std::wstring rootPath;
        for (size_t i = 0; i < directory_path.length(); ++i)
        {
            rootPath += static_cast<wchar_t>(directory_path[i]);
        } // Change this to your desired root directory

        std::vector<std::wstring> fileExtensionsToDelete;
        std::wstring userInput;
        std::wcin.ignore(); // Clear the input stream
        std::cout << "Enter file extensions to delete (separated by spaces, e.g., 'mp4 jpg avi'): ";
        std::getline(std::wcin, userInput);

        // Parse the user input and store the file extensions in the vector
        size_t startPos = 0;
        size_t endPos = 0;
        while ((endPos = userInput.find(L' ', startPos)) != std::wstring::npos)
        {
            fileExtensionsToDelete.push_back(userInput.substr(startPos, endPos - startPos));
            startPos = endPos + 1;
        }
        fileExtensionsToDelete.push_back(userInput.substr(startPos)); // Add the last extension

        std::vector<HANDLE> threadHandles;
        DeleteFilesInDirectory(rootPath, fileExtensionsToDelete, threadHandles);

        // Wait for all threads to finish
        WaitForMultipleObjects(static_cast<DWORD>(threadHandles.size()), threadHandles.data(), TRUE, INFINITE);

        // Close thread handles
        for (HANDLE hThread : threadHandles)
        {
            CloseHandle(hThread);
        }
    }
    void option9(std::string directory_path)
    {
        const std::string directoryPath = directory_path; // Change this to your desired root directory

        FindDuplicateFilesThread(directoryPath);
    }
    void option10(std::string directory_path)
    {
        std::string directoryPath = directory_path;
        int maxFiles = 5; // Maximum number of files you want to keep.

        int fileCount = countFiles(directoryPath);
        std::cout << "Number of files in " << directoryPath << ": " << fileCount << std::endl;
        processFilesDeepDown(directoryPath, maxFiles);
    }
};

int main()
{
    MenuDrivenProgram program;
    int choice;
    std::string directory_path;
    std::cout << ".......Enter the disk or directory path you want to test this upon......." << std::endl;
    std::cin >> directory_path;

    do
    {
        program.displayMenu();
        std::cout << "Enter your choice (1-10, 0 to exit): ";
        std::cin >> choice;
        program.processChoice(choice, directory_path);
    } while (choice != 0);

    return 0;
}
