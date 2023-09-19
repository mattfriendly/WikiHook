#include "/usr/include/httplib.h"
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <signal.h>

const char *LOCK_FILE = "/tmp/hooker.lock";

void handleWebhook(const httplib::Request& req, httplib::Response& res) {
    std::cout << "Received data: " << req.body << std::endl;

    std::ofstream logFile("webhook_log.txt", std::ios::app);
    if (logFile.is_open()) {
        logFile << "Received data: " << req.body << std::endl;
        logFile.close();
    } else {
        std::cerr << "Unable to open log file." << std::endl;
    }

    res.set_content("{\"status\": \"Received successfully\"}", "application/json");
}

void signalHandler(int signum) {
    // Remove the lock file
    remove(LOCK_FILE);
    // Terminate the process
    exit(signum);
}

int main() {
    if (access(LOCK_FILE, F_OK) == 0) {
        std::cerr << "Error: fancy_hooker is already running." << std::endl;
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, signalHandler);  // Handle Ctrl+C
    signal(SIGTERM, signalHandler); // Handle termination request

    pid_t pid, sid;

    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        std::cout << "The webhook_server has successfully started. The process ID is " << pid << "." << std::endl;
        exit(EXIT_SUCCESS);
    }

    std::ofstream lockFile(LOCK_FILE);
    if (!lockFile) {
        std::cerr << "Error: Unable to create lock file." << std::endl;
        exit(EXIT_FAILURE);
    }
    lockFile.close();

    umask(0);
    sid = setsid();
    if (sid < 0) {
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    httplib::Server svr;
    svr.Post("/webhook", handleWebhook);
    svr.listen("::", 4000);
}
