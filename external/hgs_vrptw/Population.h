/*MIT License

Original HGS-CVRP code: Copyright(c) 2020 Thibaut Vidal
Additional contributions: Copyright(c) 2022 ORTEC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/

#ifndef POPULATION_H
#define POPULATION_H

#include <iosfwd>
#include <list>
#include <string>
#include <time.h>
#include <vector>

#include "Individual.h"
#include "LocalSearch.h"
#include "Params.h"
#include "Split.h"

// Create the alias SubPopulation for an object of type std::vector<Individual*>
typedef std::vector<Individual *> SubPopulation;

// Class representing the population of a Genetic Algorithm with functionality to write information to files, do binary
// tournaments, update fitness values etc.
class Population {
private:
    Params         *params;      // Problem parameters
    Split          *split;       // Split algorithm
    HGSLocalSearch *localSearch; // Local search structure
    SubPopulation
        feasibleSubpopulation; // Feasible subpopulation (called a 'pool'), kept ordered by increasing penalized cost
    SubPopulation infeasibleSubpopulation;   // Infeasible subpopulation (called a 'pool'), kept ordered by increasing
                                             // penalized cost
    std::list<bool> listFeasibilityLoad;     // Load feasibility of the last 100 individuals generated by LS
    std::list<bool> listFeasibilityTimeWarp; // Time warp feasibility of the last 100 individuals generated by LS
    std::vector<std::pair<clock_t, double>>
               searchProgress;      // Keeps tracks of the time stamps of successive best solutions
    Individual bestSolutionRestart; // Best solution found during the current restart of the algorthm
    Individual bestSolutionOverall; // Best solution found during the complete execution of the algorithm

    // Evaluates the biased fitness of all individuals in the population
    void updateBiasedFitnesses(SubPopulation &pop);

    // Removes the worst individual in terms of biased fitness
    void removeWorstBiasedFitness(SubPopulation &subpop);

    // Performs local search and adds the individual. If the individual is infeasible, with some
    // probability we try to repair it and add it if this succeeds.
    void doHGSLocalSearchAndAddIndividual(Individual *indiv);

public:
    // Generates the population. Part of the population is generated randomly and the other part using
    // several construction heuristics. There is variety in the individuals that are constructed using
    // the construction heuristics through the parameters used.
    void generatePopulation();

    // Add an individual in the population (survivor selection is automatically triggered whenever the population
    // reaches its maximum size) Returns TRUE if a new best solution of the run has been found
    bool addIndividual(const Individual *indiv, bool updateFeasible);

    // Cleans all solutions and generates a new initial population (only used when running HGS until a time limit, in
    // which case the algorithm restarts until the time limit is reached)
    void restart();

    // Adaptation of the penalty parameters (this also updates the evaluations)
    void managePenalties();

    // Selects an individal by binary tournament
    Individual *getBinaryTournament();

    // Selects two non-identical parents by binary tournament and return as a pair
    std::pair<Individual *, Individual *> getNonIdenticalParentsBinaryTournament();

    // Accesses the best feasible individual If not possible, return nullptr
    Individual *getBestFeasible();

    // Accesses the best infeasible individual If not possible, return nullptr
    Individual *getBestInfeasible();

    // Accesses the best found solution at all time. If not possible, return nullptr
    Individual *getBestFound();

    // Prints population state
    void printState(int nbIter, int nbIterNoImprovement);

    // Returns the average diversity value among the minimumPopulationSize best individuals in the subpopulation
    // Returns -1.0 if the size of pop <= 0
    double getDiversity(const SubPopulation &pop);

    // Returns the average solution value among the minimumPopulationSize best individuals in the subpopulation
    // Returns -1.0 if the size of pop <= 0
    double getAverageCost(const SubPopulation &pop);

    // Overwrites a solution written in a file if the current solution is better
    void exportBKS(std::string fileName);

    // Exports the history of solution improvements to a file
    void exportSearchProgress(std::string fileName, std::string instanceName, int seedRNG);

    // Exports the population to a file
    void exportPopulation(int nbIter, std::string fileName);

    // Logs costs and list of client vists of one solution/individual to a file
    void logSolution(int nbIter, std::ofstream &myfile, Individual *indiv);

    // Constructor
    Population(Params *params, Split *split, HGSLocalSearch *localSearch);

    std::vector<std::vector<int>> extractFeasibleRoutes() {
        std::vector<std::vector<int>> feasibleRoutes; // Stores all feasible routes with nodes

        // auto indiv = getBestFound();
        //  Iterate over all feasible individuals
        for (Individual *indiv : feasibleSubpopulation) {
            // Loop over all routes/vehicles of the solution
            for (int k = 0; k < params->nbVehicles; k++) {
                // If the route is not empty, store it
                if (!indiv->chromR[k].empty()) {
                    std::vector<int> route;
                    // Add depot at the beginning (assuming 0 is the depot)
                    route.push_back(0);

                    // Add all customer nodes in the route
                    for (int i : indiv->chromR[k]) { route.push_back(i); }

                    // Add depot at the end
                    route.push_back(0);

                    // Add this route to the collection of feasible routes
                    feasibleRoutes.push_back(route);
                }
            }
        }
        //}

        return feasibleRoutes;
    }

    // Destructor
    ~Population();
};

#endif
