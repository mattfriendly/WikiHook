#include <iostream>
#include <fstream>
#include <cstdlib>
#include <mysql/mysql.h>

// File to store last processed id
const char* LAST_ID_FILE = "last_id.txt";

// Fetch the last processed id
int getLastId() {
    std::ifstream file(LAST_ID_FILE);
    if(!file.is_open()) {
        return 0;  // default value if file not found
    }

    int lastId;
    file >> lastId;
    return lastId;
}

// Save the last processed id
void saveLastId(int id) {
    std::ofstream file(LAST_ID_FILE);
    file << id;
}

int main() {
    MYSQL *conn;
    MYSQL_RES *res;
    MYSQL_ROW row;

    const char* db_host = getenv("DB_HOST");
    const char* db_user = getenv("DB_USER");
    const char* db_pass = getenv("DB_PASSWORD");
    const char* db_name = getenv("DB_NAME");
    const char* db_table = getenv("DB_TABLE");

    if(!db_host || !db_user || !db_pass || !db_name || !db_table) {
        std::cerr << "One or more environment variables (DB_HOST, DB_USER, DB_PASSWORD, DB_NAME, DB_TABLE) are not set." << std::endl;
        return 1;
    }

    std::cout << "Connecting to database..." << std::endl;

    conn = mysql_init(NULL);

    // Connect to database
    if(!mysql_real_connect(conn, db_host, db_user, db_pass, db_name, 0, NULL, 0)) {
        std::cerr << "Connection to database failed: " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return 1;
    }

    int lastId = getLastId();

    std::cout << "Fetching new rows from database..." << std::endl;

    std::string query = "SELECT id, order_id FROM " + std::string(db_table) + " WHERE id > " + std::to_string(lastId) + " ORDER BY id ASC";

    if(mysql_query(conn, query.c_str())) {
        std::cerr << "Query failed: " << mysql_error(conn) << std::endl;
        mysql_close(conn);
        return 1;
    }

    res = mysql_store_result(conn);
    int numRows = mysql_num_rows(res);

    if(numRows == 0) {
        std::cout << "No new rows found." << std::endl;
    } else {
        std::cout << numRows << " new rows found." << std::endl;
    }

    for(int i = 0; i < numRows; i++) {
        row = mysql_fetch_row(res);
        std::cout << "Processing row with id: " << row[0] << " and order_id: " << row[1] << std::endl;
        
        setenv("SHIPMENT_ID", row[1], 1); // set environment variable
        std::cout << "Executing wikihook binary..." << std::endl;
        system("./wikihook"); // execute binary

        // Update the last processed id
        lastId = std::stoi(row[0]);
    }

    if(numRows > 0) {
        saveLastId(lastId);
        std::cout << "Saved last processed id: " << lastId << std::endl;
    }

    mysql_free_result(res);
    mysql_close(conn);

    std::cout << "Finished processing." << std::endl;

    return 0;
}
