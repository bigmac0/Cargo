#include "catch.hpp"
#include "libcargo.h"

#include <stdio.h>

using namespace cargo;

static int callback(void *NotUsed, int argc, char **argv, char **azColName) {
    int i;
    for (i = 0; i < argc; i++) {
        printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
    }
    printf("\n");
    return 0;
}

TEST_CASE("Sqlite3 works", "[sqlite3]") {
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;
    rc = sqlite3_open(":memory:", &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
    } else {
        rc = sqlite3_exec(db, "CREATE TABLE t(x INTEGER);", callback, 0,
                          &zErrMsg);
        rc = sqlite3_exec(db, "INSERT INTO t VALUES(42);", callback, 0,
                          &zErrMsg);
        rc = sqlite3_exec(db, "SELECT * FROM t;", callback, 0, &zErrMsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        }
    }
    sqlite3_close(db);
}

static int circle_query_callback(sqlite3_rtree_query_info *p) {
    int nParam = p->nParam;
    std::cout << "there are " << nParam << " parameters" << std::endl;
    float lng = p->aCoord[0];
    float lat = p->aCoord[2];
}

TEST_CASE("Sqlite3 Rtree", "[sqlite3]") {
    std::cout << "rtree test" << std::endl;
    Longitude minX, maxX;
    Latitude minY, maxY;
    KeyValueNodes nodes;
    file::ReadNodes("../data/roadnetwork/mny.rnet", nodes, minX, maxX, minY,
                    maxY);
    sqlite3 *db;
    char *zErrMsg = 0;
    int rc;
    rc = sqlite3_open(":memory:", &db);
    if (rc != SQLITE_OK) {
        std::cerr << "open error: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
    } else {
        rc = sqlite3_exec(db,
                          "CREATE VIRTUAL TABLE location_index USING rtree "
                          "(id, minX, maxX, minY, maxY",
                          NULL, NULL, &zErrMsg);
        sqlite3_rtree_query_callback(db, "circle", circle_query_callback, NULL,
                                     NULL);

        sqlite3_stmt *insert_node;
        rc = sqlite3_prepare_v2(
            db, "INSERT INTO location_index VALUES(?, ?, ?, ?, ?)", -1,
            &insert_node, NULL);
        for (auto &kv : nodes) {
            sqlite3_bind_int(insert_node, 1, kv.first);
            sqlite3_bind_int(insert_node, 2, kv.second.lng);
            sqlite3_bind_int(insert_node, 3, kv.second.lng);
            sqlite3_bind_int(insert_node, 4, kv.second.lat);
            sqlite3_bind_int(insert_node, 5, kv.second.lat);
            sqlite3_step(insert_node);
        }

        auto iter = nodes.begin();
        std::string select_query =
            "SELECT * from location_index WHERE id MATCH circle(";
        select_query.append(std::to_string(iter->second.lng));
        select_query.append(", ");
        select_query.append(std::to_string(iter->second.lat));
        select_query.append(")");
        sqlite3_exec(db, select_query.c_str(), NULL, NULL, &zErrMsg);

        sqlite3_finalize(insert_node);
        sqlite3_close(db);
    }
}