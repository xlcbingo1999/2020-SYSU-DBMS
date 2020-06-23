#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include "data_page.h"
#include "time.h"

using namespace std;

#define CLOCKS_PER_SEC ((clock_t)1000)

int main() 
{
    string loadPath = "../workloads/1w-rw-50-50-load.txt";
    string runPath = "../workloads/1w-rw-50-50-run.txt";
    
    vector<string> loadOperation;
    vector<string> runOperation;
    
    string line;
    
    fstream load(loadPath);
    int onhun = 100;
    while (getline(load, line) && onhun > 0) {
        loadOperation.push_back(line);  
        --onhun;
    }
    onhun = 100;
    fstream run(runPath);
    while (getline(load, line) && onhun > 0) {
        loadOperation.push_back(line);  
		--onhun;
    }
    
    clock_t loadStartTime, loadEndTime;
    
    loadStartTime = clock();
	PmEHash* ehash = new PmEHash;
    for (int i = 0; i < loadOperation.size(); i++) {
    	string order = loadOperation[i];
    	string operation = "";
    	uint64_t key = 0;
    	uint64_t value = 0;
    	for (int j = 0; j < order.length();) {
    	    while (order[j] != ' ') {
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
    	}
    	if (operation == "INSERT") {
			kv temp;
			temp.key = key;
			temp.value = value;
			ehash->insert(temp);
    	}
    }
    loadEndTime = clock();
    cout << "load total time : " << (double)(loadEndTime - loadStartTime) / CLOCKS_PER_SEC << "s" << endl;
    cout << "load total operations : " << loadOperation.size() << endl;
    cout << "load operations per second : " << loadOperation.size() * CLOCKS_PER_SEC / (double)(loadEndTime - loadStartTime) << endl;

    clock_t runStartTime, runEndTime;
    int INSERT = 0;
    int UPDATE = 0;
	int READ = 0;
	int DELETE = 0;
    runStartTime = clock();
    for (int i = 0; i < runOperation.size(); i++) {
    	string order = runOperation[i];
    	string operation = "";
    	uint64_t key = 0;
    	uint64_t value = 0;
    	for (int j = 0; j < order.length();) {
    	    while (order[j] != ' ') {
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
    	}
    	
    	// PmEHash* ehash = new PmEHash;
        kv temp;
        temp.key = key;
        temp.value = value;
    	
    	if (operation == "INSERT") {
			ehash->insert(temp);
			INSERT++;
    	} else if (operation == "UPDATE") {
			ehash->update(temp);
			UPDATE++;
    	} else if (operation == "READ"){
    	    ehash->search(key, value);
    	    READ++;
    	} else if (operation == "DELETE"){
    	    ehash->remove(key);
    	    DELETE++;
    	}
    }
    runEndTime = clock();  
    cout << "run total Time : " <<(double)(runEndTime - runStartTime) / CLOCKS_PER_SEC << "s" << endl;
    cout << "run total operations : " << runOperation.size() << endl;
    cout << "run operations per second : " << runOperation.size() * CLOCKS_PER_SEC / (double)(runEndTime - runStartTime) << endl;
    cout << "INSERT : " << INSERT << endl;
    cout << "UPDATE : " << UPDATE << endl;
    cout << "READ : " << READ << endl;
    cout << "DELETE : " << DELETE << endl;
}
