// Ротманов Борис 91 группа, лабораторная 19
module;
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <exception>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

export module text_analysis;

namespace
{
    constexpr unsigned int MAX_TEXT = 1024 * 1024;
    constexpr int LINE_SIZE = 4096;
    constexpr int SYMBOLS = 0x110000;

    struct Statistics
    {
        unsigned int* count = new unsigned int[SYMBOLS]{};
        Statistics() = default;
        ~Statistics() { delete[] count; }
        Statistics(const Statistics&) = delete;
        Statistics& operator=(const Statistics& other)
        {
            if (this != &other)
            {
                std::memcpy(count, other.count, sizeof(unsigned int) * SYMBOLS);
                total = other.total;
                bytes = other.bytes;
                ignored = other.ignored;
            }
            return *this;
        }
        unsigned int ignored = 0;
        unsigned int total = 0; // Число учитываемых символов, без пробелов.
        unsigned int bytes = 0;
    };

    enum class LineStatus { ok, tooLong, invalid, end };

    LineStatus readLine(char line[], int capacity)
    {
        int length = 0;
        bool overflow = false;
        bool invalid = false;
        char ch = 0;
        while (std::cin.get(ch))
        {
            if (ch == '\n')
            {
                line[length] = '\0';
                if (overflow) return LineStatus::tooLong;
                if (invalid) return LineStatus::invalid;
                return LineStatus::ok;
            }
            if (ch == '\r') continue;
            if (ch == '\0') invalid = true;
            if (length < capacity - 1) line[length++] = ch;
            else overflow = true;
        }
        line[0] = '\0';
        return LineStatus::end; 
    }

    #include "unicode_categories.h"

    int unicodeCategory(unsigned int cp)
    {
        int left = 0;
        int right = static_cast<int>(sizeof(UNICODE_RANGES) / sizeof(UNICODE_RANGES[0])) - 1;
        while (left <= right)
        {
            int middle = left + (right - left) / 2;
            if (cp < UNICODE_RANGES[middle].first) right = middle - 1;
            else if (cp > UNICODE_RANGES[middle].last) left = middle + 1;
            else return UNICODE_RANGES[middle].kind;
        }
        return 0;
    }

    struct Utf8State
    {
        unsigned int value = 0;
        unsigned int minimum = 0;
        int remaining = 0;
        bool first = true;
    };

    bool addByte(Statistics& data, Utf8State& state, unsigned char byte)
    {
        if (data.bytes >= MAX_TEXT) return false;
        ++data.bytes;
        if (state.remaining == 0)
        {
            if (byte <= 0x7F) state.value = byte;
            else if (byte >= 0xC2 && byte <= 0xDF)
            { state.value = byte & 0x1F; state.minimum = 0x80; state.remaining = 1; return true; }
            else if (byte >= 0xE0 && byte <= 0xEF)
            { state.value = byte & 0x0F; state.minimum = 0x800; state.remaining = 2; return true; }
            else if (byte >= 0xF0 && byte <= 0xF4)
            { state.value = byte & 0x07; state.minimum = 0x10000; state.remaining = 3; return true; }
            else return false;
        }
        else
        {
            if ((byte & 0xC0) != 0x80) return false;
            state.value = (state.value << 6) | (byte & 0x3F);
            --state.remaining;
            if (state.remaining != 0) return true;
            if (state.value < state.minimum || state.value > 0x10FFFF
                || (state.value >= 0xD800 && state.value <= 0xDFFF)) return false;
        }
        if (state.first && state.value == 0xFEFF)
        {
            state.first = false;
            data.bytes -= 3;
            return true;
        }
        state.first = false;
        if (unicodeCategory(state.value) != 0)
        {
            ++data.count[state.value];
            ++data.total;
        }
        else ++data.ignored; // Пробелы, emoji, служебные и прочие символы.
        return true;
    }

    void printInputError()
    {
        std::cout << "Rejected: invalid/incomplete UTF-8 or text exceeds 1 MiB.\n"
                     "Previous result is unchanged.\n";
    }

    bool readKeyboard(Statistics& result)
    {
        Statistics temporary;
        Utf8State decoder;
        char line[LINE_SIZE];
        std::cout << "Enter English text (several lines allowed).\n"
                     "Finish: /end on a separate line. Cancel: /cancel.\n"
                     "Max 4095 UTF-8 bytes per line; max 1 MiB in total.\n";
        while (true)
        {
            LineStatus status = readLine(line, LINE_SIZE);
            if (status == LineStatus::end) return false;
            if (status != LineStatus::ok)
            {
                std::cout << "Invalid or too long line. Entire operation cancelled.\n";
                return false;
            }
            if (std::strcmp(line, "/cancel") == 0) return false;
            if (std::strcmp(line, "/end") == 0)
            {
                if (decoder.remaining != 0) { printInputError(); return false; }
                result = temporary;
                return true;
            }
            for (int i = 0; line[i] != '\0'; ++i)
            {
                if (!addByte(temporary, decoder, static_cast<unsigned char>(line[i])))
                {
                    printInputError();
                    return false;
                }
            }
            if (!addByte(temporary, decoder, '\n'))
            {
                printInputError();
                return false;
            }
        }
    }


    struct ConsoleInputEncoding
    {
        bool ready = true;
#ifdef _WIN32
        UINT previous = 0;
        UINT previousOutput = 0;
        ConsoleInputEncoding()
        {
            DWORD mode = 0;
            if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode))
            {
                previous = GetConsoleCP();
                ready = SetConsoleCP(CP_UTF8) != 0;
            }
            if (GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode))
            {
                previousOutput = GetConsoleOutputCP();
                ready = (SetConsoleOutputCP(CP_UTF8) != 0) && ready;
            }
        }
        ~ConsoleInputEncoding()
        {
            if (previous != 0) SetConsoleCP(previous);
            if (previousOutput != 0) SetConsoleOutputCP(previousOutput);
        }
#endif
    };

    bool askPath(std::filesystem::path& filePath)
    {
        char path[LINE_SIZE];
        std::cout << "TXT path (Russian names and outer quotes allowed; empty cancels): ";
        LineStatus status = readLine(path, LINE_SIZE);
        if (status != LineStatus::ok)
        {
            if (status != LineStatus::end)
                std::cout << "Invalid or too long path (max 4095 UTF-8 bytes).\n";
            return false;
        }
        int length = static_cast<int>(std::strlen(path));
        if (length == 0) return false;
        // «Копировать как путь» добавляет внешние кавычки — удаляем пару.
        if (length >= 2 && path[0] == '"' && path[length - 1] == '"')
        {
            std::memmove(path, path + 1, static_cast<std::size_t>(length - 2));
            length -= 2;
            path[length] = '\0';
        }
        for (int i = 0; i < length; ++i)
        {
            unsigned char ch = static_cast<unsigned char>(path[i]);
            if (ch < 32 || ch == 127 || ch == '"')
            {
                std::cout << "Invalid path: control characters or unmatched quotes.\n";
                return false;
            }
        }
        if (length < 5 || path[length - 4] != '.'
            || (path[length - 3] != 't' && path[length - 3] != 'T')
            || (path[length - 2] != 'x' && path[length - 2] != 'X')
            || (path[length - 1] != 't' && path[length - 1] != 'T'))
        {
            std::cout << "The filename must end in .txt.\n";
            return false;
        }
#ifdef _WIN32
        wchar_t widePath[LINE_SIZE];
        int converted = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            path, -1, widePath, LINE_SIZE);
        if (converted == 0)
        {
            std::cout << "Invalid UTF-8 path or path too long.\n";
            return false;
        }
        filePath = std::filesystem::path(widePath);
#else
        filePath = std::filesystem::path(path);
#endif
        return true;
    }

    bool readTextFile(Statistics& result)
    {
        std::filesystem::path path;
        if (!askPath(path)) return false;
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error) || error)
        {
            std::cout << "Not an accessible regular file.\n";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            std::cout << "Cannot open input file.\n";
            return false;
        }
        Statistics temporary;
        Utf8State decoder;
        char ch = 0;
        while (input.get(ch))
        {
            if (!addByte(temporary, decoder, static_cast<unsigned char>(ch)))
            {
                printInputError();
                return false;
            }
        }
        if (input.bad() || !input.eof())
        {
            std::cout << "File read error. Previous result is unchanged.\n";
            return false;
        }
        if (decoder.remaining != 0) { printInputError(); return false; }
        result = temporary; // Меняем результат только после успешного чтения.
        return true;
    }

    const char* category(unsigned int cp)
    {
        int kind = unicodeCategory(cp);
        if (kind == 1) return "letter";
        if (kind == 2) return "digit";
        return "punctuation";
    }

    void writeUtf8(std::ostream& output, unsigned int cp)
    {
        if (cp <= 0x7F) output.put(static_cast<char>(cp));
        else if (cp <= 0x7FF)
        {
            output.put(static_cast<char>(0xC0 | (cp >> 6)));
            output.put(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            output.put(static_cast<char>(0xE0 | (cp >> 12)));
            output.put(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            output.put(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            output.put(static_cast<char>(0xF0 | (cp >> 18)));
            output.put(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            output.put(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            output.put(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    void writeReport(std::ostream& output, const Statistics& data)
    {
        output << "LAB 19 - Unicode character frequencies (UTF-8)\n"
                  "Case-sensitive. Count: letters, decimal digits, punctuation.\n"
                  "Other characters are accepted but excluded from percentages.\n"
                  "Percent = count / total counted characters * 100.\n"
               << "Total counted: " << data.total << '\n'
               << "Excluded characters: " << data.ignored << '\n';
        if (data.total == 0)
        {
            output << "No letters, digits or punctuation found.\n";
            return;
        }
        output << "Code point | Symbol | Type | Count | Percent\n";
        output << std::fixed << std::setprecision(2);
        for (unsigned int cp = 0; cp < SYMBOLS; ++cp)
        {
            if (data.count[cp] == 0) continue;
            output << "U+" << std::hex << std::uppercase << std::setw(4)
                   << std::setfill('0') << cp << std::dec << std::setfill(' ') << " | ";
            writeUtf8(output, cp);
            output << " | " << category(cp) << " | " << data.count[cp]
                   << " | " << 100.0 * data.count[cp] / data.total << "%\n";
        }
    }

    void saveReport(const Statistics& data)
    {
        std::filesystem::path path;
        if (!askPath(path)) return;
        std::error_code error;
        bool exists = std::filesystem::exists(path, error);
        if (error)
        {
            std::cout << "Cannot access output path.\n";
            return;
        }
        if (exists)
        {
            if (!std::filesystem::is_regular_file(path, error) || error)
            {
                std::cout << "Output path is not a regular file.\n";
                return;
            }
            char answer[16];
            std::cout << "File exists. Overwrite? Enter YES to confirm: ";
            if (readLine(answer, 16) != LineStatus::ok
                || std::strcmp(answer, "YES") != 0)
            {
                std::cout << "Save cancelled.\n";
                return;
            }
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            std::cout << "Cannot open output file.\n";
            return;
        }
        writeReport(output, data);
        output.close(); 
        if (!output) std::cout << "Write failed; output file may be incomplete.\n";
        else std::cout << "Report saved.\n";
    }
}

export int runApplication() try
{
    ConsoleInputEncoding consoleEncoding;
    if (!consoleEncoding.ready)
    {
        std::cout << "Cannot configure console input for UTF-8.\n";
        return 1;
    }
    Statistics data;
    bool hasData = false;
    std::cout << "Lab 19 (Unicode text and paths). Author: Boris (fill in surname and group).\n";
    while (true)
    {
        try
        {
            char choice[32];
            std::cout << "\n1 - Enter text\n2 - Read TXT file\n"
                         "3 - Show result\n4 - Save result to TXT\n0 - Exit\nChoice: ";
            LineStatus status = readLine(choice, 32);
            if (status == LineStatus::end)
            {
                std::cout << "\nInput closed. Exiting safely.\n";
                return 0;
            }
            if (status != LineStatus::ok || std::strlen(choice) != 1)
            {
                std::cout << "Invalid choice. Enter one digit from 0 to 4.\n";
                continue;
            }
            switch (choice[0])
            {
            case '0': return 0;
            case '1':
                if (readKeyboard(data))
                {
                    hasData = true;
                    writeReport(std::cout, data);
                }
                break;
            case '2':
                if (readTextFile(data))
                {
                    hasData = true;
                    writeReport(std::cout, data);
                }
                break;
            case '3':
                if (hasData) writeReport(std::cout, data);
                else std::cout << "Enter or load text first.\n";
                break;
            case '4':
                if (hasData) saveReport(data);
                else std::cout << "Enter or load text first.\n";
                break;
            default: std::cout << "Invalid choice. Enter one digit from 0 to 4.\n";
            }
            if (!std::cin)
            {
                std::cout << "Input closed. Exiting safely.\n";
                return 0;
            }
        }
        catch (const std::exception&)
        {
            std::cout << "Operation failed due to a system/resource error.\n";
            if (!std::cin || !std::cout) return 1;
        }
    }
}

catch (const std::exception&)
{
    std::cerr << "Cannot initialize application: system/resource error.\n";
    return 1;
}
