/*!
 * \author Ezbeat, Ji Hoon Park
 */

#pragma once

#include "SqliteManagerErrors.h"

#include "SQLite/sqlite3.h"

#include <windows.h>
#include <functional>
#include <codecvt>
#include <vector>

#include <iostream>

namespace EzSqlite
{

/*
    sqlite3_update_hook �ݹ� �Լ�

    userContext: sqlite3_update_hook ����° �Ķ������ ���纻
    actionCode: �ݹ��� �߻���Ų Action (SQLITE_INSERT, SQLITE_DELETE, SQLITE_UPDATE)
    dbName: ������ �޴� Database �̸�
    tableName: ������ �޴� Table �̸�
    rowid: ������ ���� rowid �� (������Ʈ�� ���, ������Ʈ�� �߻��� ���� rowid)

    ���� �ý��� ���̺��� �����Ǵ� ���� �ݹ� ȣ����� ���� (sqlite_master, sqlite_sequence)
    WITHOUT ROWID ���̺��� ��� �ݹ��� ȣ����� ����
*/
enum class CallbackActionCode
{
    kInsert = SQLITE_INSERT,
    kDelete = SQLITE_DELETE,
    kUpdate = SQLITE_UPDATE
};

typedef void (*FPDataChangeNotificationCallback)(
    void* userContext,
    CallbackActionCode actionCode,
    char const* dbName,
    char const* tableName,
    sqlite_int64 rowid
    );

const uint32_t kBusyTimeOutSecond = 30;

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
    kBegin,                 // BEGIN;
    kBeginImmediate,        // BEGIN IMMEDIATE;
    kCommit,                // COMMIT;
    kRollback,              // ROLLBACK;
    kVacuum,                // VACUUM;

    kBasicStmtNumber,
    kNoIndex = -1
};

// SQLite���� �����ϴ� ��� Syntax�� ������� ���� (���� ���� ���¸� ���)
enum class StmtType
{
    kAlterTable,            // ALTER TABLE
    kAnalyze,               // ANALYZE
    kAttach,                // ATTACH
    kBegin,                 // BEGIN, BEGIN IMMEDIATE
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
    std::string stmtString;
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
        _In_opt_ FPDataChangeNotificationCallback dataChangeNotificationCallback,
        _In_opt_ void* dataChangeNotificationCallbackUserContext,
        _In_ const std::vector<std::string>& verifyTableStmtStringList,
        _In_opt_ const std::vector<std::string>* createTableStmtStringList = nullptr
    );

    Errors CloseDatabase(_In_opt_ bool deleteDatabase = false, _In_opt_ bool resetPreparedStmtIndex = false);

    Errors PrepareStmt(_In_ const std::string& stmtString, _In_opt_ uint32_t prepareFlags = SQLITE_PREPARE_PERSISTENT, _Out_opt_ uint32_t* preparedStmtIndex = nullptr);
    void ClearPreparedStmt(_In_opt_ bool resetPreparedStmtIndex = false);

    // preparedStmtInfo�� ���ϵǴ� StmtInfo �ּ� ���� vector �� �ּ��̹Ƿ� preparedStmtInfoList_�� �� ���� �� ������ ���� ��.
    // ��, �����ϰ� ����Ϸ��� ����Ϸ��� ���� �����س��� ���°� ����.
    Errors FindPreparedStmt(_In_ const std::string& preparedStmtString, _Out_ const StmtInfo*& preparedStmtInfo);
    Errors FindPreparedStmt(_In_ uint32_t preparedStmtIndex, _Out_ const StmtInfo*& preparedStmtInfo);

    /*
        ��ɹ��� Prepared�ϰ� �����͸� Bind�ؼ� ó���ϴ� ����
        1. Insert �ؾ� �� �����Ͱ� �������� ��� ó���ϱⰡ ����
        2. ���� ó�� �������� �������� �� �ʿ䰡 ���� ������ �ӵ��� ����
        3. Binding�� �ϰԵǸ� ���ڿ��� ����ǥ�� ������� �ʰ� �ǹǷ� SQL Injection ������ ������ �پ��
        4. ū ���ڿ��� ���� �м��ϰų� ���� �� �ʿ䰡 �����Ƿ� �ӵ��� ����
    */
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

    void GetStmtInfo_(_Inout_ StmtInfo& stmtInfo);
    StmtType GetStmtType_(_In_ const std::string::traits_type::char_type* stmtString);
    const std::string::traits_type::char_type* GetPreparedStmtString_(_In_ sqlite3_stmt* stmt, _In_opt_ bool withBoundParameters = false);
    void SetPragmaStmtInfo_(_In_ const std::string& stmtString, _Out_ StmtInfo& stmtInfo);

    // stmtStepCallback �ݹ鿡�� PrepareStmt �޼��� ȣ���� �߻��ϸ� preparedStmtInfoList_ �ּҵ��� �� �ٲ�Ƿ� stmtInfo�� call-by-value�� ����
    Errors ExecStmt_(
        _In_ const StmtInfo stmtInfo,
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );

    Errors StmtBindParameter_(_In_ const StmtInfo& stmtInfo, _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList);
    Errors PragmaStmtBindParameter_(_In_ const StmtInfo& stmtInfo, _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList, _Out_ std::string& pragmaStmtString);
    Errors VerifyTable_(_In_ const std::vector<std::string>& verifyTableStmtStringList);

    // sqlite3_XXX ���� �Լ�
    int SqliteStep_(sqlite3_stmt* stmt, uint32_t timeOutSecond = kBusyTimeOutSecond);
    int SqlitePrepareV2_(
        sqlite3* db,
        const char* zSql,
        int nBytes,
        sqlite3_stmt** ppStmt,
        const char** pzTail,
        uint32_t timeOutSecond = kBusyTimeOutSecond
    );
    int SqlitePrepareV3_(
        sqlite3* db,
        const char* zSql,
        int nBytes,
        unsigned int prepFlags,
        sqlite3_stmt** ppStmt,
        const char** pzTail,
        uint32_t timeOutSecond = kBusyTimeOutSecond
    );
    void SqliteUpdateHook_(
        sqlite3* db,
        FPDataChangeNotificationCallback dataChangeNotificationCallback,
        void* userContext
    );

private:
    std::wstring databasePath_;
    sqlite3* database_;

    std::vector<StmtInfo> preparedStmtInfoList_;
    std::vector<uint32_t*> preparedStmtIndexPointerList_;
};

} // namespace EzSqlite