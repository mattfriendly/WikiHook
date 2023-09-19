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
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    const char* db_host = getenv("DB_HOST2");
    const char* db_user = getenv("DB_USER2");
    const char* db_password = getenv("DB_PASSWORD2");
    const char* db_name = getenv("DB_NAME2");
    const char* client_url = getenv("CLIENT_URL");
    const char* client_cert = getenv("MYSQL_CLIENT_CERT");
    const char* client_key = getenv("MYSQL_CLIENT_KEY");
    const char* ca_file = getenv("MYSQL_CA_FILE");

    if (!db_host) {
        std::cerr << "Error: DB_HOST2 environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!db_user) {
        std::cerr << "Error: DB_USER2 environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!db_password) {
        std::cerr << "Error: DB_PASSWORD2 environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!db_name) {
        std::cerr << "Error: DB_NAME2 environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!client_url) {
        std::cerr << "Error: CLIENT_URL environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!client_cert) {
        std::cerr << "Error: MYSQL_CLIENT_CERT environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!client_key) {
        std::cerr << "Error: MYSQL_CLIENT_KEY environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (!ca_file) {
        std::cerr << "Error: MYSQL_CA_FILE environment variable is not set!" << std::endl;
        exit(EXIT_FAILURE);
    }

    // Initialize MySQL
    conn = mysql_init(NULL);

    // Set SSL options

    std::cout << "Key: " << client_key << "\nCert: " << client_cert << "\nCA File: " << ca_file << std::endl;
    std::ifstream file("client-key.pem");
    if (!file) {
        std::cerr << "Error reading client-key.pem!" << std::endl;
        return;
    }
    if (mysql_ssl_set(conn, client_key, client_cert, ca_file, NULL, NULL) != 0) {
        std::cerr << "Failed to set SSL parameters. Error: " << mysql_error(conn) << std::endl;
        return;
    } else {
        std::cerr << "SSL parameters set." << std::endl;
    }
    // Connect to the database
    if (!mysql_real_connect(conn, db_host, db_user, db_password, db_name, 0, NULL, CLIENT_SSL)) {
        std::cerr << "Error: Could not establish a MySQL connection! Error: " << mysql_error(conn) << std::endl;
        return;
    } else {
        std::cout << "Successfully connected to the MySQL server." << std::endl;
    }

    const char* shipment_id = getenv("SHIPMENT_ID");
    if (!shipment_id) {
        std::cerr << "Error: SHIPMENT_ID environment variable is not set!" << std::endl;
        return;
    }

    std::string query = "SELECT * FROM hfc_shipments WHERE order_id = ";
    query += "'";
    query += shipment_id;  // append the shipment_id to the query
    query += "'";

    // Execute the SQL query
    if(mysql_query(conn, query.c_str())) {
        std::cerr << "SELECT error: " << mysql_error(conn) << std::endl;
        return;
    }

    res = mysql_store_result(conn);
// Process the results and prepare the data for HTTP POST
while ((row = mysql_fetch_row(res)) != NULL) {
    int numFields = mysql_num_fields(res);
    std::string payload = "{";

    for (int i = 0; i < numFields; i++) {
        if (i != 0) {
            payload += ","; // Add a comma between fields
        }
        payload += "\"" + std::string(mysql_fetch_field_direct(res, i)->name) + "\": \"" + (row[i] ? row[i] : "NULL") + "\"";
    }

    payload += "}";

    std::cout << "Payload being sent: " << payload << std::endl;

    httplib::SSLClient cli(client_url);
    auto response = cli.Post("/", payload.c_str(), "application/json");

    if (!response) {
        std::cerr << "Failed to get a response from the server. The server might be down or there might be a network issue." << std::endl;
    } else if (response->status != 200) {
        std::cerr << "Failed to POST data. HTTP Status: " << response->status << ". Reason: " << response->body << std::endl;
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
        clientMode();
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
