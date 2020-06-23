#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "pm_ehash.h"
#include "time.h"

using std::string;
using std::vector;
using std::cout;
using std::endl;
using std::fstream;

int main()
{
    string loadPath[7];
    loadPath[0] = "../../workloads/1w-rw-50-50-load.txt";
    loadPath[1] = "../../workloads/10w-rw-0-100-load.txt";
    loadPath[2] = "../../workloads/10w-rw-25-75-load.txt";
    loadPath[3] = "../../workloads/10w-rw-50-50-load.txt";
    loadPath[4] = "../../workloads/10w-rw-75-25-load.txt";
    loadPath[5] = "../../workloads/10w-rw-100-0-load.txt";
    loadPath[6] = "../../workloads/220w-rw-50-50-load.txt";
    string runPath[7];
    runPath[0] = "../../workloads/1w-rw-50-50-run.txt";
    runPath[1] = "../../workloads/10w-rw-0-100-run.txt";
    runPath[2] = "../../workloads/10w-rw-25-75-run.txt";
    runPath[3] = "../../workloads/10w-rw-50-50-run.txt";
    runPath[4] = "../../workloads/10w-rw-75-25-run.txt";
    runPath[5] = "../../workloads/10w-rw-100-0-run.txt";
    runPath[6] = "../../workloads/220w-rw-50-50-run.txt";

    vector<string> loadOperation;
    vector<string> runOperation;

    string line;
    for (int file_index = 0; file_index < 7; ++file_index) {
        loadOperation.clear();
        runOperation.clear();
        fstream load(loadPath[file_index]);
        while (getline(load, line)) {
            loadOperation.push_back(line);
        }
        load.close();
        fstream run(runPath[file_index]);
        while (getline(run, line)) {
            runOperation.push_back(line);
        }
        run.close();
        clock_t loadStartTime, loadEndTime;

        loadStartTime = clock();
        PmEHash* ehash = new PmEHash;
        for (int i = 0; i < loadOperation.size(); i++) {
            string order = loadOperation[i];
            string operation = "";
            uint64_t key = 0;
            uint64_t value = 0;
            for (int j = 0; j < order.length();) {
                while (order[j] >= 'A' && order[j] <= 'Z') {
                    operation += order[j];
                    j++;
                }
                j++;
                for (int k = 0; k < 8; k++) {
                    key = key * 10 + order[j] - '0';
                    j++;
                }
                for (int k = 0; k < 8; k++) {
                    value = value * 10 + order[j] - '0';
                    j++;
                }
                j = order.length();
            }
            if (operation == "INSERT") {
                kv temp;
                temp.key = key;
                temp.value = value;
                ehash->insert(temp);
            }
        }

        loadEndTime = clock();
        cout << "********************************" << loadPath[file_index] << "*******************************\n";
        cout << "load total time : " << (double)(loadEndTime - loadStartTime) / CLOCKS_PER_SEC << "s" << endl;
        cout << "load total operations : " << loadOperation.size() << endl;
        cout << "load operations per second : " << loadOperation.size() * CLOCKS_PER_SEC / (double)(loadEndTime - loadStartTime) << endl;
        
        clock_t runStartTime, runEndTime;
        int INSERT = 0;
        int UPDATE = 0;
        int READ = 0;
        int DELETE = 0;
        int INSERT_SUCCESS = 0;
        int UPDATE_SUCCESS = 0;
        int READ_SUCCESS = 0;
        int DELETE_SUCCESS = 0;
        runStartTime = clock();
        for (int i = 0; i < runOperation.size(); i++) {
            string order = runOperation[i];
            string operation = "";
            uint64_t key = 0;
            uint64_t value = 0;
            for (int j = 0; j < order.length();) {
                while (order[j] >= 'A' && order[j] <= 'Z') {
                    operation += order[j];
                    j++;
                }
                j++;
                for (int k = 0; k < 8; k++) {
                    key = key * 10 + order[j] - '0';
                    j++;
                }

                for (int k = 0; k < 8; k++) {
                    value = value * 10 + order[j] - '0';
                    j++;
                }
                j = order.length();
            }

            // PmEHash* ehash = new PmEHash;
            kv temp;
            temp.key = key;
            temp.value = value;

            if (operation == "INSERT") {
                if (ehash->insert(temp) == 0) {
                    INSERT_SUCCESS++;
                }
                INSERT++;
            } else if (operation == "UPDATE") {
                if (ehash->update(temp) == 0) {
                    UPDATE_SUCCESS++;
                }
                UPDATE++;
            } else if (operation == "READ") {
                if (ehash->search(key, value) == 0) {
                    READ_SUCCESS++;
                }
                READ++;
            } else if (operation == "DELETE") {
                if (ehash->remove(key) == 0) {
                    DELETE_SUCCESS++;
                }
                DELETE++;
            }
        }

        runEndTime = clock();
        
        cout << "run total Time : " << (double)(runEndTime - runStartTime) / CLOCKS_PER_SEC << "s" << endl;
        cout << "run total operations : " << runOperation.size() << endl;
        cout << "run operations per second : " << runOperation.size() * CLOCKS_PER_SEC / (double)(runEndTime - runStartTime) << endl;
        cout << "INSERT : " << INSERT << " INSERT_SUCCESS : " << INSERT_SUCCESS << endl;
        cout << "UPDATE : " << UPDATE << " UPDATE_SUCCESS : " << UPDATE_SUCCESS << endl;
        cout << "READ : " << READ << " READ_SUCCESS : " << READ_SUCCESS << endl;
        cout << "DELETE : " << DELETE << " DELETE_SUCCESS  : " << DELETE_SUCCESS << endl;
        cout << "********************************" << runPath[file_index] << "*******************************\n";
        ehash->selfDestory();
    }
}