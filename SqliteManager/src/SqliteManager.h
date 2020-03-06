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
    sqlite3_update_hook 콜백 함수

    userContext: sqlite3_update_hook 세번째 파라미터의 복사본
    actionCode: 콜백을 발생시킨 Action (SQLITE_INSERT, SQLITE_DELETE, SQLITE_UPDATE)
    dbName: 영향을 받는 Database 이름
    tableName: 영향을 받는 Table 이름
    rowid: 영향을 받은 rowid 값 (업데이트의 경우, 업데이트가 발생한 후의 rowid)

    내부 시스템 테이블이 수정되는 경우는 콜백 호출되지 않음 (sqlite_master, sqlite_sequence)
    WITHOUT ROWID 테이블의 경우 콜백이 호출되지 않음
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

/* std::string으로 된 값은 전부 utf8 */

enum class DesiredAccess
{
    kReadOnly,
    kReadWrite
};

enum class CreationDisposition
{
    kCreateAlways,  // DB 파일 무조건 새로 생성
    kOpenAlways,    // DB 파일 있는 경우 열고, 실패할 경우 새로 생성 후 열기
    kOpenExisting   // DB 파일 있는 경우 열고, 실패할 경우 실패
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

// SQLite에서 제공하는 모든 Syntax를 고려하진 않음 (많이 쓰는 형태만 고려)
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
    uint32_t dataByteSize;              // text interface인 경우 Default는 -1 (null까지 읽음)
    StmtBindParameterOptions options;   // blob과 text interface인 경우 Default는 kDestructorTransient (값 복사)
};

struct StmtInfo
{
    StmtInfo()
    {
        stmt = nullptr;
        columnCount = 0;
        bindParameterCount = 0;
    };

    mutable sqlite3_stmt* stmt; // 외부 라이브러리 변수라서 mutable 선언
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

    // preparedStmtInfo에 리턴되는 StmtInfo 주소 값은 vector 내 주소이므로 preparedStmtInfoList_에 값 변동 시 쓰레기 값이 됨.
    // 즉, 안전하게 사용하려면 사용하려는 값을 복사해놓고 쓰는게 좋음.
    Errors FindPreparedStmt(_In_ const std::string& preparedStmtString, _Out_ const StmtInfo*& preparedStmtInfo);
    Errors FindPreparedStmt(_In_ uint32_t preparedStmtIndex, _Out_ const StmtInfo*& preparedStmtInfo);

    /*
        명령문을 Prepared하고 데이터를 Bind해서 처리하는 이유
        1. Insert 해야 될 데이터가 유동적인 경우 처리하기가 편함
        2. 내부 처리 구문으로 재컴파일 할 필요가 없기 때문에 속도가 빠름
        3. Binding을 하게되면 문자열에 따옴표를 사용하지 않게 되므로 SQL Injection 공격의 위험이 줄어듦
        4. 큰 문자열을 구문 분석하거나 복사 할 필요가 없으므로 속도가 빠름
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

    // stmtStepCallback 콜백에서 PrepareStmt 메서드 호출이 발생하면 preparedStmtInfoList_ 주소들이 다 바뀌므로 stmtInfo를 call-by-value로 받음
    Errors ExecStmt_(
        _In_ const StmtInfo stmtInfo,
        _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList = nullptr,
        _In_opt_ StepCallbackFunc* stmtStepCallback = nullptr
    );

    Errors StmtBindParameter_(_In_ const StmtInfo& stmtInfo, _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList);
    Errors PragmaStmtBindParameter_(_In_ const StmtInfo& stmtInfo, _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList, _Out_ std::string& pragmaStmtString);
    Errors VerifyTable_(_In_ const std::vector<std::string>& verifyTableStmtStringList);

    // sqlite3_XXX 랩핑 함수
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