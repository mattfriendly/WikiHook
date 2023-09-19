#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <cstdlib>
#include <signal.h>
#include <mysql/mysql.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

const char *LOCK_FILE = "/tmp/hooker.lock";
std::ofstream logFile;

void initLogFile() {
    if (!logFile.is_open()) {
        logFile.open("request.log", std::ios::app);
        if (!logFile.is_open()) {
            std::cerr << "Unable to open log file." << std::endl;
            exit(EXIT_FAILURE);
        }
    }
}

void writeToLog(const std::string& message) {
    initLogFile();
    logFile << message << std::endl;
    logFile.flush();
}

void handleWebhook(const httplib::Request& req, httplib::Response& res) {
    writeToLog("Received data: " + req.body);
    res.set_content("{\"status\": \"Received successfully\"}", "application/json");
}

void signalHandler(int signum) {
    // Remove the lock file
    remove(LOCK_FILE);
    // Terminate the process
    exit(signum);
}

std::string rowToJson(MYSQL_RES* res, MYSQL_ROW row) {
    nlohmann::json jsonObj;
    MYSQL_FIELD* fields = mysql_fetch_fields(res);
    int numFields = mysql_num_fields(res);

    for (int i = 0; i < numFields; i++) {
        const char* field_name = fields[i].name;
        const char* field_value = row[i] ? row[i] : "NULL";
        jsonObj[field_name] = field_value;
    }

    return jsonObj.dump();
}

void clientMode() {
    // Variables for MySQL
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;
    MYSQL_FIELD *fields;

    // Collect environment variables
    const char* env_vars[] = {"DB_HOST2", "DB_USER2", "DB_PASSWORD2", "DB_NAME2", "CLIENT_URL", 
                              "MYSQL_CLIENT_CERT", "MYSQL_CLIENT_KEY", "MYSQL_CA_FILE", "SHIPMENT_ID"};
    for (const char* var : env_vars) {
        if (!getenv(var)) {
            writeToLog(std::string("Error: ") + var + " environment variable is not set!");
            return;
        }
    }

    // Initialize MySQL and set SSL options
    conn = mysql_init(NULL);
    if (mysql_ssl_set(conn, getenv("MYSQL_CLIENT_KEY"), getenv("MYSQL_CLIENT_CERT"), 
                     getenv("MYSQL_CA_FILE"), NULL, NULL) != 0) {
        writeToLog("Failed to set SSL parameters. Error: " + std::string(mysql_error(conn)));
        return;
    } else {
        writeToLog("SSL parameters set.");
    }

    // Database connection
    if (!mysql_real_connect(conn, getenv("DB_HOST2"), getenv("DB_USER2"), getenv("DB_PASSWORD2"),
                            getenv("DB_NAME2"), 0, NULL, CLIENT_SSL)) {
        writeToLog("Error: Could not establish a MySQL connection! Error: " + std::string(mysql_error(conn)));
        return;
    } else {
        writeToLog("Successfully connected to the MySQL server.");
    }

    // Construct the SQL query
    std::string query = "SELECT * FROM hfc_shipments WHERE order_id = '" + std::string(getenv("SHIPMENT_ID")) + "'";
    if(mysql_query(conn, query.c_str())) {
        writeToLog("SELECT error: " + std::string(mysql_error(conn)));
        return;
    }

    res = mysql_store_result(conn);
    fields = mysql_fetch_fields(res);

    // Process results and prepare data for HTTP POST
    while ((row = mysql_fetch_row(res)) != NULL) {
        std::string payload = rowToJson(res, row);
        writeToLog("Payload being sent: " + payload);

        writeToLog("Initializing the SSL client...");
        httplib::SSLClient cli(getenv("CLIENT_URL"));

        cli.set_default_headers({
            {"User-Agent", "sequel-hooker/1.0"},
            {"Accept", "*/*"}
        });
        cli.set_ca_cert_path("/etc/ssl/certs/ca-certificates.crt");

        writeToLog("Attempting to send request to " + std::string(getenv("CLIENT_URL")));
        auto response = cli.Post("/", payload.c_str(), "application/json");
        
        if (!response) {
            writeToLog("Failed to get a response from the server.");
        } else {
            writeToLog("Received a response from the server.");

            if (response->status >= 400) {
                writeToLog("HTTP Error: " + std::to_string(response->status) + " - " + response->body);
            } else {
                writeToLog("HTTP Status: " + std::to_string(response->status));
                writeToLog("Response Body: " + response->body);
            }
        }
    }

    // Cleanup
    mysql_free_result(res);
    mysql_close(conn);
}

int main() {
    std::string mode;
    std::cout << "Choose mode (server/client): ";
    std::cin >> mode;

    if (mode == "client") {
        initLogFile(); // Initialize log file
        clientMode();
        logFile.close(); // Close the log file when done
        return 0;
    } else if (mode != "server") {
        std::cerr << "Invalid mode selected." << std::endl;
        exit(EXIT_FAILURE);
    }

    // The following part will only be executed in server mode:
    if (access(LOCK_FILE, F_OK) == 0) {
        std::cerr << "Error: fancy_hooker is already running." << std::endl;
        exit(EXIT_FAILURE);
    }

    initLogFile(); // Initialize log file for server mode

    signal(SIGINT, signalHandler);  // Handle Ctrl+C
    signal(SIGTERM, signalHandler); // Handle termination request

    pid_t pid, sid;

    pid = fork();
    if (pid < 0) {
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        std::cout << "The webhook_server has successfully started. The process ID is " << pid << "." << std::endl;
        logFile.close(); // Close the log file when done
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

 return 0;

}
