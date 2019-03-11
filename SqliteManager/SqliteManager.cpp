#include "SqliteManager.h"

EzSqlite::SqliteManager::SqliteManager()
{
    database_ = nullptr;
}

EzSqlite::SqliteManager::~SqliteManager()
{
    this->CloseDatabase();
}

EzSqlite::Errors EzSqlite::SqliteManager::CreateDatabase(
    _In_ const std::wstring& databasePath,
    _In_ DesiredAccess desiredAccess,
    _In_ CreationDisposition creationDisposition,
    _In_ const std::vector<std::string>& verifyTableStmtStringList,
    _In_opt_ const std::vector<std::string>* createTableStmtStringList /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;
    int openFlags = 0;

    std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
    std::string databasePathUtf8;

    auto raii = RAIIRegister([&] 
    {
        if (retValue == Errors::kUnsuccess)
        {
            this->CloseDatabase();
        }
    });

    // SQLite Database가 열려있는 경우
    if (database_ != nullptr)
    {
        if (creationDisposition == CreationDisposition::kOpenAlways ||
            creationDisposition == CreationDisposition::kOpenExisting)
        {
            // 열려있는 Database와 경로가 같고 테이블 구조가 같은 경우 kAlreadyOpen 리턴
            if ((databasePath_ == databasePath) && (VerifyTable_(verifyTableStmtStringList) == Errors::kSuccess))
            {
                retValue = Errors::kAlreadyOpen;
                return retValue;
            }
        }

        if (this->CloseDatabase() != Errors::kSuccess)
        {
            return retValue;
        }
    }

    //
    // 인자 검증 
    //
    
    // kReadOnly를 지정하려면 kOpenExisting 값이어야 됨
    if (desiredAccess == DesiredAccess::kReadOnly && creationDisposition != CreationDisposition::kOpenExisting)
    {
        return retValue;
    }

    // 테이블 생성 명령어를 지정하지 않으려면 kOpenExisting 값이어야 됨
    if (createTableStmtStringList == nullptr && creationDisposition != CreationDisposition::kOpenExisting)
    {
        return retValue;
    }

    if (creationDisposition == CreationDisposition::kCreateAlways)
    {
        if ((::DeleteFileW(databasePath.c_str()) == FALSE) && (GetLastError() != ERROR_FILE_NOT_FOUND))
        {
            return retValue;
        }
    }

    databasePathUtf8 = convert.to_bytes(databasePath);
    if (desiredAccess == DesiredAccess::kReadOnly)
    {
        openFlags = SQLITE_OPEN_READONLY;
    }
    else if (desiredAccess == DesiredAccess::kReadWrite)
    {
        openFlags = SQLITE_OPEN_READWRITE;
    }
    
    sqliteStatus = sqlite3_open_v2(databasePathUtf8.c_str(), &database_, openFlags, nullptr);
    if ((sqliteStatus != SQLITE_OK) || (VerifyTable_(verifyTableStmtStringList) != Errors::kSuccess))
    {
        // sqlite3_open_v2 함수가 실패해도 database_ 엔 값이 리턴 됨
        if (this->CloseDatabase() != Errors::kSuccess)
        {
            return retValue;
        }

        if (creationDisposition == CreationDisposition::kOpenExisting)
        {
            return retValue;
        }
        else if (creationDisposition == CreationDisposition::kOpenAlways)
        {
            if ((::DeleteFileW(databasePath.c_str()) == FALSE) && (GetLastError() != ERROR_FILE_NOT_FOUND))
            {
                return retValue;
            }
        }

        sqliteStatus = sqlite3_open_v2(databasePathUtf8.c_str(), &database_, openFlags | SQLITE_OPEN_CREATE, nullptr);
        if (sqliteStatus != SQLITE_OK)
        {
            return retValue;
        }
        
        // 테이블 생성
        if (createTableStmtStringList->size() == 0)
        {
            return retValue;
        }

        for (const auto& createTableStmtStringListEntry : *createTableStmtStringList)
        {
            if(GetStmtType_(createTableStmtStringListEntry.c_str()) != StmtType::kCreateTable)
            {
                return retValue;
            }

            if (this->ExecStmt(createTableStmtStringListEntry) != Errors::kSuccess)
            {
                return retValue;
            }
        }
    }

    retValue = PrepareInternalStmt_();
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    databasePath_ = databasePath;

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::CloseDatabase(
    _In_opt_ bool deleteDatabase, /*= false*/ // true여도 이미 닫힌 경우엔 제거 불가
    _In_opt_ bool resetPreparedStmtIndex /*= false*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    auto raii = RAIIRegister([&] 
    {
        if (retValue != Errors::kSuccess)
        {
            PrepareInternalStmt_();
        }
    });

    if (database_ == nullptr)
    {
        retValue = Errors::kSuccess;
        return retValue;
    }

    this->ClearPreparedStmt(resetPreparedStmtIndex);

    sqliteStatus = sqlite3_close(database_);
    if(sqliteStatus != SQLITE_OK)
    {
        /*sqlite3_stmt * stmt;
        while ((stmt = sqlite3_next_stmt(database_, NULL)) != NULL) {
        sqlite3_finalize(stmt);
        }*/

        return retValue;
    }

    database_ = nullptr;

    if (deleteDatabase == true)
    {
        if ((::DeleteFileW(databasePath_.c_str()) == FALSE) && (GetLastError() != ERROR_FILE_NOT_FOUND))
        {
            return retValue;
        }
    }    

    databasePath_.clear();

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::PrepareStmt(
    _In_ const std::string& stmtString, 
    _Out_opt_ uint32_t* preparedStmtIndex /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    StmtInfo stmtInfo;

    auto raii = RAIIRegister([&] 
    {
        if (retValue != Errors::kSuccess && stmtInfo.stmt != nullptr)
        {
            sqlite3_finalize(stmtInfo.stmt);
            stmtInfo.stmt = nullptr;
        }
    });

    if (database_ == nullptr)
    {
        return retValue;
    }

    sqliteStatus = SqlitePrepareV2_(
        database_,
        stmtString.c_str(),
        -1,
        &stmtInfo.stmt,
        nullptr
    );
    if (sqliteStatus != SQLITE_OK)
    {
        return retValue;
    }

    GetStmtInfo_(stmtInfo.stmt, stmtInfo.stmtType, stmtInfo.columnCount, stmtInfo.bindParameterCount);

    preparedStmtInfoList_.push_back(stmtInfo);
    preparedStmtIndexPointerList_.push_back(preparedStmtIndex);

    if (preparedStmtIndex != nullptr)
    {
        *preparedStmtIndex = static_cast<uint32_t>(preparedStmtInfoList_.size()) - 1;
    }

    retValue = Errors::kSuccess;
    return retValue;
}

void EzSqlite::SqliteManager::ClearPreparedStmt(
    _In_opt_ bool resetPreparedStmtIndex /*= false*/
)
{
    if (preparedStmtInfoList_.size() == 0)
    {
        return;
    }

    for (uint32_t preparedStmtIndex = 0; preparedStmtIndex < preparedStmtInfoList_.size(); preparedStmtIndex++)
    {
        if (preparedStmtInfoList_[preparedStmtIndex].stmt != nullptr)
        {
            sqlite3_finalize(preparedStmtInfoList_[preparedStmtIndex].stmt);
            preparedStmtInfoList_[preparedStmtIndex].stmt = nullptr;
        }

        if (resetPreparedStmtIndex == true)
        {
            // Prepared Statement는 순차적으로 저장되기 때문에 Pointer List Index와 Stmt Index가 같다.
            __try
            {
                if ((preparedStmtIndexPointerList_[preparedStmtIndex] != nullptr) &&
                    (*(preparedStmtIndexPointerList_[preparedStmtIndex]) == preparedStmtIndex))
                {
                    *(preparedStmtIndexPointerList_[preparedStmtIndex]) = static_cast<uint32_t>(StmtIndex::kNoIndex);
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    preparedStmtInfoList_.clear();
    preparedStmtIndexPointerList_.clear();    
}

EzSqlite::Errors EzSqlite::SqliteManager::FindPreparedStmt(
    _In_ const std::string& preparedStmtString, 
    _Out_ const StmtInfo*& preparedStmtInfo
)
{
    Errors retValue = Errors::kNotFound;
    
    if (preparedStmtInfoList_.size() == 0)
    {
        return retValue;
    }

    for (const auto& preparedStmtInfoListEntry : preparedStmtInfoList_)
    {
        if (strcmp(GetPreparedStmtString_(preparedStmtInfoListEntry.stmt), preparedStmtString.c_str()) == 0)
        {
            preparedStmtInfo = &preparedStmtInfoListEntry;
            
            retValue = Errors::kSuccess;
            break;
        }
    }

    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::FindPreparedStmt(
    _In_ uint32_t preparedStmtIndex,
    _Out_ const StmtInfo*& preparedStmtInfo
)
{
    Errors retValue = Errors::kNotFound;

    if (preparedStmtIndex == static_cast<uint32_t>(StmtIndex::kNoIndex))
    {
        return retValue;
    }

    if (preparedStmtInfoList_.size() <= preparedStmtIndex)
    {
        return retValue;
    }

    preparedStmtInfo = &(preparedStmtInfoList_[preparedStmtIndex]);

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::ExecStmt(
    _In_ const std::string& stmtString,
    _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList /*= nullptr */,
    _In_opt_ StepCallbackFunc* stmtStepCallback /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    const StmtInfo* preparedStmtInfo = nullptr;
    StmtInfo stmtInfo;

    auto raii = RAIIRegister([&] 
    {
        if (stmtInfo.stmt != nullptr)
        {
            sqlite3_finalize(stmtInfo.stmt);
            stmtInfo.stmt = nullptr;
        }
    });

    if (database_ == nullptr)
    {
        return retValue;
    }

    retValue = FindPreparedStmt(stmtString, preparedStmtInfo);
    if (retValue == Errors::kNotFound)
    {
        sqliteStatus = SqlitePrepareV2_(
            database_,
            stmtString.c_str(),
            -1,
            &stmtInfo.stmt,
            nullptr
        );
        if (sqliteStatus != SQLITE_OK)
        {
            retValue = Errors::kUnsuccess;
            return retValue;
        }

        GetStmtInfo_(stmtInfo.stmt, stmtInfo.stmtType, stmtInfo.columnCount, stmtInfo.bindParameterCount);
    }

    return ExecStmt_(&stmtInfo, stmtBindParameterInfoList, stmtStepCallback);
}

EzSqlite::Errors EzSqlite::SqliteManager::ExecStmt(
    _In_ uint32_t preparedStmtIndex, 
    _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList /*= nullptr */,
    _In_opt_ StepCallbackFunc* stmtStepCallback /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    const StmtInfo* stmtInfo = nullptr;

    if (database_ == nullptr)
    {
        return retValue;
    }

    retValue = FindPreparedStmt(preparedStmtIndex, stmtInfo);
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    return ExecStmt_(stmtInfo, stmtBindParameterInfoList, stmtStepCallback);
}

EzSqlite::Errors EzSqlite::SqliteManager::PrepareInternalStmt_()
{
    Errors retValue = Errors::kUnsuccess;

    auto raii = RAIIRegister([&] 
    {
        if (retValue != Errors::kSuccess)
        {
            this->ClearPreparedStmt();
        }
    });

    retValue = this->PrepareStmt("BEGIN;");
    if(retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("BEGIN IMMEDIATE;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("COMMIT;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("ROLLBACK;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = this->PrepareStmt("VACUUM;");
    if (retValue != Errors::kSuccess)
    {
        return retValue;
    }

    retValue = Errors::kSuccess;
    return retValue;
}

void EzSqlite::SqliteManager::GetStmtInfo_(
    _In_ sqlite3_stmt* stmt, 
    _Out_ StmtType& stmtType,
    _Out_ uint32_t& columnCount, 
    _Out_ uint32_t& bindParameterCount
)
{
    stmtType = StmtType::kUnknown;
    columnCount = 0;
    bindParameterCount = 0;

    if (stmt == nullptr)
    {
        return;
    }

    stmtType = GetStmtType_(GetPreparedStmtString_(stmt));
    columnCount = sqlite3_column_count(stmt);
    bindParameterCount = sqlite3_bind_parameter_count(stmt);

    return;
}

EzSqlite::StmtType EzSqlite::SqliteManager::GetStmtType_(
    _In_ const std::string::traits_type::char_type* stmtString
)
{
    uint32_t stmtStringLength = static_cast<uint32_t>(strlen(stmtString));

    if ((strlen("ALTER TABLE") <= stmtStringLength) && (_strnicmp("ALTER TABLE", stmtString, strlen("ALTER TABLE")) == 0))
    {
        return StmtType::kAlterTable;
    }
    else if ((strlen("ANALYZE") <= stmtStringLength) && (_strnicmp("ANALYZE", stmtString, strlen("ANALYZE")) == 0))
    {
        return StmtType::kAnalyze;
    }
    else if ((strlen("ATTACH") <= stmtStringLength) && (_strnicmp("ATTACH", stmtString, strlen("ATTACH")) == 0))
    {
        return StmtType::kAttach;
    }
    else if ((strlen("BEGIN") <= stmtStringLength) && (_strnicmp("BEGIN", stmtString, strlen("BEGIN")) == 0))
    {
        return StmtType::kBegin;
    }
    else if ((strlen("COMMIT") <= stmtStringLength) && (_strnicmp("COMMIT", stmtString, strlen("COMMIT")) == 0))
    {
        return StmtType::kCommit;
    }
    else if (((strlen("CREATE INDEX") <= stmtStringLength) && (_strnicmp("CREATE INDEX", stmtString, strlen("CREATE INDEX")) == 0)) ||
        ((strlen("CREATE UNIQUE INDEX") <= stmtStringLength) && (_strnicmp("CREATE UNIQUE INDEX", stmtString, strlen("CREATE UNIQUE INDEX")) == 0)))
    {
        return StmtType::kCreateIndex;
    }
    else if (((strlen("CREATE TABLE") <= stmtStringLength) && (_strnicmp("CREATE TABLE", stmtString, strlen("CREATE TABLE")) == 0)) ||
        ((strlen("CREATE TEMP TABLE") <= stmtStringLength) && (_strnicmp("CREATE TEMP TABLE", stmtString, strlen("CREATE TEMP TABLE")) == 0)) ||
        ((strlen("CREATE TEMPORARY TABLE") <= stmtStringLength) && (_strnicmp("CREATE TEMPORARY TABLE", stmtString, strlen("CREATE TEMPORARY TABLE")) == 0)))
    {
        return StmtType::kCreateTable;
    }
    else if (((strlen("CREATE TRIGGER") <= stmtStringLength) && (_strnicmp("CREATE TRIGGER", stmtString, strlen("CREATE TRIGGER")) == 0)) ||
        ((strlen("CREATE TEMP TRIGGER") <= stmtStringLength) && (_strnicmp("CREATE TEMP TRIGGER", stmtString, strlen("CREATE TEMP TRIGGER")) == 0)) ||
        ((strlen("CREATE TEMPORARY TRIGGER") <= stmtStringLength) && (_strnicmp("CREATE TEMPORARY TRIGGER", stmtString, strlen("CREATE TEMPORARY TRIGGER")) == 0)))
    {
        return StmtType::kCreateTrigger;
    }
    else if (((strlen("CREATE VIEW") <= stmtStringLength) && (_strnicmp("CREATE VIEW", stmtString, strlen("CREATE VIEW")) == 0)) ||
        ((strlen("CREATE TEMP VIEW") <= stmtStringLength) && (_strnicmp("CREATE TEMP VIEW", stmtString, strlen("CREATE TEMP VIEW")) == 0)) ||
        ((strlen("CREATE TEMPORARY VIEW") <= stmtStringLength) && (_strnicmp("CREATE TEMPORARY VIEW", stmtString, strlen("CREATE TEMPORARY VIEW")) == 0)))
    {
        return StmtType::kCreateView;
    }
    else if ((strlen("CREATE VIRTUAL TABLE") <= stmtStringLength) && (_strnicmp("CREATE VIRTUAL TABLE", stmtString, strlen("CREATE VIRTUAL TABLE")) == 0))
    {
        return StmtType::kCreateVirtualTable;
    }
    else if ((strlen("DELETE FROM") <= stmtStringLength) && (_strnicmp("DELETE FROM", stmtString, strlen("DELETE FROM")) == 0))
    {
        return StmtType::kDelete;
    }
    else if ((strlen("DETACH") <= stmtStringLength) && (_strnicmp("DETACH", stmtString, strlen("DETACH")) == 0))
    {
        return StmtType::kDetach;
    }
    else if ((strlen("DROP INDEX") <= stmtStringLength) && (_strnicmp("DROP INDEX", stmtString, strlen("DROP INDEX")) == 0))
    {
        return StmtType::kDropIndex;
    }
    else if ((strlen("DROP TABLE") <= stmtStringLength) && (_strnicmp("DROP TABLE", stmtString, strlen("DROP TABLE")) == 0))
    {
        return StmtType::kDropTable;
    }
    else if ((strlen("DROP TRIGGER") <= stmtStringLength) && (_strnicmp("DROP TRIGGER", stmtString, strlen("DROP TRIGGER")) == 0))
    {
        return StmtType::kDropTrigger;
    }
    else if ((strlen("DROP VIEW") <= stmtStringLength) && (_strnicmp("DROP VIEW", stmtString, strlen("DROP VIEW")) == 0))
    {
        return StmtType::kDropView;
    }
    else if ((strlen("INSERT INTO") <= stmtStringLength) && (_strnicmp("INSERT INTO", stmtString, strlen("INSERT INTO")) == 0))
    {
        return StmtType::kInsert;
    }
    else if ((strlen("PRAGMA") <= stmtStringLength) && (_strnicmp("PRAGMA", stmtString, strlen("PRAGMA")) == 0))
    {
        return StmtType::kPragma;
    }
    else if ((strlen("REINDEX") <= stmtStringLength) && (_strnicmp("REINDEX", stmtString, strlen("REINDEX")) == 0))
    {
        return StmtType::kReindex;
    }
    else if ((strlen("RELEASE") <= stmtStringLength) && (_strnicmp("RELEASE", stmtString, strlen("RELEASE")) == 0))
    {
        return StmtType::kRelease;
    }
    else if ((strlen("ROLLBACK") <= stmtStringLength) && (_strnicmp("ROLLBACK", stmtString, strlen("ROLLBACK")) == 0))
    {
        return StmtType::kRollback;
    }
    else if ((strlen("SAVEPOINT") <= stmtStringLength) && (_strnicmp("SAVEPOINT", stmtString, strlen("SAVEPOINT")) == 0))
    {
        return StmtType::kSavepoint;
    }
    else if ((strlen("SELECT") <= stmtStringLength) && (_strnicmp("SELECT", stmtString, strlen("SELECT")) == 0))
    {
        return StmtType::kSelect;
    }
    else if ((strlen("UPDATE") <= stmtStringLength) && (_strnicmp("UPDATE", stmtString, strlen("UPDATE")) == 0))
    {
        return StmtType::kUpdate;
    }
    else if ((strlen("VACUUM") <= stmtStringLength) && (_strnicmp("VACUUM", stmtString, strlen("VACUUM")) == 0))
    {
        return StmtType::kVacuum;
    }
    else
    {
        return StmtType::kUnknown;
    }
}

const std::string::traits_type::char_type* EzSqlite::SqliteManager::GetPreparedStmtString_(
    _In_ sqlite3_stmt* stmt,
    _In_opt_ bool withBoundParameters /*= false*/
)
{
    if (withBoundParameters == false)
    {
        return sqlite3_sql(stmt);
    }
    else
    {
        return sqlite3_expanded_sql(stmt);
    }
}

EzSqlite::Errors EzSqlite::SqliteManager::ExecStmt_(
    _In_ const StmtInfo* stmtInfo,
    _In_opt_ const std::vector<StmtBindParameterInfo>* stmtBindParameterInfoList /*= nullptr */,
    _In_opt_ StepCallbackFunc* stmtStepCallback /*= nullptr*/
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;
    uint32_t stepCount = 0;
    CallbackErrors callbackStatus;

    auto raii = RAIIRegister([&]
    {
        sqlite3_clear_bindings(stmtInfo->stmt);
        sqlite3_reset(stmtInfo->stmt);
    });

    // Bind Parameter
    if (stmtInfo->bindParameterCount != 0)
    {
        if (stmtBindParameterInfoList == nullptr)
        {
            return retValue;
        }
        else if (stmtInfo->bindParameterCount != stmtBindParameterInfoList->size())
        {
            return retValue;
        }

        retValue = StmtBindParameter_(stmtInfo, *stmtBindParameterInfoList);
        if (retValue != Errors::kSuccess)
        {
            return retValue;
        }
    }

    sqliteStatus = SqliteStep_(stmtInfo->stmt);
    stepCount++;   

    do
    {
        if (sqliteStatus == SQLITE_ROW && stmtStepCallback != nullptr)
        {
            callbackStatus = (*stmtStepCallback)(*stmtInfo);
            if (callbackStatus == CallbackErrors::kStop)
            {
                retValue = Errors::kStopCallback;
                break;
            }
            else if (callbackStatus == CallbackErrors::kFail)
            {
                retValue = Errors::kFailCallback;
                break;
            }
        }
        else if (sqliteStatus == SQLITE_DONE)
        {
            if ((stmtInfo->stmtType == StmtType::kSelect) && (stepCount == 1))
            {
                retValue = Errors::kNotFound;
            }
            else
            {
                retValue = Errors::kSuccess;
            }
            
            break;
        }
        else
        {
            retValue = Errors::kUnsuccess;
            break;
        }

        sqliteStatus = SqliteStep_(stmtInfo->stmt);
        stepCount++;

    } while (true);

    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::StmtBindParameter_(
    _In_ const StmtInfo* stmtInfo, 
    _In_ const std::vector<StmtBindParameterInfo>& stmtBindParameterInfoList
)
{
    Errors retValue = Errors::kUnsuccess;

    int sqliteStatus = SQLITE_ERROR;

    /*
        가장 왼쪽의 SQL Parameter Index는 1
    */
    uint32_t parameterIndex = 1;

    for (const auto& stmtBindParameterInfoListEntry : stmtBindParameterInfoList)
    {
        switch (stmtBindParameterInfoListEntry.dataType)
        {
        case StmtDataType::kInteger:
            if (stmtBindParameterInfoListEntry.dataByteSize == sizeof(int8_t))
            {
                if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kSigned)
                {
                    sqliteStatus = sqlite3_bind_int(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const int8_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
                else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kUnsigned)
                {
                    sqliteStatus = sqlite3_bind_int(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const uint8_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
            }
            else if (stmtBindParameterInfoListEntry.dataByteSize == sizeof(int16_t))
            {
                if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kSigned)
                {
                    sqliteStatus = sqlite3_bind_int(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const int16_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
                else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kUnsigned)
                {
                    sqliteStatus = sqlite3_bind_int(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const uint16_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
            }
            else if (stmtBindParameterInfoListEntry.dataByteSize == sizeof(int32_t))
            {
                if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kSigned)
                {
                    sqliteStatus = sqlite3_bind_int(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const int32_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
                else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kUnsigned)
                {
                    sqliteStatus = sqlite3_bind_int(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const uint32_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
            }
            else if (stmtBindParameterInfoListEntry.dataByteSize == sizeof(int64_t))
            {
                if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kSigned)
                {
                    sqliteStatus = sqlite3_bind_int64(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const int64_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
                else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kUnsigned)
                {
                    sqliteStatus = sqlite3_bind_int64(
                        stmtInfo->stmt,
                        parameterIndex++,
                        *reinterpret_cast<const uint64_t*>(stmtBindParameterInfoListEntry.data)
                    );
                }
            }

            break;

        case StmtDataType::kFloat:
            if (stmtBindParameterInfoListEntry.dataByteSize == sizeof(float_t))
            {
                sqliteStatus = sqlite3_bind_double(
                    stmtInfo->stmt,
                    parameterIndex++,
                    *reinterpret_cast<const float_t*>(stmtBindParameterInfoListEntry.data)
                );
            }
            else if (stmtBindParameterInfoListEntry.dataByteSize == sizeof(double_t))
            {
                sqliteStatus = sqlite3_bind_double(
                    stmtInfo->stmt,
                    parameterIndex++,
                    *reinterpret_cast<const double_t*>(stmtBindParameterInfoListEntry.data)
                );
            }

            break;

        case StmtDataType::kText:
            if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorStatic)
            {
                sqliteStatus = sqlite3_bind_text(
                    stmtInfo->stmt,
                    parameterIndex++,
                    reinterpret_cast<const std::string::traits_type::char_type*>(stmtBindParameterInfoListEntry.data),
                    stmtBindParameterInfoListEntry.dataByteSize == 0 ? -1 : stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_STATIC
                );
            }
            else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kNone ||
                stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorTransient)
            {
                sqliteStatus = sqlite3_bind_text(
                    stmtInfo->stmt,
                    parameterIndex++,
                    reinterpret_cast<const std::string::traits_type::char_type*>(stmtBindParameterInfoListEntry.data),
                    stmtBindParameterInfoListEntry.dataByteSize == 0 ? -1 : stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_TRANSIENT
                );
            }

            break;

        case StmtDataType::kBlob:
            if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorStatic)
            {
                sqliteStatus = sqlite3_bind_blob(
                    stmtInfo->stmt,
                    parameterIndex++,
                    stmtBindParameterInfoListEntry.data,
                    stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_STATIC
                );
            }
            else if (stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kNone ||
                stmtBindParameterInfoListEntry.options == StmtBindParameterOptions::kDestructorTransient)
            {
                sqliteStatus = sqlite3_bind_blob(
                    stmtInfo->stmt,
                    parameterIndex++,
                    stmtBindParameterInfoListEntry.data,
                    stmtBindParameterInfoListEntry.dataByteSize,
                    SQLITE_TRANSIENT
                );
            }

            break;

        case StmtDataType::kNull:
            sqliteStatus = sqlite3_bind_null(
                stmtInfo->stmt,
                parameterIndex++
            );

            break;
        }

        if (sqliteStatus != SQLITE_OK)
        {
            return retValue;
        }
    }

    retValue = Errors::kSuccess;
    return retValue;
}

EzSqlite::Errors EzSqlite::SqliteManager::VerifyTable_(
    _In_ const std::vector<std::string>& verifyTableStmtStringList
)
{
    Errors retValue = Errors::kFailedVerifyTable;

    int sqliteStatus = SQLITE_ERROR;

    sqlite3_stmt* stmt = nullptr;

    auto raii = RAIIRegister([&] 
    {
        if (stmt != nullptr)
        {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    });

    if (verifyTableStmtStringList.size() == 0)
    {
        retValue = Errors::kSuccess;
        return retValue;
    }

    retValue = Errors::kSuccess;
    for (const auto& verifyTableStmtStringListEntry : verifyTableStmtStringList)
    {
        if(GetStmtType_(verifyTableStmtStringListEntry.c_str()) != StmtType::kSelect)
        {
            retValue = Errors::kFailedVerifyTable;
            break;
        }

        sqliteStatus = SqlitePrepareV2_(
            database_,
            verifyTableStmtStringListEntry.c_str(),
            -1,
            &stmt,
            nullptr
        );
        if (sqliteStatus != SQLITE_OK)
        {
            retValue = Errors::kFailedVerifyTable;
            break;
        }

        sqlite3_finalize(stmt);
        stmt = nullptr;
    }

    return retValue;
}

int EzSqlite::SqliteManager::SqliteStep_(
    sqlite3_stmt* stmt,
    uint32_t timeOutSecond /*= kBusyTimeOutSecond*/
)
{
    int sqliteStatus = SQLITE_ERROR;

    const double_t intervalTime = 0.5;
    double_t stayTime = 0;

    while (true)
    {
        sqliteStatus = sqlite3_step(
            stmt
        );

        if ((sqliteStatus != SQLITE_BUSY) || (stayTime >= timeOutSecond))
        {
            break;
        }

        Sleep(static_cast<uint32_t>(intervalTime * 1000));
        stayTime += intervalTime;
    }

    return sqliteStatus;
}

int EzSqlite::SqliteManager::SqlitePrepareV2_(
    sqlite3 *db,
    const char *zSql,
    int nBytes,
    sqlite3_stmt **ppStmt,
    const char **pzTail,
    uint32_t timeOutSecond /*= kBusyTimeOutSecond*/
)
{
    int sqliteStatus = SQLITE_ERROR;

    const double_t intervalTime = 0.5;
    double_t stayTime = 0;

    while (true)
    {
        sqliteStatus = sqlite3_prepare_v2(
            db,
            zSql,
            nBytes,
            ppStmt,
            pzTail
        );
        
        if ((sqliteStatus != SQLITE_BUSY) || (stayTime >= timeOutSecond))
        {
            break;
        }

        Sleep(static_cast<uint32_t>(intervalTime * 1000));
        stayTime += intervalTime;

        std::cout << "StayTime: " << stayTime << std::endl;
    }

    return sqliteStatus;
}