#ifndef __H_SQLITEMANAGER_H__
#define __H_SQLITEMANAGER_H__

#include "SqliteManagerErrors.h"

#include "SQLite/sqlite3.h"

#include <windows.h>
#include <functional>
#include <codecvt>
#include <vector>

#include <iostream>

namespace EzSqlite
{

/* std::string���� �� ���� ���� utf8 */

enum class DesiredAccess
{
    kReadOnly,
    kReadWrite
};

enum class CreationDisposition
{
    kCreateAlways,  // DB ���� ������ ���� ����
    kOpenAlways,    // DB ���� �ִ� ��� ����, ������ ��� ���� ���� �� ����
    kOpenExisting   // DB ���� �ִ� ��� ����, ������ ��� ����
};

enum class StmtIndex
{
    kBegin,     // BEGIN;
    kCommit,    // COMMIT;
    kRollback,  // ROLLBACK;
    kVacuum,    // VACUUM;

    kBasicStmtNumber,
    kNoIndex = -1
};

// SQLite���� �����ϴ� ��� Syntax�� ������� ���� (���� ���� ���¸� ���)
enum class StmtType
{
    kAlterTable,            // ALTER TABLE
    kAnalyze,               // ANALYZE
    kAttach,                // ATTACH
    kBegin,                 // BEGIN
    kCommit,                // COMMIT
    kCreateIndex,           // CREATE INDEX, CREATE UNIQUE INDEX
    kCreateTable,           // CREATE TABLE, CREATE TEMP TABLE, CREATE TEMPORARY TABLE
    kCreateTrigger,         // CREATE TRIGGER, CREATE TEMP TRIGGER, CREATE TEMPORARY TRIGGER
    kCreateView,            // CREATE VIEW, CREATE TEMP VIEW, CREATE TEMPORARY VIEW
    kCreateVirtualTable,    // CREATE VIRTUAL TABLE
    kDelete,                // DELETE FROM
    kDetach,                // DETACH
    kDropIndex,             // DROP INDEX
    kDropTable,             // DROP TABLE
    kDropTrigger,           // DROP TRIGGER
    kDropView,              // DROP VIEW
    kInsert,                // INSERT INTO
    kPragma,                // PRAGMA
    kReindex,               // REINDEX
    kRelease,               // RELEASE
    kRollback,              // ROLLBACK
    kSavepoint,             // SAVEPOINT
    kSelect,                // SELECT
    kUpdate,                // UPDATE
    kVacuum,                // VACUUM
    kUnknown
};

enum class StmtDataType
{
    kInteger = SQLITE_INTEGER,
    kFloat = SQLITE_FLOAT,
    kText = SQLITE_TEXT,
    kBlob = SQLITE_BLOB,
    kNull = SQLITE_NULL
};

enum class StmtBindParameterOptions
{
    // StmtDataType::kText, StmtDataType::kBlob
    kDestructorStatic,    // SQLITE_STATIC
    kDestructorTransient, // SQLITE_TRANSIENT

    // StmtDataType::kInteger
    kSigned,
    kUnsigned,

    kNone
};

struct StmtBindParameterInfo
{
    StmtBindParameterInfo()
    {
        data = nullptr;
        dataByteSize = 0;
        options = StmtBindParameterOptions::kNone;
    };

    const void* data;
    StmtDataType dataType;
    uint32_t dataByteSize;              // text interface�� ��� Default�� -1 (null���� ����)
    StmtBindParameterOptions options;   // blob�� text interface�� ��� Default�� kDestructorTransient (�� ����)
};

struct StmtInfo
{
    StmtInfo()
    {
        stmt = nullptr;
        columnCount = 0;
        bindParameterCount = 0;
    };

    mutable sqlite3_stmt* stmt; // �ܺ� ���̺귯�� ������ mutable ����
    StmtType stmtType;
    uint32_t columnCount;
    uint32_t bindParameterCount;
};

typedef std::function<CallbackErrors(const StmtInfo&)> StepCallbackFunc;

class SqliteManager
{
public:
    SqliteManager();
    ~SqliteManager();

    Errors CreateDatabase(
        _In_ const std::wstring& databasePath,
        _In_ DesiredAccess desiredAccess,
        _In_ CreationDisposition creationDisposition,
        _In_ const std::vector<std::string>& verifyTableStmtStringList,
        _In_opt_ const std::vector<std::string>* createTableStmtStringList = nullptr
    );

    Errors CloseDatabase(_In_opt_ bool deleteDatabase = false, _In_opt_ bool resetPreparedStmtIndex = false);

    Errors PrepareStmt(_In_ const std::string& stmtString, _Out_opt_ uint32_t* preparedStmtIndex = nullptr);
    void ClearPreparedStmt(_In_opt_ bool resetPreparedStmtIndex = false);

    // preparedStmtInfo�� ���ϵǴ� StmtInfo �ּ� ���� vector �� �ּ��̹Ƿ� preparedStmtInfoList_�� �� ���� �� ������ ���� ��.
    // ��, �����ϰ� ����Ϸ��� ����Ϸ��� ���� �����س��� ���°� ����.
    Errors FindPreparedStmt(_In_ const std::string& preparedStmtString, _Out_ const StmtInfo*& preparedStmtInfo);
    Errors FindPreparedStmt(_In_ uint32_t preparedStmtIndex, _Out_ const StmtInfo*& preparedStmtInfo);

    Errors ExecStmt(
        _In_ const std::string& stmtString, 
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );
    Errors ExecStmt(
        _In_ uint32_t preparedStmtIndex, 
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );

private:
    class RAIIRegister
    {
    private:
        std::function<void()> raiiFunction_;

    public:
        RAIIRegister(std::function<void()> raiiFunc) : raiiFunction_(raiiFunc)
        {

        }
        ~RAIIRegister()
        {
            raiiFunction_();
        }
    };

private:
    Errors PrepareInternalStmt_();

    void GetStmtInfo_(_In_ sqlite3_stmt* stmt, _Out_ StmtType& stmtType, _Out_ uint32_t& columnCount, _Out_ uint32_t& bindParameterCount);
    StmtType GetStmtType_(_In_ const std::string::traits_type::char_type* stmtString);
    const std::string::traits_type::char_type* GetPreparedStmtString_(_In_ sqlite3_stmt* stmt, _In_opt_ bool withBoundParameters = false);    

    Errors ExecStmt_(
        _In_ const StmtInfo* stmtInfo,
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );

    Errors StmtBindParameter_(_In_ const StmtInfo* stmtInfo, _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList);
    Errors VerifyTable_(_In_ const std::vector<std::string>& verifyTableStmtStringList);

private:
    std::wstring databasePath_;
    sqlite3* database_;

    std::vector<StmtInfo> preparedStmtInfoList_;
    std::vector<uint32_t*> preparedStmtIndexPointerList_;
};

} // namespace EzSqlite

#endif // #ifndef __H_SQLITEMANAGER_H__