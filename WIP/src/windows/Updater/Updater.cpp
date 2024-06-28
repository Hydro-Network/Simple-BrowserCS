// Copyright (C) Daniel McGuire Corporation
// THANKS FOR CONTRIBUTING (or Building from Source)
// This file is part of Simple Browser Updater
//
// Updater.cpp

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <windows.h>
#include <shobjidl.h>
#include <curl/curl.h>
#include <json/json.h> 

// Function to write data received by libcurl
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Global variable for taskbar interface
ITaskbarList3* pTaskbarList = nullptr;

// Function to display the progress bar
int ProgressCallback(void* ptr, curl_off_t totalToDownload, curl_off_t nowDownloaded, curl_off_t, curl_off_t) {
    if (totalToDownload <= 0) return 0;

    double fractionDownloaded = (double)nowDownloaded / totalToDownload;
    int barWidth = 50;
    std::cout << "\r" << "Downloading: " << nowDownloaded / (1024 * 1024) << "MB of " << totalToDownload / (1024 * 1024) << "MB [";
    int pos = barWidth * fractionDownloaded;
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else if (i == pos) std::cout << ">";
        else std::cout << " ";
    }
    std::cout << "] " << int(fractionDownloaded * 100.0) << " %";
    std::cout.flush();

    // Update taskbar progress
    if (pTaskbarList) {
        HWND hwnd = GetConsoleWindow();
        pTaskbarList->SetProgressValue(hwnd, nowDownloaded, totalToDownload);
    }

    return 0;
}

// Function to download a file using libcurl
bool DownloadFile(const std::string& url, const std::string& output_path) {
    CURL* curl;
    FILE* fp;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        fp = fopen(output_path.c_str(), "wb");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, NULL);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
        res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        fclose(fp);
        std::cout << std::endl; // Move to the next line after the progress bar
        return (res == CURLE_OK);
    }
    return false;
}

// Function to compare semantic versions
bool IsNewerVersion(const std::string& current, const std::string& latest) {
    std::istringstream currentStream(current);
    std::istringstream latestStream(latest);
    int currentMajor, currentMinor, currentPatch;
    int latestMajor, latestMinor, latestPatch;
    char dot;

    currentStream >> currentMajor >> dot >> currentMinor >> dot >> currentPatch;
    latestStream >> latestMajor >> dot >> latestMinor >> dot >> latestPatch;

    if (latestMajor > currentMajor) return true;
    if (latestMajor == currentMajor && latestMinor > currentMinor) return true;
    if (latestMajor == currentMajor && latestMinor == currentMinor && latestPatch > currentPatch) return true;

    return false;
}

// Helper function to convert std::string to std::wstring
std::wstring StringToWString(const std::string& str) {
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

int main() {
    // Initialize COM library
    HRESULT hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        std::cerr << "Failed to initialize COM library." << std::endl;
        return 1;
    }

    // Create taskbar interface
    hr = CoCreateInstance(CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pTaskbarList));
    if (SUCCEEDED(hr)) {
        pTaskbarList->HrInit();
    } else {
        std::cerr << "Failed to create taskbar interface." << std::endl;
        CoUninitialize();
        return 1;
    }

    // Read the current version from update.ini
    std::ifstream versionFile("update.ini");
    std::string currentVersion;
    if (versionFile.is_open()) {
        std::getline(versionFile, currentVersion);
        versionFile.close();
    }
    else {
        std::cerr << "Failed to open update.ini." << std::endl;
        return 1;
    }

    // Step 1: Get the latest release tag from GitHub
    std::string repo = "Daniel-McGuire-Corporation/Simple-BrowserCS";  // Replace with your repository
    std::string url = "https://api.github.com/repos/" + repo + "/releases/latest";
    std::string readBuffer;

    // Initialize CURL for HTTP request
    CURL* curl;
    CURLcode res;
    curl = curl_easy_init();
    if (curl) {
        // Set CURL options for the request
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        // Perform the request
        res = curl_easy_perform(curl);
        // Clean up CURL
        curl_easy_cleanup(curl);
    }

    // Parse the JSON response
    Json::CharReaderBuilder readerBuilder;
    Json::Value jsonData;
    std::string errs;
    std::istringstream s(readBuffer);
    std::string tag_name;
    std::string updater_url;

    // Check if JSON parsing was successful
    if (Json::parseFromStream(readerBuilder, s, &jsonData, &errs)) {
        // Extract the tag name
        tag_name = jsonData["tag_name"].asString();
        const Json::Value assets = jsonData["assets"];
        // Find the updater.exe in the assets
        for (const auto& asset : assets) {
            if (asset["name"].asString() == "updater.exe") {
                updater_url = asset["browser_download_url"].asString();
                break;
            }
        }
    }

    // If updater.exe URL is not found, exit with an error
    if (updater_url.empty()) {
        std::cerr << "Updater not found in the latest release." << std::endl;
        return 1;
    }

    // Compare versions
    if (IsNewerVersion(currentVersion, tag_name)) {
        // Step 2: Quit the main app
        system("taskkill /f /im SimpleBrowser.exe");

        // Step 3: Download the updater.exe
        std::string updater_path = "updater.exe";
        if (!DownloadFile(updater_url, updater_path)) {
            std::cerr << "Failed to download updater.exe." << std::endl;
            return 1;
        }

        // Step 4: Run the updater.exe
        std::wstring updater_path_w = StringToWString(updater_path);
        STARTUPINFO si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        if (!CreateProcess(updater_path_w.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            std::cerr << "Failed to start updater.exe." << std::endl;
            return 1;
        }

        // Close process and thread handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else {
        // Run the main app if no update is needed
        std::wstring main_app_path = L"SimpleBrowser.exe";
        STARTUPINFO si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        if (!CreateProcess(main_app_path.c_str(), NULL, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            std::cerr << "Failed to start SimpleBrowser.exe." << std::endl;
            return 1;
        }

        // Close process and thread handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }

    // Clean up taskbar interface
    if (pTaskbarList) {
        pTaskbarList->Release();
    }

    // Uninitialize COM library
    CoUninitialize();

    return 0;
}
