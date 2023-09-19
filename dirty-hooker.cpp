#include "/usr/include/httplib.h"
#include <iostream>
#include <fstream>

int main() {
    httplib::Server svr;

    svr.Post("/webhook", [](const httplib::Request& req, httplib::Response& res) {
        // Print to console
        std::cout << "Received data: " << req.body << std::endl;

        // Log to file
        std::ofstream logFile("webhook_log.txt", std::ios::app); // Open file in append mode
        if (logFile.is_open()) {
            logFile << "Received data: " << req.body << std::endl;
            logFile.close();
        } else {
            std::cerr << "Unable to open log file." << std::endl;
        }

        // Responding to Zapier
        res.set_content("{\"status\": \"Received successfully\"}", "application/json");
    });

    svr.listen("127.0.0.1", 4000);
}
