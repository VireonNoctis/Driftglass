#include "driftglass/core.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace driftglass {
namespace {

constexpr std::uint64_t kDatabaseLimit = 512ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kReportPayloadLimit = 256ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kReportCountLimit = 1000ULL;

[[noreturn]] void sql_error(sqlite3* db, const std::string& action) {
    throw Error(action + " (" + (db ? sqlite3_errmsg(db) : "SQLite unavailable") + ")");
}

void check_sql(sqlite3* db, int code, const std::string& action) {
    if (code != SQLITE_OK && code != SQLITE_DONE && code != SQLITE_ROW) sql_error(db, action);
}

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        check_sql(db_, sqlite3_prepare_v2(db_, sql, -1, &statement_, nullptr), "could not prepare history query");
    }
    ~Statement() { if (statement_) sqlite3_finalize(statement_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    sqlite3_stmt* get() const { return statement_; }
    void bind_text(int index, const std::string& value) {
        if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            throw Error("history text value exceeds SQLite's binding limit");
        }
        check_sql(db_, sqlite3_bind_text(statement_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT),
                  "could not bind history value");
    }
    void bind_integer(int index, std::uint64_t value) {
        if (value > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())) {
            throw Error("history value exceeds SQLite integer range");
        }
        check_sql(db_, sqlite3_bind_int64(statement_, index, static_cast<sqlite3_int64>(value)),
                  "could not bind history count");
    }
    void bind_nullable_text(int index, const Json& value) {
        if (value.is_null()) {
            check_sql(db_, sqlite3_bind_null(statement_, index), "could not bind null history value");
        } else {
            bind_text(index, value.get<std::string>());
        }
    }
    int step() {
        const int result = sqlite3_step(statement_);
        check_sql(db_, result, "history query failed");
        return result;
    }
private:
    sqlite3* db_{};
    sqlite3_stmt* statement_{};
};

class Database {
public:
    explicit Database(const std::filesystem::path& requested_path) {
        std::error_code error;
        path_ = std::filesystem::absolute(requested_path, error);
        if (error) throw Error("could not resolve history database path");
        if (path_.parent_path().empty()) path_ = std::filesystem::current_path() / path_;
        if (!std::filesystem::is_directory(path_.parent_path())) {
            throw Error("history database directory does not exist");
        }

        const auto status = std::filesystem::symlink_status(path_, error);
        const bool exists = !error && std::filesystem::exists(status);
        if (error && error != std::errc::no_such_file_or_directory) {
            throw Error("could not inspect history database path");
        }
        if (exists && (!std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status))) {
            throw Error("history database must be a regular file; symlinks and special files are rejected");
        }

#ifndef _WIN32
        struct stat before {};
        if (::lstat(path_.c_str(), &before) == 0) {
            if (!S_ISREG(before.st_mode)) throw Error("history database must be a regular file");
            before_device_ = before.st_dev;
            before_inode_ = before.st_ino;
            had_file_ = true;
        } else if (errno == ENOENT) {
            const int descriptor = ::open(path_.c_str(), O_CREAT | O_EXCL | O_WRONLY
#ifdef O_NOFOLLOW
                | O_NOFOLLOW
#endif
                , S_IRUSR | S_IWUSR);
            if (descriptor < 0) {
                if (errno != EEXIST) throw Error("could not create private history database file");
                if (::lstat(path_.c_str(), &before) != 0 || !S_ISREG(before.st_mode)) {
                    throw Error("history database path changed before it could be opened");
                }
            } else {
                ::close(descriptor);
                if (::lstat(path_.c_str(), &before) != 0 || !S_ISREG(before.st_mode)) {
                    throw Error("could not verify newly created history database file");
                }
            }
            before_device_ = before.st_dev;
            before_inode_ = before.st_ino;
            had_file_ = true;
        } else {
            throw Error("could not inspect history database path");
        }
#endif

        const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        const int result = sqlite3_open_v2(path_.string().c_str(), &db_, flags, nullptr);
        if (result != SQLITE_OK) {
            const std::string message = db_ ? sqlite3_errmsg(db_) : "SQLite unavailable";
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw Error("could not open local report history (" + message + ")");
        }

        try {
            sqlite3_busy_timeout(db_, 5000);
#ifndef _WIN32
            struct stat after {};
            if (::lstat(path_.c_str(), &after) != 0 || !S_ISREG(after.st_mode)
                || (had_file_ && (after.st_dev != before_device_ || after.st_ino != before_inode_))) {
                throw Error("history database path changed while it was being opened");
            }
#endif
            execute("PRAGMA foreign_keys = ON");
            execute("PRAGMA journal_mode = DELETE");
            execute("PRAGMA synchronous = FULL");
            configure_size_limit();
            configure_schema();
        } catch (...) {
            sqlite3_close(db_);
            db_ = nullptr;
            throw;
        }
    }

    ~Database() { if (db_) sqlite3_close(db_); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    sqlite3* get() const { return db_; }

    void execute(const char* sql) {
        char* message = nullptr;
        const int result = sqlite3_exec(db_, sql, nullptr, nullptr, &message);
        if (result != SQLITE_OK) {
            std::string detail = message ? message : sqlite3_errmsg(db_);
            sqlite3_free(message);
            throw Error("history database operation failed (" + detail + ")");
        }
    }

private:
    void configure_size_limit() {
        Statement page_size(db_, "PRAGMA page_size");
        if (page_size.step() != SQLITE_ROW) throw Error("could not read SQLite page size");
        const auto page = sqlite3_column_int64(page_size.get(), 0);
        if (page <= 0) throw Error("SQLite returned an invalid page size");
        const auto max_pages = kDatabaseLimit / static_cast<std::uint64_t>(page);
        if (max_pages == 0 || max_pages > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max())) {
            throw Error("history database size limit is invalid");
        }

        Statement page_count(db_, "PRAGMA page_count");
        if (page_count.step() != SQLITE_ROW) throw Error("could not read SQLite page count");
        const auto current_pages = sqlite3_column_int64(page_count.get(), 0);
        std::error_code error;
        const auto file_size = std::filesystem::file_size(path_, error);
        if (error || file_size > kDatabaseLimit || current_pages < 0
            || static_cast<std::uint64_t>(current_pages) > max_pages) {
            throw Error("history database exceeds the 512 MiB size limit");
        }
        const std::string pragma = "PRAGMA max_page_count = " + std::to_string(max_pages);
        Statement limit(db_, pragma.c_str());
        if (limit.step() != SQLITE_ROW || static_cast<std::uint64_t>(sqlite3_column_int64(limit.get(), 0)) > max_pages) {
            throw Error("could not enforce history database size limit");
        }
    }

    void configure_schema() {
        Statement version(db_, "PRAGMA user_version");
        if (version.step() != SQLITE_ROW) throw Error("could not read history schema version");
        const int schema = sqlite3_column_int(version.get(), 0);
        if (schema == 0) {
            Statement tables(db_, "SELECT count(*) FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'");
            if (tables.step() != SQLITE_ROW || sqlite3_column_int64(tables.get(), 0) != 0) {
                throw Error("history database has unversioned tables; refusing to modify it");
            }
            create_schema();
        } else if (schema != 1) {
            throw Error("unsupported history database schema version " + std::to_string(schema));
        } else {
            for (const auto* table : {"reports", "runs", "report_runs", "findings"}) {
                Statement exists(db_, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?");
                exists.bind_text(1, table);
                if (exists.step() != SQLITE_ROW) throw Error(std::string("history database is missing table '") + table + "'");
            }
            const std::map<std::string, std::set<std::string>> columns{
                {"reports", {"report_id", "schema_version", "experiment_id", "application_name", "status",
                    "baseline_run_id", "candidate_run_id", "baseline_version", "candidate_version",
                    "finding_count", "uncertainty_count", "report_json", "stored_at"}},
                {"runs", {"run_id", "experiment_id", "application_name", "version", "artifact_sha256", "capture_status"}},
                {"report_runs", {"report_id", "run_id", "role"}},
                {"findings", {"report_id", "finding_id", "rule_id", "change_type", "category", "summary", "finding_json"}}
            };
            for (const auto& [table, required] : columns) {
                const std::string pragma = "PRAGMA table_info(" + table + ")";
                Statement info(db_, pragma.c_str());
                std::set<std::string> actual;
                while (info.step() == SQLITE_ROW) actual.insert(column_text(info.get(), 1));
                for (const auto& name : required) {
                    if (!actual.contains(name)) throw Error("history database table '" + table + "' is missing column '" + name + "'");
                }
            }
        }
    }

    void create_schema() {
        execute("BEGIN EXCLUSIVE");
        try {
            execute(R"SQL(
                CREATE TABLE reports (
                    report_id TEXT PRIMARY KEY CHECK(length(report_id) = 64),
                    schema_version INTEGER NOT NULL CHECK(schema_version = 1),
                    experiment_id TEXT NOT NULL,
                    application_name TEXT NOT NULL,
                    status TEXT NOT NULL CHECK(status IN ('complete', 'partial')),
                    baseline_run_id TEXT NOT NULL,
                    candidate_run_id TEXT NOT NULL,
                    baseline_version TEXT NOT NULL,
                    candidate_version TEXT NOT NULL,
                    finding_count INTEGER NOT NULL CHECK(finding_count >= 0),
                    uncertainty_count INTEGER NOT NULL CHECK(uncertainty_count >= 0),
                    report_json TEXT NOT NULL,
                    stored_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
                )
            )SQL");
            execute(R"SQL(
                CREATE TABLE runs (
                    run_id TEXT PRIMARY KEY,
                    experiment_id TEXT NOT NULL,
                    application_name TEXT NOT NULL,
                    version TEXT NOT NULL,
                    artifact_sha256 TEXT,
                    capture_status TEXT NOT NULL CHECK(capture_status IN ('running', 'completed', 'incomplete', 'failed'))
                )
            )SQL");
            execute(R"SQL(
                CREATE TABLE report_runs (
                    report_id TEXT NOT NULL REFERENCES reports(report_id) ON DELETE CASCADE,
                    run_id TEXT NOT NULL REFERENCES runs(run_id) ON DELETE RESTRICT,
                    role TEXT NOT NULL CHECK(role IN ('baseline', 'candidate')),
                    PRIMARY KEY(report_id, role),
                    UNIQUE(report_id, run_id)
                )
            )SQL");
            execute(R"SQL(
                CREATE TABLE findings (
                    report_id TEXT NOT NULL REFERENCES reports(report_id) ON DELETE CASCADE,
                    finding_id TEXT NOT NULL,
                    rule_id TEXT NOT NULL,
                    change_type TEXT NOT NULL CHECK(change_type IN ('added', 'removed', 'changed')),
                    category TEXT NOT NULL,
                    summary TEXT NOT NULL,
                    finding_json TEXT NOT NULL,
                    PRIMARY KEY(report_id, finding_id)
                )
            )SQL");
            execute("CREATE INDEX reports_history_idx ON reports(stored_at DESC, report_id DESC)");
            execute("CREATE INDEX reports_experiment_idx ON reports(experiment_id, stored_at DESC)");
            execute("PRAGMA user_version = 1");
            execute("COMMIT");
        } catch (...) {
            try { execute("ROLLBACK"); } catch (...) {}
            throw;
        }
    }

    std::filesystem::path path_;
    sqlite3* db_{};
#ifndef _WIN32
    dev_t before_device_{};
    ino_t before_inode_{};
    bool had_file_{false};
#endif

    static std::string column_text(sqlite3_stmt* statement, int index) {
        const auto* value = sqlite3_column_text(statement, index);
        return value ? reinterpret_cast<const char*>(value) : "";
    }
};

// Keep this helper outside Database for query results.
std::string column_text(sqlite3_stmt* statement, int index) {
    const auto* value = sqlite3_column_text(statement, index);
    return value ? reinterpret_cast<const char*>(value) : "";
}

void insert_run(sqlite3* db, const Json& report, const char* role) {
    const auto& run = report.at(role);
    Statement insert(db, R"SQL(
        INSERT INTO runs(run_id, experiment_id, application_name, version, artifact_sha256, capture_status)
        VALUES(?, ?, ?, ?, ?, ?) ON CONFLICT(run_id) DO NOTHING
    )SQL");
    insert.bind_text(1, run.at("run_id").get<std::string>());
    insert.bind_text(2, report.at("experiment_id").get<std::string>());
    insert.bind_text(3, report.at("application_name").get<std::string>());
    insert.bind_text(4, run.at("version").get<std::string>());
    insert.bind_nullable_text(5, run.at("artifact_sha256"));
    insert.bind_text(6, run.at("capture_status").get<std::string>());
    insert.step();

    Statement check(db, "SELECT experiment_id, application_name, version, artifact_sha256, capture_status FROM runs WHERE run_id=?");
    check.bind_text(1, run.at("run_id").get<std::string>());
    if (check.step() != SQLITE_ROW) throw Error("could not verify stored run metadata");
    const bool hash_matches = run.at("artifact_sha256").is_null()
        ? sqlite3_column_type(check.get(), 3) == SQLITE_NULL
        : run.at("artifact_sha256").get<std::string>() == column_text(check.get(), 3);
    if (column_text(check.get(), 0) != report.at("experiment_id").get<std::string>()
        || column_text(check.get(), 1) != report.at("application_name").get<std::string>()
        || column_text(check.get(), 2) != run.at("version").get<std::string>()
        || !hash_matches
        || column_text(check.get(), 4) != run.at("capture_status").get<std::string>()) {
        throw Error("run_id '" + run.at("run_id").get<std::string>() + "' already exists with different metadata");
    }
}

} // namespace

std::string save_report(const std::filesystem::path& database_path, const Json& report) {
    validate_report(report);
    const auto serialized = report.dump(2, ' ', true, Json::error_handler_t::strict);
    if (serialized.size() > kReportPayloadLimit) throw Error("report exceeds the local history report-payload budget");
    Database database(database_path);
    sqlite3* db = database.get();
    database.execute("BEGIN IMMEDIATE");
    try {
        Statement existing(db, "SELECT report_json FROM reports WHERE report_id=?");
        existing.bind_text(1, report.at("report_id").get<std::string>());
        if (existing.step() == SQLITE_ROW) {
            const auto stored = parse_json_strict(column_text(existing.get(), 0));
            if (canonical_json(stored) != canonical_json(report)) throw Error("report_id already exists with different content");
            database.execute("COMMIT");
            return report.at("report_id").get<std::string>();
        }

        Statement totals(db, "SELECT count(*), COALESCE(sum(length(CAST(report_json AS BLOB))), 0) FROM reports");
        if (totals.step() != SQLITE_ROW) throw Error("could not inspect local report history");
        const auto total_count = sqlite3_column_int64(totals.get(), 0);
        const auto total_bytes = sqlite3_column_int64(totals.get(), 1);
        if (total_count < 0 || static_cast<std::uint64_t>(total_count) >= kReportCountLimit) {
            throw Error("local history is full (1000 reports); delete selected reports before saving more");
        }
        if (total_bytes < 0 || static_cast<std::uint64_t>(total_bytes) + serialized.size() > kReportPayloadLimit) {
            throw Error("local history report-payload budget would be exceeded");
        }

        insert_run(db, report, "baseline");
        insert_run(db, report, "candidate");

        Statement insert(db, R"SQL(
            INSERT INTO reports(report_id, schema_version, experiment_id, application_name, status,
                baseline_run_id, candidate_run_id, baseline_version, candidate_version,
                finding_count, uncertainty_count, report_json)
            VALUES(?, 1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        )SQL");
        insert.bind_text(1, report.at("report_id").get<std::string>());
        insert.bind_text(2, report.at("experiment_id").get<std::string>());
        insert.bind_text(3, report.at("application_name").get<std::string>());
        insert.bind_text(4, report.at("status").get<std::string>());
        insert.bind_text(5, report.at("baseline").at("run_id").get<std::string>());
        insert.bind_text(6, report.at("candidate").at("run_id").get<std::string>());
        insert.bind_text(7, report.at("baseline").at("version").get<std::string>());
        insert.bind_text(8, report.at("candidate").at("version").get<std::string>());
        insert.bind_integer(9, report.at("findings").size());
        insert.bind_integer(10, report.at("uncertainties").size());
        insert.bind_text(11, serialized);
        insert.step();

        for (const auto* role : {"baseline", "candidate"}) {
            Statement link(db, "INSERT INTO report_runs(report_id, run_id, role) VALUES(?, ?, ?)");
            link.bind_text(1, report.at("report_id").get<std::string>());
            link.bind_text(2, report.at(role).at("run_id").get<std::string>());
            link.bind_text(3, role);
            link.step();
        }

        for (const auto& finding : report.at("findings")) {
            Statement add(db, R"SQL(
                INSERT INTO findings(report_id, finding_id, rule_id, change_type, category, summary, finding_json)
                VALUES(?, ?, ?, ?, ?, ?, ?)
            )SQL");
            add.bind_text(1, report.at("report_id").get<std::string>());
            add.bind_text(2, finding.at("finding_id").get<std::string>());
            add.bind_text(3, finding.at("rule_id").get<std::string>());
            add.bind_text(4, finding.at("change_type").get<std::string>());
            add.bind_text(5, finding.at("category").get<std::string>());
            add.bind_text(6, finding.at("summary").get<std::string>());
            add.bind_text(7, finding.dump(-1, ' ', true));
            add.step();
        }
        database.execute("COMMIT");
        return report.at("report_id").get<std::string>();
    } catch (...) {
        try { database.execute("ROLLBACK"); } catch (...) {}
        throw;
    }
}

Json list_reports(const std::filesystem::path& database_path, std::size_t limit) {
    if (limit < 1U || limit > 1000U) throw Error("history limit must be between 1 and 1000");
    Database database(database_path);
    Statement query(database.get(), R"SQL(
        SELECT report_id, experiment_id, application_name, status, baseline_run_id,
            candidate_run_id, baseline_version, candidate_version, finding_count,
            uncertainty_count, stored_at
        FROM reports ORDER BY stored_at DESC, report_id DESC LIMIT ?
    )SQL");
    query.bind_integer(1, limit);
    Json result = Json::array();
    while (query.step() == SQLITE_ROW) {
        result.push_back(Json{
            {"report_id", column_text(query.get(), 0)},
            {"experiment_id", column_text(query.get(), 1)},
            {"application_name", column_text(query.get(), 2)},
            {"status", column_text(query.get(), 3)},
            {"baseline_run_id", column_text(query.get(), 4)},
            {"candidate_run_id", column_text(query.get(), 5)},
            {"baseline_version", column_text(query.get(), 6)},
            {"candidate_version", column_text(query.get(), 7)},
            {"finding_count", sqlite3_column_int64(query.get(), 8)},
            {"uncertainty_count", sqlite3_column_int64(query.get(), 9)},
            {"stored_at", column_text(query.get(), 10)}
        });
    }
    return result;
}

std::optional<Json> get_report(const std::filesystem::path& database_path, const std::string& report_id) {
    if (report_id.size() != 64U || !std::all_of(report_id.begin(), report_id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) throw Error("report_id must be a 64-character lowercase SHA-256 digest");
    Database database(database_path);
    Statement query(database.get(), "SELECT report_json FROM reports WHERE report_id=?");
    query.bind_text(1, report_id);
    if (query.step() != SQLITE_ROW) return std::nullopt;
    const auto report = parse_json_strict(column_text(query.get(), 0));
    validate_report(report);
    if (report.at("report_id") != report_id) throw Error("stored report ID does not match its history key");
    return std::optional<Json>{report};
}

bool delete_report(const std::filesystem::path& database_path, const std::string& report_id) {
    if (report_id.size() != 64U || !std::all_of(report_id.begin(), report_id.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    })) throw Error("report_id must be a 64-character lowercase SHA-256 digest");
    Database database(database_path);
    database.execute("BEGIN IMMEDIATE");
    try {
        Statement remove(database.get(), "DELETE FROM reports WHERE report_id=?");
        remove.bind_text(1, report_id);
        remove.step();
        const bool deleted = sqlite3_changes(database.get()) > 0;
        if (deleted) {
            database.execute(R"SQL(
                DELETE FROM runs
                WHERE NOT EXISTS (
                    SELECT 1 FROM report_runs WHERE report_runs.run_id = runs.run_id
                )
            )SQL");
        }
        database.execute("COMMIT");
        return deleted;
    } catch (...) {
        try { database.execute("ROLLBACK"); } catch (...) {}
        throw;
    }
}

} // namespace driftglass
