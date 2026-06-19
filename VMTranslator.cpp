// VMTranslator.cpp
// Project 8 VM translator (multi-file, branching, functions)
// Compile: g++ -std=c++17 VMTranslator.cpp -O2 -o VMTranslator

#include <bits/stdc++.h>
#include <filesystem>
using namespace std;
namespace fs = std::filesystem;

enum class CommandType {
    C_ARITHMETIC,
    C_PUSH,
    C_POP,
    C_LABEL,
    C_GOTO,
    C_IF,
    C_FUNCTION,
    C_CALL,
    C_RETURN,
    C_UNKNOWN
};

struct Command {
    CommandType type = CommandType::C_UNKNOWN;
    string arg1; // for arithmetic: the command; for push/pop/others: segment/label/function name
    int arg2 = 0; // for push/pop/function/call: index/num
    bool valid = false;
};

// Utility: trim and remove comments
static string strip(const string &line) {
    string s = line;
    auto pos = s.find("//");
    if (pos != string::npos) s = s.substr(0, pos);
    size_t start = 0;
    while (start < s.size() && isspace((unsigned char)s[start])) ++start;
    size_t end = s.size();
    while (end > start && isspace((unsigned char)s[end - 1])) --end;
    return s.substr(start, end - start);
}

// Parser: reads VM lines and produces Command
class Parser {
    ifstream in;
public:
    Parser() = default;
    Parser(const string &filename) { in.open(filename); }
    bool open(const string &filename) { in.open(filename); return in.is_open(); }
    bool good() const { return in.is_open(); }

    bool hasMoreCommands() {
        while (in && in.peek() != EOF) {
            streampos pos = in.tellg();
            string line;
            getline(in, line);
            string s = strip(line);
            if (!s.empty()) {
                in.clear();
                in.seekg(pos);
                return true;
            }
        }
        return false;
    }

    // Read next command; returns false if none
    bool advance(Command &cmd) {
        string line;
        while (getline(in, line)) {
            string s = strip(line);
            if (s.empty()) continue;
            stringstream ss(s);
            string tok;
            vector<string> toks;
            while (ss >> tok) toks.push_back(tok);
            if (toks.empty()) continue;

            // classify
            if (toks[0] == "push" && toks.size() == 3) {
                cmd.type = CommandType::C_PUSH;
                cmd.arg1 = toks[1];
                cmd.arg2 = stoi(toks[2]);
                cmd.valid = true;
                return true;
            } else if (toks[0] == "pop" && toks.size() == 3) {
                cmd.type = CommandType::C_POP;
                cmd.arg1 = toks[1];
                cmd.arg2 = stoi(toks[2]);
                cmd.valid = true;
                return true;
            } else if (toks[0] == "label" && toks.size() == 2) {
                cmd.type = CommandType::C_LABEL;
                cmd.arg1 = toks[1];
                cmd.valid = true;
                return true;
            } else if (toks[0] == "goto" && toks.size() == 2) {
                cmd.type = CommandType::C_GOTO;
                cmd.arg1 = toks[1];
                cmd.valid = true;
                return true;
            } else if (toks[0] == "if-goto" && toks.size() == 2) {
                cmd.type = CommandType::C_IF;
                cmd.arg1 = toks[1];
                cmd.valid = true;
                return true;
            } else if (toks[0] == "function" && toks.size() == 3) {
                cmd.type = CommandType::C_FUNCTION;
                cmd.arg1 = toks[1];
                cmd.arg2 = stoi(toks[2]);
                cmd.valid = true;
                return true;
            } else if (toks[0] == "call" && toks.size() == 3) {
                cmd.type = CommandType::C_CALL;
                cmd.arg1 = toks[1];
                cmd.arg2 = stoi(toks[2]);
                cmd.valid = true;
                return true;
            } else if (toks[0] == "return") {
                cmd.type = CommandType::C_RETURN;
                cmd.valid = true;
                return true;
            } else {
                static const unordered_set<string> arith = {
                    "add","sub","neg","eq","gt","lt","and","or","not"
                };
                if (toks.size() == 1 && arith.count(toks[0])) {
                    cmd.type = CommandType::C_ARITHMETIC;
                    cmd.arg1 = toks[0];
                    cmd.valid = true;
                    return true;
                }
            }
            // else unknown - skip
        }
        return false;
    }
};

// CodeWriter: emits Hack assembly for commands (extended for Project 8)
class CodeWriter {
    ofstream out;
    string basename;         // used for static variable naming (file scope)
    string currentFunction;  // used for function-scoped labels
    int labelCounter = 0;

    string makeLabel(const string &prefix) {
        return prefix + to_string(labelCounter++);
    }

    void writeLine(const string &s) { out << s << '\n'; }

    // push D register value onto stack
    void pushDToStack() {
        writeLine("@SP");
        writeLine("A=M");
        writeLine("M=D");
        writeLine("@SP");
        writeLine("M=M+1");
    }

    // pop top of stack to D
    void popStackToD() {
        writeLine("@SP");
        writeLine("AM=M-1");
        writeLine("D=M");
    }

    // helper to build a function-scoped label name
    string makeScopedLabel(const string &label) const {
        if (!currentFunction.empty()) return currentFunction + "$" + label;
        return label;
    }

public:
    CodeWriter() = default;
    CodeWriter(const string &outfilename, const string &inbasename) {
        basename = inbasename;
        out.open(outfilename);
    }

    void setOutput(const string &outfilename) {
        if (out.is_open()) out.close();
        out.open(outfilename);
    }

    void setFileName(const string &inbasename) { basename = inbasename; }
    bool good() const { return out.is_open(); }

    // write bootstrap (SP=256 and call Sys.init)
    void writeInit() {
        // SP = 256
        writeLine("@256");
        writeLine("D=A");
        writeLine("@SP");
        writeLine("M=D");
        // call Sys.init
        writeCall("Sys.init", 0);
    }

    // Write arithmetic command
    void writeArithmetic(const string &cmd) {
        if (cmd == "add" || cmd == "sub" || cmd == "and" || cmd == "or") {
            // binary ops: pop y -> D, x at SP-1 -> M, compute, result in M (x's slot)
            popStackToD();           // D = y
            writeLine("@SP");
            writeLine("A=M-1");     // A points to x
            if (cmd == "add") writeLine("M=M+D");
            if (cmd == "sub") writeLine("M=M-D");
            if (cmd == "and") writeLine("M=M&D");
            if (cmd == "or")  writeLine("M=M|D");
        } else if (cmd == "neg" || cmd == "not") {
            // unary ops on top of stack
            writeLine("@SP");
            writeLine("A=M-1");
            if (cmd == "neg") writeLine("M=-M");
            else writeLine("M=!M");
        } else if (cmd == "eq" || cmd == "gt" || cmd == "lt") {
            // comparison: pop y -> D ; x in M (SP-1)
            popStackToD(); // D = y
            writeLine("@SP");
            writeLine("A=M-1"); // A -> x
            writeLine("D=M-D"); // D = x - y
            string labelTrue = makeLabel("TRUE");
            string labelEnd  = makeLabel("END");
            writeLine("@" + labelTrue);
            if (cmd == "eq") writeLine("D;JEQ");
            if (cmd == "gt") writeLine("D;JGT");
            if (cmd == "lt") writeLine("D;JLT");
            // false:
            writeLine("@SP");
            writeLine("A=M-1");
            writeLine("M=0");
            writeLine("@" + labelEnd);
            writeLine("0;JMP");
            // true:
            writeLine("(" + labelTrue + ")");
            writeLine("@SP");
            writeLine("A=M-1");
            writeLine("M=-1"); // true = -1
            // end:
            writeLine("(" + labelEnd + ")");
        } else {
            // unknown arithmetic (shouldn't happen)
        }
    }

    // Write push command
    void writePush(const string &segment, int index) {
        if (segment == "constant") {
            writeLine("@" + to_string(index));
            writeLine("D=A");
            pushDToStack();
            return;
        }

        if (segment == "local" || segment == "argument" || segment == "this" || segment == "that") {
            string base;
            if (segment == "local") base = "LCL";
            if (segment == "argument") base = "ARG";
            if (segment == "this") base = "THIS";
            if (segment == "that") base = "THAT";
            writeLine("@" + to_string(index));
            writeLine("D=A");
            writeLine("@" + base);
            writeLine("A=M+D");
            writeLine("D=M");
            pushDToStack();
            return;
        }

        if (segment == "temp") {
            // temp base is 5
            writeLine("@" + to_string(5 + index));
            writeLine("D=M");
            pushDToStack();
            return;
        }

        if (segment == "pointer") {
            // pointer 0 -> THIS, pointer 1 -> THAT
            if (index == 0) writeLine("@THIS");
            else writeLine("@THAT");
            writeLine("D=M");
            pushDToStack();
            return;
        }

        if (segment == "static") {
            string name = basename + "." + to_string(index);
            writeLine("@" + name);
            writeLine("D=M");
            pushDToStack();
            return;
        }

        // unknown segment -> ignore (should not happen for valid VM)
    }

    // Write pop command
    void writePop(const string &segment, int index) {
        if (segment == "constant") {
            // pop constant is invalid; ignore
            return;
        }

        if (segment == "local" || segment == "argument" || segment == "this" || segment == "that") {
            string base;
            if (segment == "local") base = "LCL";
            if (segment == "argument") base = "ARG";
            if (segment == "this") base = "THIS";
            if (segment == "that") base = "THAT";
            // compute target = base + index into R13
            writeLine("@" + to_string(index));
            writeLine("D=A");
            writeLine("@" + base);
            writeLine("D=M+D");
            writeLine("@R13");
            writeLine("M=D");
            // pop stack to D
            popStackToD();
            // store D into *R13
            writeLine("@R13");
            writeLine("A=M");
            writeLine("M=D");
            return;
        }

        if (segment == "temp") {
            popStackToD();
            writeLine("@" + to_string(5 + index));
            writeLine("M=D");
            return;
        }

        if (segment == "pointer") {
            popStackToD();
            if (index == 0) writeLine("@THIS");
            else writeLine("@THAT");
            writeLine("M=D");
            return;
        }

        if (segment == "static") {
            popStackToD();
            string name = basename + "." + to_string(index);
            writeLine("@" + name);
            writeLine("M=D");
            return;
        }

        // unknown
    }

    // Branching: label, goto, if-goto
    void writeLabel(const string &label) {
        string lab = makeScopedLabel(label);
        writeLine("(" + lab + ")");
    }

    void writeGoto(const string &label) {
        string lab = makeScopedLabel(label);
        writeLine("@" + lab);
        writeLine("0;JMP");
    }

    void writeIf(const string &label) {
        string lab = makeScopedLabel(label);
        popStackToD();          // D = popped value
        writeLine("@" + lab);
        writeLine("D;JNE");     // jump if D != 0
    }

    // Functions: function, call, return
    void writeFunction(const string &funcName, int nVars) {
        // set current function scope
        currentFunction = funcName;
        // label for function entry
        writeLine("(" + funcName + ")");
        // initialize local variables to 0
        for (int i = 0; i < nVars; ++i) {
            writeLine("@0");
            writeLine("D=A");
            pushDToStack();
        }
    }

    void writeCall(const string &funcName, int nArgs) {
        // create return label
        string retLabel = makeLabel(funcName + "$ret.");
        // push return-address
        writeLine("@" + retLabel);
        writeLine("D=A");
        pushDToStack();
        // push LCL, ARG, THIS, THAT
        writeLine("@LCL"); writeLine("D=M"); pushDToStack();
        writeLine("@ARG"); writeLine("D=M"); pushDToStack();
        writeLine("@THIS"); writeLine("D=M"); pushDToStack();
        writeLine("@THAT"); writeLine("D=M"); pushDToStack();
        // ARG = SP - nArgs - 5
        writeLine("@SP"); writeLine("D=M");
        writeLine("@" + to_string(nArgs + 5)); writeLine("D=D-A");
        writeLine("@ARG"); writeLine("M=D");
        // LCL = SP
        writeLine("@SP"); writeLine("D=M"); writeLine("@LCL"); writeLine("M=D");
        // goto function
        writeLine("@" + funcName); writeLine("0;JMP");
        // (return-address)
        writeLine("(" + retLabel + ")");
    }

    void writeReturn() {
        // FRAME = LCL   (R13 = FRAME)
        writeLine("@LCL"); writeLine("D=M"); writeLine("@R13"); writeLine("M=D");
        // RET = *(FRAME - 5)  (R14 = RET)
        writeLine("@5"); writeLine("A=D-A"); writeLine("D=M"); writeLine("@R14"); writeLine("M=D");
        // *ARG = pop()
        popStackToD();
        writeLine("@ARG"); writeLine("A=M"); writeLine("M=D");
        // SP = ARG + 1
        writeLine("@ARG"); writeLine("D=M+1"); writeLine("@SP"); writeLine("M=D");
        // restore THAT = *(FRAME -1)
        writeLine("@R13"); writeLine("AM=M-1"); writeLine("D=M"); writeLine("@THAT"); writeLine("M=D");
        // restore THIS = *(FRAME -2)
        writeLine("@R13"); writeLine("AM=M-1"); writeLine("D=M"); writeLine("@THIS"); writeLine("M=D");
        // restore ARG = *(FRAME -3)
        writeLine("@R13"); writeLine("AM=M-1"); writeLine("D=M"); writeLine("@ARG"); writeLine("M=D");
        // restore LCL = *(FRAME -4)
        writeLine("@R13"); writeLine("AM=M-1"); writeLine("D=M"); writeLine("@LCL"); writeLine("M=D");
        // goto RET
        writeLine("@R14"); writeLine("A=M"); writeLine("0;JMP");
    }

};

static string getBaseName(const string &path) {
    string s = path;
    size_t pos = s.find_last_of("/\\");
    if (pos != string::npos) s = s.substr(pos + 1);
    if (s.size() >= 3 && s.substr(s.size() - 3) == ".vm") s = s.substr(0, s.size() - 3);
    return s;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        cerr << "Usage: VMTranslator InputFile.vm | InputFolder\n";
        return 1;
    }

    string input = argv[1];
    vector<string> vmFiles;
    string outFile;

    try {
        fs::path p(input);
        if (fs::is_directory(p)) {
            // collect .vm files
            for (auto &entry : fs::directory_iterator(p)) {
                if (entry.path().extension() == ".vm") {
                    vmFiles.push_back(entry.path().string());
                }
            }
            if (vmFiles.empty()) {
                cerr << "No .vm files found in directory: " << input << "\n";
                return 1;
            }
            // output file: folderName/folderName.asm
            string folderName = p.filename().string();
            outFile = (p / (folderName + ".asm")).string();
        } else {
            // single file
            if (p.extension() != ".vm") {
                cerr << "Input must be a .vm file or folder containing .vm files\n";
                return 1;
            }
            vmFiles.push_back(p.string());
            outFile = p.parent_path().empty() ? (p.stem().string() + ".asm")
                                             : (p.parent_path() / (p.stem().string() + ".asm")).string();
        }
    } catch (const fs::filesystem_error &e) {
        cerr << "Filesystem error: " << e.what() << "\n";
        return 1;
    }

    CodeWriter writer;
    writer.setOutput(outFile);
    if (!writer.good()) {
        cerr << "Failed to open output file: " << outFile << "\n";
        return 1;
    }

    bool multiFile = (vmFiles.size() > 1);
    if (multiFile) {
        // Write bootstrap code when translating a folder (multi-file program)
        writer.writeInit();
    }

    // Process files in sorted order for determinism
    sort(vmFiles.begin(), vmFiles.end());

    for (const string &file : vmFiles) {
        string base = getBaseName(file);
        writer.setFileName(base);

        Parser parser(file);
        if (!parser.good()) {
            cerr << "Failed to open input file: " << file << "\n";
            return 1;
        }

        Command cmd;
        while (parser.advance(cmd)) {
            if (!cmd.valid) continue;
            switch (cmd.type) {
                case CommandType::C_ARITHMETIC:
                    writer.writeArithmetic(cmd.arg1);
                    break;
                case CommandType::C_PUSH:
                    writer.writePush(cmd.arg1, cmd.arg2);
                    break;
                case CommandType::C_POP:
                    writer.writePop(cmd.arg1, cmd.arg2);
                    break;
                case CommandType::C_LABEL:
                    writer.writeLabel(cmd.arg1);
                    break;
                case CommandType::C_GOTO:
                    writer.writeGoto(cmd.arg1);
                    break;
                case CommandType::C_IF:
                    writer.writeIf(cmd.arg1);
                    break;
                case CommandType::C_FUNCTION:
                    writer.writeFunction(cmd.arg1, cmd.arg2);
                    break;
                case CommandType::C_CALL:
                    writer.writeCall(cmd.arg1, cmd.arg2);
                    break;
                case CommandType::C_RETURN:
                    writer.writeReturn();
                    break;
                default:
                    break;
            }
            cmd = Command(); // reset
        }
    }

    cout << "Wrote " << outFile << "\n";
    return 0;
}

