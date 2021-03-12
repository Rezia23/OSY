#ifndef __PROGTEST__

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <climits>
#include <cfloat>
#include <cassert>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <array>
#include <iterator>
#include <set>
#include <list>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
#include <deque>
#include <memory>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <pthread.h>
#include <semaphore.h>
#include "progtest_solver.h"
#include "sample_tester.h"

using namespace std;
#endif /* __PROGTEST__ */

class CCargoPlanner {
public:
    vector<ACustomer> m_Customers;
    vector<AShip> m_Ships;
    vector<thread> m_Sales;
    vector<thread> m_Workers;
    vector<pair<AShip, vector<CCargo>>> m_PossibleCargoShip;

    sem_t semShipSales;
    sem_t semSalesWorkers;

    mutex mutShip;
    mutex mutCargo;
    bool stopSignal = false;

    CCargoPlanner(){
        sem_init(&semShipSales,0, 0);
        sem_init(&semSalesWorkers,0, 0);
    }

    static int SeqSolver(const vector<CCargo> &cargo,
                         int maxWeight,
                         int maxVolume,
                         vector<CCargo> &load);

    void Start(int sales,
               int workers);

    void Stop(void);

    void Customer(ACustomer customer);

    void Ship(AShip ship);
    void SalesFunc();
    void WorkersFunc();
};
void CCargoPlanner::Stop() {
    stopSignal = true;
    for(size_t i = 0; i<m_Sales.size();i++){
        sem_post(&semShipSales);
    }
    for(size_t i = 0; i<m_Sales.size();i++){
        m_Sales[i].join();
    }
    for(size_t i = 0; i<m_Workers.size();i++){
        sem_post(&semSalesWorkers);
    }
    for(size_t i = 0; i<m_Workers.size();i++){
        m_Workers[i].join();
    }
}

void CCargoPlanner::SalesFunc(){
    //printf("sales starts\n");
    AShip myShip;
    while(1){
    //todo -- maybe done
        sem_wait(&semShipSales);
        mutShip.lock();
        if(m_Ships.empty()){
            mutShip.unlock();
            if(stopSignal){
                //printf("Stoppin  now\n");
                break;
            }else{
                continue;
            }
        }else{
            myShip = m_Ships.back();
            m_Ships.pop_back();
            mutShip.unlock();

            vector<CCargo> tmpCargo;
            vector<CCargo> cargo;


            for(size_t i = 0; i<m_Customers.size();i++){
                m_Customers[i]->Quote(myShip->Destination(),tmpCargo);
                cargo.insert(cargo.end(), tmpCargo.begin(), tmpCargo.end());
            }
            //printf("puttin stuff to possibleCargoShip %s\n", myShip->Destination().c_str());
            mutCargo.lock();
            m_PossibleCargoShip.emplace_back(myShip, cargo);
            mutCargo.unlock();
            sem_post(&semSalesWorkers);
            //printf("have put stuff to possibleCargoShip %s\n", myShip->Destination().c_str());
        }
    }
}
void CCargoPlanner::WorkersFunc() {
    //todo -- maybe done
    //printf("starting worker\n");
    pair<AShip, vector <CCargo>> shipAndCargo;
    while(1){
        sem_wait(&semSalesWorkers);
        mutCargo.lock();
        if(m_PossibleCargoShip.empty()){
            mutCargo.unlock();
            break;
        } else{
            vector<CCargo> load;
            shipAndCargo = m_PossibleCargoShip.back();
            m_PossibleCargoShip.pop_back();
            mutCargo.unlock();
            SeqSolver(shipAndCargo.second, shipAndCargo.first->MaxWeight(), shipAndCargo.first->MaxVolume(), load );
            shipAndCargo.first->Load(load);
            //printf("loaded stuff");
        }
    }
}


void CCargoPlanner::Start(int sales, int workers) {
    for(int i = 0; i<sales; i++){
        m_Sales.emplace_back(&CCargoPlanner::SalesFunc, this);
    }
    for(int i = 0; i<workers;i++){
        m_Workers.emplace_back(thread(&CCargoPlanner::WorkersFunc, this));
    }
}

int CCargoPlanner::SeqSolver(const vector<CCargo> &cargo, int maxWeight, int maxVolume, vector<CCargo> &load) {
    return ProgtestSolver(cargo, maxWeight, maxVolume, load);
}

void CCargoPlanner::Customer(ACustomer customer) {
    m_Customers.push_back(customer);
}

void CCargoPlanner::Ship(AShip ship) {
    //printf("adding ship\n");
    mutShip.lock();
    m_Ships.push_back(ship);
    mutShip.unlock();
    //printf("added ship\n");
    sem_post(&semShipSales);
}



// TODO: CCargoPlanner implementation goes here
//-------------------------------------------------------------------------------------------------
#ifndef __PROGTEST__

int main(void) {
    CCargoPlanner test;
    vector<AShipTest> ships;
    vector<ACustomerTest> customers{make_shared<CCustomerTest>(), make_shared<CCustomerTest>()};

    ships.push_back(g_TestExtra[0].PrepareTest("New York", customers));
    ships.push_back(g_TestExtra[1].PrepareTest("Barcelona", customers));
    ships.push_back(g_TestExtra[2].PrepareTest("Kobe", customers));
    ships.push_back(g_TestExtra[8].PrepareTest("Perth", customers));
    // add more ships here

    for (auto x : customers)
        test.Customer(x);

    test.Start(3, 2);

    for (auto x : ships)
        test.Ship(x);

    test.Stop();

    for (auto x : ships)
        cout << x->Destination() << ": " << (x->Validate() ? "ok" : "fail") << endl;
    return 0;
}

#endif /* __PROGTEST__ */
