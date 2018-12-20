#include "SqliteManager.h"

const uint32_t kEventTableNumber = 7;

const std::string kCommonColumnsName = "C_EUID, C_TimeStamp, C_Task, C_Opcode, C_ProcessId, C_ProcessId_PUID, C_ThreadId, C_ThreadId_TUID";

const std::string kFileIoEventTableName = "FILEIOEVENT_TB";
const std::string kFileIoEventColumnsName = "ED_TTid, ED_TTid_TUID, ED_OpenPath, ED_OriginalPath";

const std::string kProcessEventTableName = "PROCESSEVENT_TB";
const std::string kProcessEventColumnsName = "ED_ProcessId, ED_ProcessId_PUID, ED_ParentId, ED_ParentId_PUID, ED_SessionId, ED_ImageFileName, ED_CommandLine";

const std::string kImageEventTableName = "IMAGEEVENT_TB";
const std::string kImageEventColumnsName = "ED_ImageBase, ED_ImageSize, ED_ProcessId, ED_ProcessId_PUID, ED_FileName";

const std::string kThreadEventTableName = "THREADEVENT_TB";
const std::string kThreadEventColumnsName = "ED_ProcessId, ED_ProcessId_PUID, ED_TThreadId, ED_TThreadId_TUID, ED_Win32StartAddr";

const std::string kRegistryEventTableName = "REGISTRYEVENT_TB";
const std::string kRegistryEventColumnsName = "ED_Status, ED_KeyPath, ED_ValueName, ED_Artifact";

const std::string kTcpEventTableName = "TCPIPEVENT_TB";
const std::string kTcpEventColumnsName = "ED_PID, ED_PID_PUID, ED_size, ED_AccrueSize, ED_TrafficCount, ED_daddr, ED_saddr, ED_dport, ED_sport";

const std::string kUdpEventTableName = "UDPIPEVENT_TB";

const std::string kUdpEventColumnsName = "ED_PID, ED_PID_PUID, ED_size, ED_AccrueSize, ED_TrafficCount, ED_daddr, ED_saddr, ED_dport, ED_sport";
const std::string kCopyTempTableStmtStringList[kEventTableNumber] =
{
    "INSERT INTO " + kFileIoEventTableName + "_TEMP SELECT * FROM " + kFileIoEventTableName + " WHERE C_TimeStamp >= ? AND C_TimeStamp <= ?;",
    "INSERT INTO " + kProcessEventTableName + "_TEMP SELECT * FROM " + kProcessEventTableName + " WHERE C_TimeStamp >= ? AND C_TimeStamp <= ?;",
    "INSERT INTO " + kImageEventTableName + "_TEMP SELECT * FROM " + kImageEventTableName + " WHERE C_TimeStamp >= ? AND C_TimeStamp <= ?;",
    "INSERT INTO " + kThreadEventTableName + "_TEMP SELECT * FROM " + kThreadEventTableName + " WHERE C_TimeStamp >= ? AND C_TimeStamp <= ?;",
    "INSERT INTO " + kRegistryEventTableName + "_TEMP SELECT * FROM " + kRegistryEventTableName + " WHERE C_TimeStamp >= ? AND C_TimeStamp <= ?;",
    "INSERT INTO " + kTcpEventTableName + "_TEMP SELECT * FROM " + kTcpEventTableName + " WHERE C_TimeStamp >= ? AND C_TimeStamp <= ?;",
    "INSERT INTO " + kUdpEventTableName + "_TEMP SELECT * FROM " + kUdpEventTableName + " WHERE C_TimeStamp >= ? AND C_TimeStamp <= ?;"
};

const std::vector<std::string> kCheckTableStmtStringList =
{
    "SELECT " + kCommonColumnsName + " " + kFileIoEventColumnsName + " FROM " + kFileIoEventTableName + ";",
    "SELECT " + kCommonColumnsName + " " + kProcessEventColumnsName + " FROM " + kProcessEventTableName + ";",
    "SELECT " + kCommonColumnsName + " " + kImageEventColumnsName + " FROM " + kImageEventTableName + ";",
    "SELECT " + kCommonColumnsName + " " + kThreadEventColumnsName + " FROM " + kThreadEventTableName + ";",
    "SELECT " + kCommonColumnsName + " " + kRegistryEventColumnsName + " FROM " + kRegistryEventTableName + ";",
    "SELECT " + kCommonColumnsName + " " + kTcpEventColumnsName + " FROM " + kTcpEventTableName + ";",
    "SELECT " + kCommonColumnsName + " " + kUdpEventColumnsName + " FROM " + kUdpEventTableName + ";"
};

EzSqlite::CallbackErrors StepCallback(_In_ const EzSqlite::StmtInfo& stmtInfo)
{
    EzSqlite::CallbackErrors retValue = EzSqlite::CallbackErrors::kContinue;

    

    return retValue;
}

int main(void)
{
    EzSqlite::Errors sqliteErrors;
    EzSqlite::SqliteManager sqliteManager;
    std::wstring databasePath = L"e.db";

    uint32_t preparedStmt1Index = 0;
    ULONGLONG bindParam1 = 131890523976951191;
    ULONGLONG bindParam2 = 131890523976986106;

    EzSqlite::StmtBindParameterInfo stmtBindParameterInfo;
    std::vector<EzSqlite::StmtBindParameterInfo> stmtBindParameterInfoList;

    //EzSqlite::StepCallbackFunc stepCallback = StepCallback;
    EzSqlite::StepCallbackFunc stepLambdaCallback = [](const EzSqlite::StmtInfo& stmtInfo)->EzSqlite::CallbackErrors
    {
        EzSqlite::CallbackErrors retValue = EzSqlite::CallbackErrors::kContinue;

        return retValue;
    };

    EzSqlite::StepCallbackFunc stepCallback = StepCallback;

    sqliteErrors = sqliteManager.CreateDatabase(
        databasePath,
        EzSqlite::DesiredAccess::kReadWrite, 
        EzSqlite::CreationDisposition::kOpenExisting,
        kCheckTableStmtStringList
    );

    stmtBindParameterInfo.data = &bindParam1;
    stmtBindParameterInfo.dataType = EzSqlite::StmtDataType::kInteger;
    stmtBindParameterInfo.dataByteSize = sizeof(ULONGLONG);
    stmtBindParameterInfoList.push_back(stmtBindParameterInfo);

    stmtBindParameterInfo.data = &bindParam2;
    stmtBindParameterInfo.dataType = EzSqlite::StmtDataType::kInteger;
    stmtBindParameterInfo.dataByteSize = sizeof(ULONGLONG);
    stmtBindParameterInfoList.push_back(stmtBindParameterInfo);

    sqliteManager.PrepareStmt("SELECT * FROM PROCESSEVENT_TB WHERE C_TimeStamp > ? AND C_TimeStamp < ?;", &preparedStmt1Index);
    sqliteManager.ExecStmt(preparedStmt1Index, &stmtBindParameterInfoList, &stepCallback);
    //sqliteManager.ExecStmt("SELECT * FROM PROCESSEVENT_TB WHERE C_TimeStamp > ? AND C_TimeStamp < ?;", &stmtBindParameterInfoList, &stepCallback);
    //sqliteManager.ExecStmt(preparedStmt1Index, &stmtBindParameterInfoList, &stepLambdaCallback);

    sqliteManager.CloseDatabase(false, true);

    return 0;
}