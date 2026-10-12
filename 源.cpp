
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <cstdlib>

using namespace std;

// ==================== 数据结构与全局变量 ====================

struct Record
{
    string time;       // 运行时间
    string equipment;  // 设备名称
    string status;     // NORMAL 或 ERROR
};

using SeqList = vector<Record>;

SeqList L1, L2, ErrorL;

// 记录 L1、L2 各自加载的 CSV 路径，便于插入/更新后写回原文件。
string L1FilePath, L2FilePath;

HWND hOutput = NULL;
HWND hEquipment = NULL;
HWND hTime = NULL;
HWND hWhich = NULL;
HWND hStatus = NULL;

HFONT hFont = NULL;

// 前置声明：更新功能在 listText、setOutput 的定义之前调用它们。
string listText(const SeqList& L, const wstring& title);
void setOutput(const string& text);
bool saveListToLoadedFile(int which);

enum
{
    ID_SHOW = 1001,
    ID_QUERY,
    ID_INSERT,
    ID_UPDATE,
    ID_FIND_ERROR,
    ID_EXTRACT,
    ID_COUNT,
    ID_MERGE,
    ID_EXPORT,
    ID_CLEAR,
    ID_LOAD,
    ID_SAVE_LIST
};

// ==================== 时间处理 ====================

// 检查时间格式是否正确，并验证小时和分钟范围。
bool isValidTime(const string& t)
{
    size_t p = t.find(':');

    if (p == string::npos || p == 0 || p > 2)
        return false;

    if (t.find(':', p + 1) != string::npos)
        return false;

    string hs = t.substr(0, p);
    string ms = t.substr(p + 1);

    if (ms.size() != 2)
        return false;

    for (char c : hs)
        if (c < '0' || c > '9')
            return false;

    for (char c : ms)
        if (c < '0' || c > '9')
            return false;

    int hour = stoi(hs);
    int minute = stoi(ms);

    return hour >= 0 && hour <= 23 &&
        minute >= 0 && minute <= 59;
}

// 将时间转换为从当天 0 点开始计算的分钟数，便于比较。
int timeToMinutes(const string& t)
{
    if (!isValidTime(t))
        return 2147483647;

    size_t p = t.find(':');

    int hour = stoi(t.substr(0, p));
    int minute = stoi(t.substr(p + 1));

    return hour * 60 + minute;
}

// 将时间统一格式化为 HH:MM，例如把 9:00 转成 09:00。
string normalizeTime(const string& t)
{
    size_t p = t.find(':');

    int hour = stoi(t.substr(0, p));
    int minute = stoi(t.substr(p + 1));

    ostringstream oss;
    oss << setw(2) << setfill('0') << hour
        << ":"
        << setw(2) << setfill('0') << minute;

    return oss.str();
}

// ==================== 线性表操作 ====================

// 按时间顺序插入记录，保持线性表有序。
void insertByTime(SeqList& L, const Record& r)
{
    int newTime = timeToMinutes(r.time);
    auto it = L.begin();

    while (it != L.end() &&
        timeToMinutes(it->time) <= newTime)
    {
        ++it;
    }

    L.insert(it, r);
}

// 按时间对整个线性表排序；时间相同时保持原有相对顺序。
void sortByTime(SeqList& L)
{
    stable_sort(L.begin(), L.end(),
        [](const Record& a, const Record& b)
        {
            return timeToMinutes(a.time) <
                timeToMinutes(b.time);
        });
}

// 删除线性表中完全相同的记录（时间、设备名称、状态均相同），并保持时间有序。
void removeDuplicateRecords(SeqList& L)
{
    SeqList uniqueRecords;
    for (const auto& r : L)
    {
        auto it = find_if(uniqueRecords.begin(), uniqueRecords.end(),
            [&](const Record& item)
            {
                return item.time == r.time &&
                    item.equipment == r.equipment &&
                    item.status == r.status;
            });
        if (it == uniqueRecords.end())
            uniqueRecords.push_back(r);
    }
    L.swap(uniqueRecords);
    sortByTime(L);
}

// 根据主界面中的设备名称和时间定位 L1/L2 记录，并将其状态更新为下拉框选择的状态。
// 如果更新后出现完全相同的记录，会自动去重。
void updateRecord(HWND hwnd)
{
    char equipment[256] = {};
    char time[64] = {};
    GetWindowTextA(hEquipment, equipment, sizeof(equipment));
    GetWindowTextA(hTime, time, sizeof(time));

    string equipmentName = equipment;
    string inputTime = time;
    int which = (int)SendMessageW(hWhich, CB_GETCURSEL, 0, 0);
    int statusIndex = (int)SendMessageW(hStatus, CB_GETCURSEL, 0, 0);

    if (equipmentName.empty())
    {
        MessageBoxW(hwnd, L"请输入要更新记录的设备名称。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    if (!isValidTime(inputTime))
    {
        MessageBoxW(hwnd, L"时间格式不正确，请输入 H:MM 或 HH:MM。", L"输入错误", MB_OK | MB_ICONWARNING);
        return;
    }
    if (which != 0 && which != 1)
    {
        MessageBoxW(hwnd, L"请选择目标线性表 L1 或 L2。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }
    if (statusIndex != 0 && statusIndex != 1)
    {
        MessageBoxW(hwnd, L"请选择要更新为的运行状态。", L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    SeqList& target = (which == 1) ? L2 : L1;
    string normalized = normalizeTime(inputTime);
    auto it = find_if(target.begin(), target.end(), [&](const Record& r)
        {
            return r.equipment == equipmentName && r.time == normalized;
        });

    if (it == target.end())
    {
        MessageBoxW(hwnd, L"没有找到设备名称和时间都匹配的记录。请填写原记录的设备名称和原时间，再选择新的状态。", L"未找到记录", MB_OK | MB_ICONWARNING);
        return;
    }

    string newStatus = (statusIndex == 1) ? "ERROR" : "NORMAL";
    int updatedCount = 0;
    for (auto& r : target)
    {
        if (r.equipment == equipmentName && r.time == normalized)
        {
            r.status = newStatus;
            ++updatedCount;
        }
    }

    size_t before = target.size();
    removeDuplicateRecords(target);
    size_t removed = before - target.size();

    bool saved = saveListToLoadedFile(which);

    ostringstream oss;
    oss << "记录更新完成。\r\n目标表：" << (which == 1 ? "L2" : "L1")
        << "\r\n匹配并更新的记录数：" << updatedCount
        << "\r\n去除的完全重复记录数：" << removed;
    if (which == 0 || which == 1)
        oss << "\r\n" << (saved ? "已写回原 CSV 文件：" : "尚未写入文件（该表可能未加载过 CSV）：")
        << ((which == 1) ? L2FilePath : L1FilePath);
    oss << "\r\n\r\n";
    oss << listText(target, (which == 1) ? L"更新后的线性表 L2" : L"更新后的线性表 L1");
    setOutput(oss.str());
}

// 将一条记录整理成适合显示的文本。
string recordToText(const Record& r)
{
    ostringstream oss;

    oss << "时间：" << r.time
        << "    设备：" << r.equipment
        << "    状态：" << r.status << "\r\n";

    return oss.str();
}

// 将线性表标题、记录数量和所有记录组合成显示文本。
string listText(const SeqList& L, const wstring& title)
{
    ostringstream oss;

    int size = WideCharToMultiByte(
        CP_ACP, 0, title.c_str(), -1,
        NULL, 0, NULL, NULL);

    string titleText;

    if (size > 1)
    {
        vector<char> buffer(size);

        WideCharToMultiByte(
            CP_ACP, 0, title.c_str(), -1,
            buffer.data(), size, NULL, NULL);

        titleText = buffer.data();
    }

    oss << "========== " << titleText << " ==========\r\n";
    oss << "记录数量：" << L.size() << "\r\n\r\n";

    for (const auto& r : L)
        oss << recordToText(r);

    oss << "\r\n";

    return oss.str();
}

// 用指定文本替换界面输出框中的内容。
void setOutput(const string& text)
{
    if (hOutput)
        SetWindowTextA(hOutput, text.c_str());
}

// 将文本追加到输出框末尾。
void appendOutput(const string& text)
{
    if (!hOutput)
        return;

    int len = GetWindowTextLengthA(hOutput);

    SendMessageA(hOutput, EM_SETSEL, len, len);
    SendMessageA(hOutput, EM_REPLACESEL, FALSE,
        (LPARAM)text.c_str());
}

// ==================== CSV 解析 ====================

// 解析一行 CSV，处理逗号分隔和双引号包裹的字段。
vector<string> parseCSVLine(const string& line)
{
    vector<string> fields;
    string field;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];

        if (c == '"')
        {
            if (inQuotes && i + 1 < line.size() &&
                line[i + 1] == '"')
            {
                field += '"';
                ++i;
            }
            else
            {
                inQuotes = !inQuotes;
            }
        }
        else if (c == ',' && !inQuotes)
        {
            fields.push_back(field);
            field.clear();
        }
        else
        {
            field += c;
        }
    }

    fields.push_back(field);
    return fields;
}

// 导出 CSV 时给字段加引号，并转义字段内部的双引号。
string csvEscape(const string& value)
{
    string result = "\"";

    for (char c : value)
    {
        if (c == '"')
            result += "\"\"";
        else
            result += c;
    }

    result += "\"";
    return result;
}

// ==================== 加载 CSV ====================
// CSV 字段顺序：时间,设备名称,运行状态

// 从 CSV 文件读取记录，过滤无效行并按时间排序。
bool loadCSV(const string& filename, SeqList& L)
{
    ifstream file(filename);

    if (!file.is_open())
        return false;

    string line;
    SeqList temp;

    while (getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        // 跳过 UTF-8 BOM
        if (temp.empty() && line.size() >= 3 &&
            (unsigned char)line[0] == 0xEF &&
            (unsigned char)line[1] == 0xBB &&
            (unsigned char)line[2] == 0xBF)
        {
            line.erase(0, 3);
        }

        if (line.empty())
            continue;

        vector<string> fields = parseCSVLine(line);

        if (fields.size() < 3)
            continue;

        // 跳过表头
        if (fields[0] == "时间" ||
            fields[0] == "time" ||
            fields[0] == "Time")
        {
            continue;
        }

        if (!isValidTime(fields[0]))
            continue;

        Record r;
        r.time = normalizeTime(fields[0]);
        r.equipment = fields[1];
        r.status = fields[2];

        temp.push_back(r);
    }

    file.close();

    sortByTime(temp);

    // 只有读取完成后才更新目标表
    L = temp;

    return true;
}

// ==================== 导出 CSV ====================

// 普通线性表导出功能保留，供需要导出 L1 或 L2 时使用。
bool exportCSV(const string& filename, const SeqList& L)
{
    ofstream file(filename);

    if (!file.is_open())
        return false;

    file << "时间,设备名称,运行状态\r\n";

    for (const auto& r : L)
    {
        file << csvEscape(r.time) << ","
            << csvEscape(r.equipment) << ","
            << csvEscape(r.status) << "\r\n";
    }

    file.close();
    return true;
}


// 将当前 L1 或 L2 写回它加载时使用的 CSV 文件。
bool saveListToLoadedFile(int which)
{
    if (which == 0)
        return !L1FilePath.empty() && exportCSV(L1FilePath, L1);
    if (which == 1)
        return !L2FilePath.empty() && exportCSV(L2FilePath, L2);
    return false;
}

// 如果该表尚未加载过文件，则弹出“另存为”对话框；否则写回原 CSV。
bool saveSelectedList(HWND hwnd, int which)
{
    if (which != 0 && which != 1)
        return false;

    string& path = (which == 1) ? L2FilePath : L1FilePath;
    const SeqList& data = (which == 1) ? L2 : L1;

    if (path.empty())
    {
        char filename[MAX_PATH] = "";
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = hwnd;
        ofn.lpstrFilter = "CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = "csv";
        ofn.lpstrTitle = (which == 1) ? "保存 L2 到 CSV 文件" : "保存 L1 到 CSV 文件";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (!GetSaveFileNameA(&ofn))
            return false;
        path = filename;
    }

    if (!exportCSV(path, data))
    {
        MessageBoxW(hwnd, L"保存失败！请检查文件路径或写入权限。", L"错误", MB_OK | MB_ICONERROR);
        return false;
    }
    return true;
}

// 将处理完成后的 ErrorL 按 no、Time、Equipment、Status 写入 device_error.csv。
bool exportErrorCSV(const string& filename, const SeqList& L)
{
    ofstream file(filename, ios::out | ios::trunc);
    if (!file.is_open())
        return false;

    file << "no,Time,Equipment,Status\r\n";
    for (size_t i = 0; i < L.size(); ++i)
    {
        file << i + 1 << ","
            << csvEscape(L[i].time) << ","
            << csvEscape(L[i].equipment) << ","
            << csvEscape(L[i].status) << "\r\n";
    }
    return file.good();
}

// 在界面中按要求显示 ErrorL 的序号、时间、设备名称和设备状态。
string errorListText(const SeqList& L, const wstring& title)
{
    ostringstream oss;
    int size = WideCharToMultiByte(CP_ACP, 0, title.c_str(), -1,
        NULL, 0, NULL, NULL);
    string titleText;
    if (size > 1)
    {
        vector<char> buffer(size);
        WideCharToMultiByte(CP_ACP, 0, title.c_str(), -1,
            buffer.data(), size, NULL, NULL);
        titleText = buffer.data();
    }

    oss << "========== " << titleText << " ==========" << "\r\n";
    oss << "no\tTime\tEquipment\tStatus\r\n";
    oss << "----------------------------------------------\r\n";
    for (size_t i = 0; i < L.size(); ++i)
    {
        oss << i + 1 << "\t"
            << L[i].time << "\t"
            << L[i].equipment << "\t"
            << L[i].status << "\r\n";
    }
    oss << "故障记录数量：" << L.size() << "\r\n";
    return oss.str();
}

// ==================== 加载 CSV（只保留这一个函数） ====================

// 打开文件选择框，并将 CSV 加载到用户选择的 L1 或 L2。
void chooseAndLoadCSV(HWND hwnd)
{
    if (hWhich == NULL)
    {
        MessageBoxW(hwnd, L"目标表下拉框不存在！",
            L"错误", MB_OK | MB_ICONERROR);
        return;
    }

    // 读取目标表：0 = L1，1 = L2
    int which = (int)SendMessageW(
        hWhich, CB_GETCURSEL, 0, 0);

    if (which != 0 && which != 1)
    {
        MessageBoxW(hwnd, L"请先选择 L1 或 L2！",
            L"提示", MB_OK | MB_ICONWARNING);
        return;
    }

    char filename[MAX_PATH] = "";

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter =
        "CSV 文件 (*.csv)\0*.csv\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = "csv";
    ofn.lpstrTitle = "选择要加载的 CSV 文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameA(&ofn))
        return;

    SeqList temp;

    if (!loadCSV(filename, temp))
    {
        MessageBoxW(hwnd, L"无法打开 CSV 文件！",
            L"错误", MB_OK | MB_ICONERROR);
        return;
    }

    // 关键修复：根据下拉框选择写入对应的表
    if (which == 1)
    {
        L2 = temp;
        L2FilePath = filename;

        ostringstream oss;
        oss << "CSV 加载成功！\r\n"
            << "目标表：L2\r\n"
            << "文件：" << filename << "\r\n"
            << "已加载记录数：" << L2.size() << "\r\n\r\n";

        oss << listText(L2, L"L2 加载结果");
        setOutput(oss.str());
    }
    else
    {
        L1 = temp;
        L1FilePath = filename;

        ostringstream oss;
        oss << "CSV 加载成功！\r\n"
            << "目标表：L1\r\n"
            << "文件：" << filename << "\r\n"
            << "已加载记录数：" << L1.size() << "\r\n\r\n";

        oss << listText(L1, L"L1 加载结果");
        setOutput(oss.str());
    }
}

// ==================== 设备查询 ====================

// 在 L1 和 L2 中查找指定设备的所有记录。
void queryEquipment(const string& equipment)
{
    ostringstream oss;
    int count = 0;

    oss << "========== 设备查询 ==========\r\n";
    oss << "查询设备：" << equipment << "\r\n\r\n";

    auto searchList = [&](const SeqList& L, const string& name)
        {
            for (const auto& r : L)
            {
                if (r.equipment == equipment)
                {
                    oss << "[" << name << "] ";
                    oss << recordToText(r);
                    ++count;
                }
            }
        };

    searchList(L1, "L1");
    searchList(L2, "L2");

    oss << "\r\n查询到 " << count << " 条记录。\r\n";
    setOutput(oss.str());
}

// ==================== 查找故障记录 ====================

// 查找并显示 L1、L2 中状态为 ERROR 的记录。
void findErrorRecords()
{
    ostringstream oss;
    int count = 0;

    oss << "========== 故障记录查询 ==========\r\n\r\n";

    auto searchList = [&](const SeqList& L, const string& name)
        {
            for (const auto& r : L)
            {
                if (r.status == "ERROR")
                {
                    oss << "[" << name << "] ";
                    oss << recordToText(r);
                    ++count;
                }
            }
        };

    searchList(L1, "L1");
    searchList(L2, "L2");

    oss << "\r\n故障记录总数：" << count << "\r\n";
    setOutput(oss.str());
}

// ==================== 提取故障记录 ====================

// 将 L1、L2 中所有 ERROR 记录转移到 ErrorL，并从原表删除。
// 遍历时使用迭代器删除，删除后不递增迭代器，避免漏掉连续故障记录。
void extractErrorRecords()
{
    ErrorL.clear();

    auto extractFrom = [](SeqList& source)
        {
            for (auto it = source.begin(); it != source.end(); )
            {
                if (it->status == "ERROR")
                {
                    ErrorL.push_back(*it); // 先复制到 ErrorL
                    it = source.erase(it); // 再从原线性表中删除
                }
                else
                {
                    ++it;
                }
            }
        };

    extractFrom(L1);
    extractFrom(L2);
    sortByTime(ErrorL);

    ostringstream oss;
    oss << "已完成故障记录提取：ERROR 记录已转移至 ErrorL，并从 L1、L2 删除。\r\n\r\n";
    oss << errorListText(ErrorL, L"故障记录表 ErrorL");
    oss << "\r\n" << listText(L1, L"删除故障记录后的线性表 L1");
    oss << listText(L2, L"删除故障记录后的线性表 L2");
    setOutput(oss.str());
}

// ==================== 统计记录 ====================

// 统计三个线性表的记录总数及故障记录数。
void countRecords()
{
    int error1 = 0;
    int error2 = 0;
    int errorAll = 0;

    for (const auto& r : L1)
        if (r.status == "ERROR")
            ++error1;

    for (const auto& r : L2)
        if (r.status == "ERROR")
            ++error2;

    for (const auto& r : ErrorL)
        if (r.status == "ERROR")
            ++errorAll;

    ostringstream oss;

    oss << "========== 记录统计 ==========\r\n\r\n";
    oss << "L1 记录总数：" << L1.size() << "\r\n";
    oss << "L1 故障记录数：" << error1 << "\r\n\r\n";

    oss << "L2 记录总数：" << L2.size() << "\r\n";
    oss << "L2 故障记录数：" << error2 << "\r\n\r\n";

    oss << "ErrorL 记录总数：" << ErrorL.size() << "\r\n";
    oss << "ErrorL 中故障记录数：" << errorAll << "\r\n";

    setOutput(oss.str());
}

// ==================== 合并故障记录 ====================

// 将同一设备的多条 ERROR 记录合并为一条，并把所有故障时间按先后顺序拼接。
// 例如：MRI Scanner 16:00、MRI Scanner 10:00 -> MRI Scanner 10:00，16:00。
void mergeErrorRecords()
{
    // 若 ErrorL 为空，则先从 L1、L2 收集故障记录；若已提取过，则直接合并 ErrorL。
    if (ErrorL.empty())
    {
        for (const auto& r : L1)
            if (r.status == "ERROR") ErrorL.push_back(r);
        for (const auto& r : L2)
            if (r.status == "ERROR") ErrorL.push_back(r);
    }

    // 先移除完全相同的原始故障记录，再按设备和时间排序合并。
    removeDuplicateRecords(ErrorL);

    // 先按设备和时间排序，保证同一设备的时间按先后排列。
    stable_sort(ErrorL.begin(), ErrorL.end(),
        [](const Record& a, const Record& b)
        {
            if (a.equipment != b.equipment)
                return a.equipment < b.equipment;
            return timeToMinutes(a.time) < timeToMinutes(b.time);
        });

    SeqList merged;
    for (const auto& r : ErrorL)
    {
        if (r.status != "ERROR")
        {
            // ErrorL 按定义应只保存故障记录；非 ERROR 记录不纳入故障合并。
            continue;
        }

        auto it = find_if(merged.begin(), merged.end(),
            [&](const Record& item)
            {
                return item.equipment == r.equipment && item.status == r.status;
            });

        if (it == merged.end())
        {
            merged.push_back(r);
        }
        else
        {
            // 避免相同故障时间重复拼接。
            vector<string> times;
            string current = it->time;
            size_t start = 0;
            while (start <= current.size())
            {
                size_t pos = current.find("，", start);
                if (pos == string::npos)
                {
                    times.push_back(current.substr(start));
                    break;
                }
                times.push_back(current.substr(start, pos - start));
                start = pos + string("，").size();
            }

            if (find(times.begin(), times.end(), r.time) == times.end())
                times.push_back(r.time);

            stable_sort(times.begin(), times.end(),
                [](const string& a, const string& b)
                {
                    return timeToMinutes(a) < timeToMinutes(b);
                });

            it->time.clear();
            for (size_t i = 0; i < times.size(); ++i)
            {
                if (i != 0) it->time += "，";
                it->time += times[i];
            }
        }
    }

    // 合并结果按设备名称排列，便于查看；同一设备只保留一条记录。
    ErrorL = merged;
    setOutput(errorListText(ErrorL, L"合并后的故障记录表 ErrorL"));
}

// ==================== 创建界面控件 ====================

// 创建一个窗口控件，并设置其字体。
HWND createControl(
    HWND parent,
    const wchar_t* className,
    const wchar_t* text,
    DWORD style,
    int x, int y, int width, int height,
    int id)
{
    HWND hwnd = CreateWindowW(
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        x, y, width, height,
        parent,
        (HMENU)(INT_PTR)id,
        GetModuleHandleW(NULL),
        NULL);

    if (hFont && hwnd)
        SendMessageW(hwnd, WM_SETFONT, (WPARAM)hFont, TRUE);

    return hwnd;
}

// ==================== 消息处理函数 ====================

// 窗口消息处理函数，负责创建界面并响应按钮、尺寸和关闭事件。
LRESULT CALLBACK WndProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        hFont = CreateFontW(
            18, 0, 0, 0, FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Microsoft YaHei");

        createControl(hwnd, L"STATIC", L"设备名称：",
            0, 20, 20, 80, 28, 0);

        hEquipment = createControl(
            hwnd, L"EDIT", L"CT Scanner",
            WS_BORDER | ES_AUTOHSCROLL,
            100, 18, 170, 30, 0);

        createControl(hwnd, L"STATIC", L"运行时间：",
            0, 285, 20, 80, 28, 0);

        hTime = createControl(
            hwnd, L"EDIT", L"09:00",
            WS_BORDER | ES_AUTOHSCROLL,
            365, 18, 90, 30, 0);

        createControl(hwnd, L"STATIC", L"目标表：",
            0, 470, 20, 65, 28, 0);

        hWhich = createControl(
            hwnd, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL,
            535, 18, 80, 150, 0);

        SendMessageW(hWhich, CB_ADDSTRING, 0, (LPARAM)L"L1");
        SendMessageW(hWhich, CB_ADDSTRING, 0, (LPARAM)L"L2");
        SendMessageW(hWhich, CB_SETCURSEL, 0, 0);

        createControl(hwnd, L"STATIC", L"运行状态：",
            0, 630, 20, 80, 28, 0);

        hStatus = createControl(
            hwnd, L"COMBOBOX", L"",
            CBS_DROPDOWNLIST | WS_VSCROLL,
            710, 18, 100, 150, 0);

        SendMessageW(hStatus, CB_ADDSTRING, 0, (LPARAM)L"NORMAL");
        SendMessageW(hStatus, CB_ADDSTRING, 0, (LPARAM)L"ERROR");
        SendMessageW(hStatus, CB_SETCURSEL, 0, 0);

        // 第二行按钮
        createControl(hwnd, L"BUTTON", L"加载 CSV",
            BS_PUSHBUTTON, 20, 65, 100, 32, ID_LOAD);

        createControl(hwnd, L"BUTTON", L"显示两个表",
            BS_PUSHBUTTON, 130, 65, 110, 32, ID_SHOW);

        createControl(hwnd, L"BUTTON", L"查询设备",
            BS_PUSHBUTTON, 250, 65, 100, 32, ID_QUERY);

        createControl(hwnd, L"BUTTON", L"按时间插入",
            BS_PUSHBUTTON, 360, 65, 110, 32, ID_INSERT);

        createControl(hwnd, L"BUTTON", L"查找故障",
            BS_PUSHBUTTON, 480, 65, 100, 32, ID_FIND_ERROR);

        createControl(hwnd, L"BUTTON", L"提取故障",
            BS_PUSHBUTTON, 590, 65, 100, 32, ID_EXTRACT);

        createControl(hwnd, L"BUTTON", L"统计次数",
            BS_PUSHBUTTON, 700, 65, 100, 32, ID_COUNT);

        // 第三行按钮
        createControl(hwnd, L"BUTTON", L"合并故障时间",
            BS_PUSHBUTTON, 20, 105, 125, 32, ID_MERGE);

        createControl(hwnd, L"BUTTON", L"导出 CSV",
            BS_PUSHBUTTON, 155, 105, 100, 32, ID_EXPORT);

        createControl(hwnd, L"BUTTON", L"清空显示",
            BS_PUSHBUTTON, 265, 105, 100, 32, ID_CLEAR);

        createControl(hwnd, L"BUTTON", L"更新记录/去重",
            BS_PUSHBUTTON, 375, 105, 130, 32, ID_UPDATE);

        createControl(hwnd, L"BUTTON", L"保存当前表到文件",
            BS_PUSHBUTTON, 515, 105, 150, 32, ID_SAVE_LIST);

        hOutput = createControl(
            hwnd, L"EDIT",
            L"欢迎使用医疗设备运行记录管理系统。\r\n"
            L"请先加载 CSV，或填写设备名称、时间和状态后插入记录。\r\n",
            WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL |
            ES_READONLY | WS_VSCROLL,
            20, 150, 850, 510, 0);

        SendMessageW(hOutput, EM_SETLIMITTEXT, 0, 0);

        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case ID_LOAD:
            // 只调用带 HWND 参数的加载函数
            chooseAndLoadCSV(hwnd);
            break;

        case ID_SHOW:
        {
            ostringstream oss;

            oss << listText(L1, L"线性表 L1");
            oss << listText(L2, L"线性表 L2");
            oss << listText(ErrorL, L"故障记录表 ErrorL");

            setOutput(oss.str());
            break;
        }

        case ID_QUERY:
        {
            char equipment[256] = {};
            GetWindowTextA(hEquipment, equipment, sizeof(equipment));

            string name = equipment;

            if (name.empty())
            {
                MessageBoxW(hwnd, L"请先输入设备名称。",
                    L"提示", MB_OK | MB_ICONWARNING);
                break;
            }

            queryEquipment(name);
            break;
        }

        case ID_INSERT:
        {
            char equipment[256] = {};
            char time[64] = {};

            GetWindowTextA(hEquipment, equipment, sizeof(equipment));
            GetWindowTextA(hTime, time, sizeof(time));

            string equipmentName = equipment;
            string inputTime = time;

            int which = (int)SendMessageW(
                hWhich, CB_GETCURSEL, 0, 0);

            int statusIndex = (int)SendMessageW(
                hStatus, CB_GETCURSEL, 0, 0);

            if (equipmentName.empty())
            {
                MessageBoxW(hwnd, L"设备名称不能为空。",
                    L"输入错误", MB_OK | MB_ICONWARNING);
                break;
            }

            if (!isValidTime(inputTime))
            {
                MessageBoxW(hwnd,
                    L"时间格式不正确！请使用 H:MM 或 HH:MM，"
                    L"例如 9:00、14:30。\r\n"
                    L"小时范围为 0—23，分钟范围为 0—59。",
                    L"输入错误", MB_OK | MB_ICONWARNING);
                break;
            }

            if (which != 0 && which != 1)
            {
                MessageBoxW(hwnd, L"请选择目标线性表 L1 或 L2。",
                    L"输入错误", MB_OK | MB_ICONWARNING);
                break;
            }

            if (statusIndex != 0 && statusIndex != 1)
            {
                MessageBoxW(hwnd, L"请选择运行状态。",
                    L"输入错误", MB_OK | MB_ICONWARNING);
                break;
            }

            Record r;
            r.time = normalizeTime(inputTime);
            r.equipment = equipmentName;
            r.status = (statusIndex == 1) ? "ERROR" : "NORMAL";

            SeqList& target = (which == 1) ? L2 : L1;
            insertByTime(target, r);
            bool saved = saveListToLoadedFile(which);

            if (!((which == 1) ? L2FilePath : L1FilePath).empty() && !saved)
                MessageBoxW(hwnd, L"记录已插入内存中的线性表，但写回 CSV 文件失败。", L"保存失败", MB_OK | MB_ICONWARNING);

            setOutput(listText(
                target,
                (which == 1) ? L"插入后的线性表 L2"
                : L"插入后的线性表 L1"));
            break;
        }

        case ID_UPDATE:
            updateRecord(hwnd);
            break;

        case ID_FIND_ERROR:
            findErrorRecords();
            break;

        case ID_EXTRACT:
            extractErrorRecords();
            break;

        case ID_COUNT:
            countRecords();
            break;

        case ID_MERGE:
            mergeErrorRecords();
            break;

        case ID_EXPORT:
        {
            // 按实验要求固定导出 ErrorL 到程序工作目录下的 device_error.csv。
            if (ErrorL.empty())
            {
                MessageBoxW(hwnd, L"ErrorL 当前为空，请先点击‘提取故障’或‘合并故障时间’。",
                    L"提示", MB_OK | MB_ICONWARNING);
                break;
            }

            if (exportErrorCSV("device_error.csv", ErrorL))
            {
                setOutput(errorListText(ErrorL, L"已导出的故障记录表 ErrorL"));
                MessageBoxW(hwnd, L"ErrorL 已按 no、Time、Equipment、Status 输出到 device_error.csv。\r\n文件位于程序工作目录。",
                    L"导出成功", MB_OK | MB_ICONINFORMATION);
            }
            else
            {
                MessageBoxW(hwnd, L"导出失败！请检查程序工作目录是否有写入权限。",
                    L"错误", MB_OK | MB_ICONERROR);
            }
            break;
        }

        case ID_SAVE_LIST:
        {
            int which = (int)SendMessageW(hWhich, CB_GETCURSEL, 0, 0);
            if (which != 0 && which != 1)
            {
                MessageBoxW(hwnd, L"请先选择 L1 或 L2。", L"提示", MB_OK | MB_ICONWARNING);
                break;
            }
            if (saveSelectedList(hwnd, which))
            {
                const string& path = (which == 1) ? L2FilePath : L1FilePath;
                ostringstream oss;
                oss << "保存成功！\r\n目标表：" << (which == 1 ? "L2" : "L1")
                    << "\r\n文件：" << path << "\r\n\r\n";
                oss << listText((which == 1) ? L2 : L1,
                    (which == 1) ? L"已保存的线性表 L2" : L"已保存的线性表 L1");
                setOutput(oss.str());
                MessageBoxW(hwnd, L"当前线性表已保存到 CSV 文件。", L"保存成功", MB_OK | MB_ICONINFORMATION);
            }
            break;
        }

        case ID_CLEAR:
            SetWindowTextW(hOutput, L"");
            break;
        }

        return 0;
    }

    case WM_SIZE:
    {
        if (hOutput)
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);

            int outputWidth = (width > 40) ? width - 40 : 100;
            int outputHeight = (height > 170) ? height - 170 : 100;

            MoveWindow(hOutput, 20, 150,
                outputWidth, outputHeight, TRUE);
        }

        return 0;
    }

    case WM_DESTROY:
        if (hFont)
        {
            DeleteObject(hFont);
            hFont = NULL;
        }

        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ==================== 程序入口 ====================

// 注册窗口类、创建主窗口，并进入 Windows 消息循环。
int main()
{
    HINSTANCE hInstance = GetModuleHandleW(NULL);

    const wchar_t CLASS_NAME[] = L"MedicalRecordWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);

    if (!RegisterClassW(&wc))
    {
        MessageBoxW(NULL, L"窗口类注册失败！",
            L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"医疗设备运行记录管理系统",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        920, 740,
        NULL, NULL, hInstance, NULL);

    if (!hwnd)
    {
        MessageBoxW(NULL, L"窗口创建失败！",
            L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg = {};

    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
