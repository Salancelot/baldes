/**
 * @file VRPTW.h
 * @brief Defines the Vehicle Routing Problem with Time Windows (VRPTW) class and its associated methods.
 *
 * This file contains the implementation of the `VRProblem` class, which solves the Vehicle Routing Problem with
 * Time Windows (VRPTW) using column generation and various optimization techniques. The VRPTW class handles:
 * - Column generation and constraint handling using the Gurobi optimizer.
 * - Addition and removal of variables and paths from the model.
 * - Handling of dual values and reduced costs for optimization.
 * - Stabilization mechanisms using Limited Memory Rank 1 Cuts and RCC separation.
 * - Integration with external algorithms such as bucket graphs and RCC managers for advanced routing optimization.
 *
 * The VRPTW class uses Gurobi for solving linear programming relaxations and applies iterative methods to refine
 * the solution with constraints and separation cuts.
 */

#pragma once

#include "Cut.h"
#include "Definitions.h"

#include "bucket/BucketGraph.h"
#include "bucket/BucketSolve.h"
#include "bucket/BucketUtils.h"

#ifdef STAB
#include "extra/Stabilization.h"
#endif

#include "Reader.h"

#ifdef RCC
#include "../third_party/cvrpsep/capsep.h"
#include "../third_party/cvrpsep/cnstrmgr.h"
#include <tbb/concurrent_unordered_map.h>
#endif

#ifdef EXACT_RCC
#include "cuts/RCC.h"
#endif

#include "Hashes.h"

#ifdef IPM
#include "ipm/IPSolver.h"
#endif

class VRProblem {
public:
    InstanceData instance;

    std::vector<VRPJob> jobs;
    std::vector<Path>   allPaths;

    int                           labels_counter = 0;
    std::vector<std::vector<int>> labels;
    int                           numConstrs = 0;

    std::vector<GRBConstr> SRCconstraints;
    ModelData              matrix;

    LimitedMemoryRank1Cuts r1c;
    std::vector<Path>      toMerge;

#ifdef RCC
    CnstrMgrPointer oldCutsCMP = nullptr;

    tbb::concurrent_unordered_map<std::pair<int, int>, std::vector<GRBLinExpr>, pair_hash, pair_equal> arcCache;

#endif
    void addVars(GRBModel *node, auto lb, auto ub, auto obj, auto vtype, auto name, auto col, auto row) {
        node->addVars(lb, ub, obj, vtype, name, col, row);
    }

    int addPath(GRBModel *node, const std::vector<Path> paths, bool enumerate = false) {
        auto &cuts = r1c.cutStorage;

        int                 numConstrsLocal = node->get(GRB_IntAttr_NumConstrs);
        const GRBConstr    *constrs         = node->getConstrs();
        std::vector<double> coluna(numConstrsLocal, 0.0); // Declare outside the loop

        // Collect the bounds, costs, names, and columns
        std::vector<double>      lb, ub, obj;
        std::vector<GRBColumn>   cols;
        std::vector<std::string> names;
        std::vector<char>        vtypes;

        auto counter = 0;
        for (auto &label : paths) {
            counter += 1;
            // if (counter > 10) break;

            labels_counter += 1;
            std::fill(coluna.begin(), coluna.end(), 0.0); // Reset for each iteration

            double      travel_cost = label.cost;
            std::string name        = "x[" + std::to_string(allPaths.size()) + "]";

            GRBColumn col;

            // Fill coluna with coefficients
            // Step 1: Accumulate the coefficients for each job
            for (auto &job : label.route) {
                if (job > 0 && job != N_SIZE - 1) {
                    int constr_index = job - 1;
                    coluna[constr_index] += 1; // Accumulate the coefficient for each job
                }
            }

            // Step 2: Add the non-zero entries to the sparse matrix
            for (int i = 0; i < N_SIZE - 1; ++i) {
                if (coluna[i] > 0) {
                    matrix.A_sparse.elements.push_back({i, matrix.A_sparse.num_cols, static_cast<double>(coluna[i])});
                }
            }

            matrix.lb.push_back(0.0);
            matrix.ub.push_back(1.0);
            matrix.c.push_back(travel_cost);
            // Add terms to GRBColumn
            for (int i = 0; i < N_SIZE - 2; i++) {
                if (coluna[i] == 0.0) continue;
                col.addTerms(&coluna[i], &constrs[i], 1);
            }
            // Add terms for total veicles constraint
            double val = 1;
            col.addTerms(&val, &constrs[N_SIZE - 2], 1);

            // Add terms for the limited memory rank 1 cuts
#if defined(SRC3) || defined(SRC)
            auto vec = cuts.computeLimitedMemoryCoefficients(label.route);
            // print vec size
            if (vec.size() > 0) {
                for (int i = 0; i < vec.size(); i++) {
                    if (vec[i] != 0) {
                        col.addTerms(&vec[i], &SRCconstraints[i], 1);
                        // matrix.A_sparse.row_indices.push_back(N_SIZE - 2 + i);
                        // matrix.A_sparse.col_indices.push_back(matrix.A_sparse.num_cols);
                        // matrix.A_sparse.values.push_back(static_cast<double>(vec[i]));
                        matrix.A_sparse.elements.push_back(
                            {N_SIZE - 2 + i, matrix.A_sparse.num_cols, static_cast<double>(vec[i])});
                    }
                }
            }

#endif

            matrix.A_sparse.num_cols++;

            // Collect bounds, costs, columns, and names
            lb.push_back(0.0);
            ub.push_back(1.0);
            obj.push_back(travel_cost);
            cols.push_back(col);
            names.push_back(name);
            vtypes.push_back(GRB_CONTINUOUS);

            Path path(label.route, label.cost);
            allPaths.emplace_back(path);
        }
        // Add variables with bounds, objectives, and columns
        if (!lb.empty()) {
            addVars(node, lb.data(), ub.data(), obj.data(), vtypes.data(), names.data(), cols.data(), lb.size());
            node->update();
        }

        return counter;
    }

    /*
     * Adds a column to the GRBModel.
     *
     */
    inline int addColumn(GRBModel *node, const auto &columns, bool enumerate = false) {
        auto &cuts = r1c.cutStorage;

        int                 numConstrsLocal = node->get(GRB_IntAttr_NumConstrs);
        const GRBConstr    *constrs         = node->getConstrs();
        std::vector<double> coluna(numConstrsLocal, 0.0); // Declare outside the loop

        // Collect the bounds, costs, names, and columns
        std::vector<double>      lb, ub, obj;
        std::vector<GRBColumn>   cols;
        std::vector<std::string> names;
        std::vector<char>        vtypes;

        auto counter = 0;
        for (auto &label : columns) {
            if (label->jobs_covered.empty()) continue;
            if (!enumerate && label->cost > 0) continue;
            counter += 1;
            if (counter > 10) break;

            labels_counter += 1;
            std::fill(coluna.begin(), coluna.end(), 0.0); // Reset for each iteration

            double      travel_cost = label->real_cost;
            std::string name        = "x[" + std::to_string(allPaths.size()) + "]";

            GRBColumn col;

            // Fill coluna with coefficients
            // Step 1: Accumulate the coefficients for each job
            for (const auto &job : label->jobs_covered) {
                if (likely(job > 0 && job != N_SIZE - 1)) { // Use likely() if this condition is almost always true
                    coluna[job - 1]++;                      // Simplified increment operation
                }
            }

            // Step 2: Add the non-zero entries to the sparse matrix
            for (int i = 0; i < N_SIZE - 1; ++i) {
                if (coluna[i] > 0) {
                    // matrix.A_sparse.row_indices.push_back(i);
                    // matrix.A_sparse.col_indices.push_back(matrix.A_sparse.num_cols);
                    // matrix.A_sparse.values.push_back(static_cast<double>(coluna[i]));
                    matrix.A_sparse.elements.push_back({i, matrix.A_sparse.num_cols, static_cast<double>(coluna[i])});
                }
            }

            matrix.lb.push_back(0.0);
            matrix.ub.push_back(1.0);
            matrix.c.push_back(travel_cost);
            // Add terms to GRBColumn
            for (int i = 0; i < N_SIZE - 2; i++) {
                if (coluna[i] == 0.0) continue;
                col.addTerms(&coluna[i], &constrs[i], 1);
            }
            // Add terms for total veicles constraint
            double val = 1;
            col.addTerms(&val, &constrs[N_SIZE - 2], 1);

            // Add terms for the limited memory rank 1 cuts
#if defined(SRC3) || defined(SRC)
            auto vec = cuts.computeLimitedMemoryCoefficients(label->jobs_covered);
            // print vec size
            if (vec.size() > 0) {
                for (int i = 0; i < vec.size(); i++) {
                    if (vec[i] != 0) {
                        col.addTerms(&vec[i], &SRCconstraints[i], 1);
                        // matrix.A_sparse.row_indices.push_back(N_SIZE - 2 + i);
                        // matrix.A_sparse.col_indices.push_back(matrix.A_sparse.num_cols);
                        // matrix.A_sparse.values.push_back(static_cast<double>(vec[i]));
                        matrix.A_sparse.elements.push_back(
                            {N_SIZE - 2 + i, matrix.A_sparse.num_cols, static_cast<double>(vec[i])});
                    }
                }
            }

#endif

            matrix.A_sparse.num_cols++;

            // Collect bounds, costs, columns, and names
            lb.push_back(0.0);
            ub.push_back(1.0);
            obj.push_back(travel_cost);
            cols.push_back(col);
            names.push_back(name);
            vtypes.push_back(GRB_CONTINUOUS);

            Path path(label->jobs_covered, label->real_cost);
            allPaths.emplace_back(path);
        }
        // Add variables with bounds, objectives, and columns
        if (!lb.empty()) {
            addVars(node, lb.data(), ub.data(), obj.data(), vtypes.data(), names.data(), cols.data(), lb.size());
            node->update();
        }

        return counter;
    }

    /**
     * @brief Changes the coefficients of a constraint in the given GRBModel.
     *
     * This function modifies the coefficients of a constraint in the specified GRBModel
     * by iterating over the variables and updating their coefficients using the provided values.
     *
     */
    void chgCoeff(GRBModel *model, const GRBConstr &constrName, std::vector<double> value) {
        auto varNames = model->getVars();
        int  numVars  = model->get(GRB_IntAttr_NumVars);
        for (int i = 0; i < numVars; i++) { model->chgCoeff(constrName, varNames[i], value[i]); }
    }

    /**
     * Binarizes the variables in the given GRBModel.
     *
     * This function sets the variable type of each variable in the model to binary.
     * It iterates over all variables in the model and sets their variable type to GRB_BINARY.
     * After updating the model, all variables will be binary variables.
     *
     */
    void binarizeNode(GRBModel *model) {
        auto varNumber = model->get(GRB_IntAttr_NumVars);
        for (int i = 0; i < varNumber; i++) {
            GRBVar var = model->getVar(i);
            var.set(GRB_CharAttr_VType, GRB_BINARY);
        }
        model->update();
    }

    /**
     * Removes variables with negative reduced costs, up to a maximum of 30% of the total variables,
     * and also removes corresponding elements from the allPaths vector.
     *
     */
    void removeNegativeReducedCostVarsAndPaths(GRBModel *model) {
        model->optimize();
        int                 varNumber = model->get(GRB_IntAttr_NumVars);
        std::vector<GRBVar> vars(varNumber);

        // Collect all variables
        for (int i = 0; i < varNumber; ++i) { vars[i] = model->getVar(i); }

        // Vector to store reduced costs
        std::vector<double> reducedCosts(varNumber);

        // Retrieve the reduced cost for each variable individually
        for (int i = 0; i < varNumber; ++i) { reducedCosts[i] = vars[i].get(GRB_DoubleAttr_RC); }

        // Vector to store indices of variables with negative reduced costs
        std::vector<int> indicesToRemove;

        // Identify variables with negative reduced costs
        for (int i = 0; i < varNumber; ++i) {
            if (reducedCosts[i] < 0) { indicesToRemove.push_back(i); }
        }

        // Limit the number of variables to remove to 30% of the total
        size_t maxRemoval = static_cast<size_t>(0.3 * varNumber);
        if (indicesToRemove.size() > maxRemoval) {
            indicesToRemove.resize(maxRemoval); // Only keep the first 30%
        }

        // Remove the selected variables from the model and corresponding paths from allPaths
        for (int i = indicesToRemove.size() - 1; i >= 0; --i) {
            int index = indicesToRemove[i];
            model->remove(vars[index]);               // Remove the variable from the model
            allPaths.erase(allPaths.begin() + index); // Remove the corresponding path from allPaths
        }

        model->update(); // Apply changes to the model
    }

    /**
     * Retrieves the dual values of the constraints in the given GRBModel.
     *
     */
    std::vector<double> getDuals(GRBModel *model) {
        // int                    numConstrs = model->get(GRB_IntAttr_NumConstrs);
        std::vector<GRBConstr> constraints;
        constraints.reserve(numConstrs);

        // Collect all constraints
        for (int i = 0; i < numConstrs; ++i) { constraints.push_back(model->getConstr(i)); }

        // Prepare the duals vector
        std::vector<double> duals(numConstrs);

        // Retrieve all dual values in one call
        auto dualArray = model->get(GRB_DoubleAttr_Pi, constraints.data(), constraints.size());

        duals.assign(dualArray, dualArray + numConstrs);
        return duals;
    }

    /**
     * Extracts the solution from a given GRBModel object.
     *
     */
    std::vector<double> extractSolution(GRBModel *model) {
        std::vector<double> sol;
        auto                varNumber = model->get(GRB_IntAttr_NumVars);
        auto                vals      = model->get(GRB_DoubleAttr_X, model->getVars(), varNumber);
        sol.assign(vals, vals + varNumber);
        return sol;
    }

    /**
     * Handles the cuts for the VRPTW problem.
     *
     */
    bool cutHandler(LimitedMemoryRank1Cuts &r1c, GRBModel *node, std::vector<GRBConstr> &constraints) {
        auto &cuts    = r1c.cutStorage;
        bool  changed = false;

        if (!cuts.empty()) {
            for (auto &cut : cuts) {
                if (cut.added && !cut.updated) { continue; }

                changed            = true;
                const int   z      = cut.id;
                const auto &coeffs = cut.coefficients;

                if (cut.added && cut.updated) {
                    cut.updated = false;
                    if (z >= constraints.size()) { continue; }
                    chgCoeff(node, constraints[z], coeffs);
                } else {
                    GRBLinExpr lhs;
                    for (size_t i = 0; i < coeffs.size(); ++i) {
                        if (coeffs[i] == 0) { continue; }
                        lhs += node->getVar(i) * coeffs[i];
                    }

                    std::string constraint_name = "cuts(z" + std::to_string(z) + ")";
                    // constraint_name << "cuts(z" << z << ")";

                    auto ctr = node->addConstr(lhs <= cut.rhs, constraint_name);
                    constraints.emplace_back(ctr);
                }

                cut.added   = true;
                cut.updated = false;
            }
        }

        node->update();
        return changed;
    }

#ifdef EXACT_RCC
    /**
     * @brief Separates Rounded Capacity Cuts (RCC) for the given model and solution.
     *
     * This function identifies and separates RCCs for the provided model and solution.
     * It uses parallel execution to handle multiple tasks concurrently, generating
     * cut expressions and right-hand side values for each identified cut.
     *
     * @param model Pointer to the Gurobi model.
     * @param solution Vector of doubles representing the solution.
     * @param constraints 3D vector of Gurobi constraints.
     * @return True if any RCCs were added, false otherwise.
     */
    bool exactRCCsep(GRBModel *model, const std::vector<double> &solution,
                     std::vector<std::vector<std::vector<GRBConstr>>> &constraints) {

        auto Ss = separate_Rounded_Capacity_cuts(model, instance.q, instance.demand, allPaths, solution);

        if (Ss.size() == 0) return false;

        // Define the tasks that need to be executed in parallel
        std::vector<int> tasks(Ss.size());
        std::iota(tasks.begin(), tasks.end(),
                  0); // Filling tasks with [0, 1, ..., cutsCMP->Size-1]

        std::vector<GRBLinExpr>          cutExpressions(Ss.size());
        std::vector<int>                 rhsValues(Ss.size());
        std::vector<std::vector<RCCarc>> arcGroups(Ss.size());
        std::mutex                       cuts_mutex;
        std::mutex                       arcCache_mutex; // Mutex to protect arcCache

        const int                JOBS = 10;
        exec::static_thread_pool pool(JOBS);
        auto                     sched = pool.get_scheduler();

        //  Create a bulk sender to handle parallel execution of tasks
        auto bulk_sender = stdexec::bulk(
            stdexec::just(), tasks.size(),
            [this, &Ss, &arcCache_mutex, &model, &cuts_mutex, &tasks, &cutExpressions, &rhsValues,
             &arcGroups](std::size_t task_idx) {
                int c = tasks[task_idx];

                auto S = Ss[c];

                GRBLinExpr          cutExpr = 0.0;
                std::vector<RCCarc> arcs;

                std::unordered_map<std::pair<int, int>, GRBLinExpr, pair_hash, pair_equal> localContributions;

                // Generate all possible arc pairs from nodes in S
                for (int i : S) {
                    for (int j : S) {
                        if (i != j) {
                            arcs.emplace_back(i, j);
                            auto cache_key = std::make_pair(i, j);

                            if (localContributions.find(cache_key) == localContributions.end()) {
                                GRBLinExpr contributionSum = 0.0;
                                for (size_t ctr = 0; ctr < allPaths.size(); ++ctr) {
                                    contributionSum += allPaths[ctr].timesArc(i, j) * model->getVar(ctr);
                                }
                                localContributions[cache_key] = contributionSum;
                                cutExpr += contributionSum;
                            } else {
                                cutExpr += localContributions[cache_key];
                            }
                        }
                    }
                }
                auto target_no_vehicles = ceil(
                    std::accumulate(S.begin(), S.end(), 0, [&](int sum, int i) { return sum + instance.demand[i]; }) /
                    instance.q);
                {
                    cutExpressions[c] = cutExpr;
                    rhsValues[c]      = S.size() - target_no_vehicles;

                    arcGroups[c] = std::move(arcs);
                }
            });

        // Define the work and run it synchronously
        auto work = stdexec::starts_on(sched, bulk_sender);
        stdexec::sync_wait(std::move(work));
        std::vector<GRBConstr> newConstraints(cutExpressions.size());
        for (size_t i = 0; i < cutExpressions.size(); ++i) {
            newConstraints[i] = model->addConstr(cutExpressions[i] <= rhsValues[i]);
        }

        rccManager.addCutBulk(arcGroups, rhsValues, newConstraints);
        fmt::print("Added {} RCC cuts\n", cutExpressions.size());
        model->update();
        model->optimize();

        return true;
    }
#endif

#ifdef RCC
    /**
     * @brief Performs RCC (Robust Capacity Cuts) separation for the given model and solution.
     *
     * This function identifies and adds violated RCC cuts to the model to improve the solution.
     * It uses parallel execution to handle the separation and constraint addition efficiently.
     *
     */

    bool RCCsep(GRBModel *model, const std::vector<double> &solution,
                std::vector<std::vector<std::vector<GRBConstr>>> &constraints) {

        // Constraint manager to store cuts
        CnstrMgrPointer cutsCMP = nullptr;
        CMGR_CreateCMgr(&cutsCMP, 100);

        auto             nVertices = N_SIZE - 1;
        std::vector<int> demands   = instance.demand;
        std::mutex       arcCache_mutex; // Mutex to protect arcCache

        // Precompute edge values from LP solution
        std::vector<std::vector<double>> aijs(N_SIZE + 2, std::vector<double>(N_SIZE + 2, 0.0));

        for (int counter = 0; counter < solution.size(); ++counter) {
            const auto &nodes = allPaths[counter].route;
            for (size_t k = 1; k < nodes.size(); ++k) {
                int source = nodes[k - 1];
                int target = (nodes[k] == N_SIZE - 1) ? 0 : nodes[k];
                aijs[source][target] += solution[counter];
            }
        }

        std::vector<int>    edgex, edgey;
        std::vector<double> edgeval;

        edgex.push_back(0);
        edgey.push_back(0);
        edgeval.push_back(0.0);

        for (int i = 0; i < nVertices; ++i) {
            for (int j = i + 1; j < nVertices; ++j) {
                double xij = aijs[i][j] + aijs[j][i];
                if (xij > 1e-4) {
                    {
                        edgex.push_back((i == 0) ? N_SIZE - 1 : i);
                        edgey.push_back(j);
                        edgeval.push_back(xij);
                    }
                }
            }
        }

        // RCC Separation
        char   intAndFeasible;
        double maxViolation = 0.0;
        CAPSEP_SeparateCapCuts(nVertices - 1, demands.data(), instance.q, edgex.size() - 1, edgex.data(), edgey.data(),
                               edgeval.data(), oldCutsCMP, 20, 1e-4, &intAndFeasible, &maxViolation, cutsCMP);

        if (intAndFeasible) return false; /* Optimal solution found */

        if (cutsCMP->Size == 0) return false;
        // Define the tasks that need to be executed in parallel
        auto tasks = std::vector<int>(cutsCMP->Size);

        std::iota(tasks.begin(), tasks.end(), 0); // Filling tasks with [0, 1, ..., cutsCMP->Size-1]

        print_cut("Found {} violated RCC cuts, max violation: {}\n", cutsCMP->Size, maxViolation);

        std::vector<int>                 rhsValues(cutsCMP->Size);
        std::vector<std::vector<RCCarc>> arcGroups(cutsCMP->Size);
        std::mutex                       cuts_mutex;

        const int                JOBS = 10;
        exec::static_thread_pool pool(JOBS);
        auto                     sched = pool.get_scheduler();

        // Create a bulk sender to handle parallel execution of tasks
        auto bulk_sender = stdexec::bulk(
            stdexec::just(), tasks.size(),
            [this, &cutsCMP, &model, &cuts_mutex, &tasks, &rhsValues, &arcGroups,

             &arcCache_mutex](std::size_t task_idx) {
                // Get the current task (cut index)
                int              cutIdx = tasks[task_idx];
                std::vector<int> S(cutsCMP->CPL[cutIdx]->IntList + 1,
                                   cutsCMP->CPL[cutIdx]->IntList + cutsCMP->CPL[cutIdx]->IntListSize + 1);

                GRBLinExpr                                                                 cutExpr = 0.0;
                std::vector<RCCarc>                                                        arcs;
                std::unordered_map<std::pair<int, int>, GRBLinExpr, pair_hash, pair_equal> localContributions;

                // Generate all possible arc pairs from nodes in S
                for (int i : S) {
                    for (int j : S) {
                        if (i != j) { arcs.emplace_back(i, j); }
                    }
                }

                rhsValues[cutIdx] = cutsCMP->CPL[cutIdx]->RHS;
                arcGroups[cutIdx] = std::move(arcs);
            });

        // Define the work and run it synchronously
        auto work = stdexec::starts_on(sched, bulk_sender);
        stdexec::sync_wait(std::move(work));

        // Create a bulk sender to handle parallel execution of each constraint
        auto bulk_sender_ctr = stdexec::bulk(stdexec::just(), rhsValues.size(), // We want rhsValues.size() tasks
                                             [&](std::size_t i) { // Lambda function for each task (constraint index i)
                                                 GRBLinExpr cutExpr = 0.0;

                                                 // For each arc in arcGroups[i], compute the cut expression
                                                 for (const auto &arc : arcGroups[i]) {
                                                     for (size_t ctr = 0; ctr < allPaths.size(); ++ctr) {
                                                         cutExpr += allPaths[ctr].getArcCount(arc) * model->getVar(ctr);
                                                     }
                                                 }

                                                 // Add the constraint to the model
                                                 auto ctr = model->addConstr(cutExpr <= rhsValues[i]);
                                                 rccManager.addCut(arcGroups[i], rhsValues[i], ctr);

                                             });

        // Attach to the scheduler and run the work synchronously
        auto work_ctr = stdexec::starts_on(sched, bulk_sender_ctr);
        stdexec::sync_wait(std::move(work_ctr)); // Wait for the tasks to complete
        model->update();
        // model->optimize();

        return true;
    }
#endif

    /**
     * Column generation algorithm.
     */
    void CG(GRBModel *node) {
        print_info("Column generation started...\n");

        node->optimize();
        int bucket_interval = 20;
        int time_horizon    = instance.T_max;

        numConstrs                = node->get(GRB_IntAttr_NumConstrs);
        std::vector<double> duals = std::vector<double>(numConstrs, 0.0);

        // BucketGraph bucket_graph(jobs, time_horizon, bucket_interval, instance.q, bucket_interval);
        BucketGraph bucket_graph(jobs, time_horizon, bucket_interval);

        bucket_graph.set_distance_matrix(instance.getDistanceMatrix(), 8);

        node->optimize();
        matrix                 = extractModelDataSparse(node);
        auto integer_solution  = node->get(GRB_DoubleAttr_ObjVal);
        bucket_graph.incumbent = integer_solution;

        auto allJobs = bucket_graph.getJobs();

#ifdef SRC
        r1c = LimitedMemoryRank1Cuts(allJobs);

        CutStorage *cuts = &r1c.cutStorage;
#endif

        std::vector<double> cutDuals;
        std::vector<double> jobDuals  = getDuals(node);
        auto                sizeDuals = jobDuals.size();

        double highs_obj_dual = 0.0;
        double highs_obj      = 0.0;

#ifdef SRC
        bucket_graph.cut_storage = cuts;
#endif

        bucket_graph.setup();

        double gap = 1e-6;

        bool s1    = true;
        bool s2    = false;
        bool s3    = false;
        bool s4    = false;
        bool s5    = false;
        bool ss    = false;
        int  stage = 1;

        bool TRstop = false;

        bool misprice = true;

        double lag_gap = std::numeric_limits<double>::max();

        auto                 inner_obj = 0.0;
        std::vector<Label *> paths;
        std::vector<double>  solution;
        bool                 can_add = true;

#ifdef STAB
        Stabilization stab(0.9, jobDuals);
#endif
        bool changed = false;
        // set start timer
        auto start_timer = std::chrono::high_resolution_clock::now();

        print_info("Starting column generation..\n\n");
        bool transition = false;

#if defined(RCC) || defined(EXACT_RCC)
        std::vector<std::vector<std::vector<GRBConstr>>> cvrsep_ctrs(
            instance.nC + 3, std::vector<std::vector<GRBConstr>>(instance.nC + 3, std::vector<GRBConstr>()));
#endif

#ifdef TR
        std::vector<GRBVar> w(numConstrs);
        std::vector<GRBVar> zeta(numConstrs);

        auto epsilon1 = 10;
        auto epsilon2 = 10;

        // Create w_i and zeta_i variables for each constraint
        for (int i = 0; i < numConstrs; i++) {
            w[i]    = node->addVar(0, epsilon1, -1.0, GRB_CONTINUOUS, "w_" + std::to_string(i + 1));
            zeta[i] = node->addVar(0, epsilon2, 1.0, GRB_CONTINUOUS, "zeta_" + std::to_string(i + 1));
        }

        const GRBConstr *constrs = node->getConstrs();
        for (int i = 0; i < numConstrs; i++) {
            node->chgCoeff(constrs[i], w[i], -1);
            node->chgCoeff(constrs[i], zeta[i], 1);
        }
        double              v                   = 100;
        bool                isInsideTrustRegion = false;
        std::vector<double> delta1;
        std::vector<double> delta2;
        for (auto dual : jobDuals) {
            delta1.push_back(dual - v);
            delta2.push_back(dual + v);
        }
        for (int i = 0; i < numConstrs; i++) {
            // Update coefficients for w[i] and zeta[i] in the objective function
            w[i].set(GRB_DoubleAttr_Obj, std::max(0.0, jobDuals[i] - v)); // Set w[i]'s coefficient to -delta1
            zeta[i].set(GRB_DoubleAttr_Obj, jobDuals[i] + v);             // Set zeta[i]'s coefficient to delta2
            w[i].set(GRB_DoubleAttr_UB, epsilon1);
            zeta[i].set(GRB_DoubleAttr_UB, epsilon2);
        }
        fmt::print("Starting trust region with v = {}\n", v);
#endif

#ifdef IPM
        IPSolver solver;
#endif

#ifdef RCC
        CMGR_CreateCMgr(&oldCutsCMP, 100); // For old cuts, if needed
#endif
        bool rcc = false;
        for (int iter = 0; iter < 2000; ++iter) {
            //////////////////////////////////////////////////////////////////////
            // PREPARING DUALS
            //////////////////////////////////////////////////////////////////////

            double obj;

            if (ss) {
#ifdef RCC
                print_cut("Testing RCC feasibility..\n");
                rcc = RCCsep(node, solution, cvrsep_ctrs);

#endif
#ifdef EXACT_RCC
                print_cut("Testing RCC feasibility..\n");
                rcc = exactRCCsep(node, solution, cvrsep_ctrs);
#endif

#if defined(SRC3) || defined(SRC)
                if (!rcc) {

                    // removeNegativeReducedCostVarsAndPaths(node);
                    // matrix = extractModelDataSparse(node);
                    node->optimize();

                    auto cuts_before = cuts->size();
                    ////////////////////////////////////////////////////
                    // Handle non-violated cuts in a single pass
                    ////////////////////////////////////////////////////
                    bool cleared        = false;
                    auto n_cuts_removed = 0;
                    // Iterate over the constraints in reverse order to remove non-violated cuts
                    for (int i = SRCconstraints.size() - 1; i >= 0; --i) {
                        GRBConstr constr = SRCconstraints[i];

                        // Get the slack value of the constraint
                        double slack = constr.get(GRB_DoubleAttr_Slack);

                        // If the slack is positive, it means the constraint is not violated
                        if (slack > 1e-3) {
                            cleared = true;

                            // Remove the constraint from the model and cut storage
                            node->remove(constr);
                            cuts->removeCut(cuts->getID(i));
                            n_cuts_removed++;

                            // Remove from SRCconstraints
                            SRCconstraints.erase(SRCconstraints.begin() + i);
                        }
                    }

                    if (cleared) {
                        node->update();                        // Update the model to reflect the removals
                        node->optimize();                      // Re-optimize the model
                        matrix = extractModelDataSparse(node); // Extract model data
                    }

                    // print cuts size
                    solution = extractSolution(node);
                    // r1c.cutStorage = cuts;
                    r1c.allPaths = allPaths;
                    r1c.separate(matrix.A_sparse, solution);
#ifdef SRC
                    r1c.prepare45Heuristic(matrix.A_sparse, solution);
                    r1c.the45Heuristic<CutType::FourRow>(matrix.A_sparse, solution);
                    r1c.the45Heuristic<CutType::FiveRow>(matrix.A_sparse, solution);
#endif
                    // cuts = &r1c.cutStorage;
                    if (cuts_before == cuts->size() + n_cuts_removed) {
                        print_info("No violations found, calling it a day\n");
                        break;
                    }

                    changed = cutHandler(r1c, node, SRCconstraints);
                    if (changed) { matrix = extractModelDataSparse(node); }
                    // cuts = &r1c.cutStorage;
                }
#endif
                bucket_graph.ss = false;
            }

#ifdef TR
            if (!TRstop) {
                isInsideTrustRegion = true;
                for (int i = 0; i < numConstrs; i++) {
                    if (jobDuals[i] < delta1[i] || jobDuals[i] > delta2[i]) { isInsideTrustRegion = false; }
                }
                // if (isInsideTrustRegion) { fmt::print("Fall inside trust region\n"); }
                if (isInsideTrustRegion) {
                    v -= 1;
                    // print v
                    fmt::print("Reducing v to {}\n", v);
                    epsilon1 += 100;
                    epsilon2 += 100;
                    for (int i = 0; i < numConstrs; i++) {
                        // Update coefficients for w[i] and zeta[i] in the objective function
                        w[i].set(GRB_DoubleAttr_Obj, jobDuals[i]);        // Set w[i]'s coefficient to -delta1
                        zeta[i].set(GRB_DoubleAttr_Obj, jobDuals[i] + v); // Set zeta[i]'s coefficient to delta2
                        w[i].set(GRB_DoubleAttr_UB, epsilon1);
                        zeta[i].set(GRB_DoubleAttr_UB, epsilon2);
                    }
                }
                for (int i = 0; i < numConstrs; i++) {
                    delta1[i] = jobDuals[i] - v;
                    delta2[i] = jobDuals[i] + v;
                }
                if (v <= 25) {
                    for (int i = 0; i < w.size(); i++) {
                        node->remove(w[i]);
                        node->remove(zeta[i]);
                    }
                    TRstop = true;
                    node->update();
                }
            }

#endif

#ifdef IPM
            auto d            = 0.5;
            auto matrixSparse = extractModelDataSparse(node);
            gap               = std::abs(highs_obj - (highs_obj + std::min(0.0, inner_obj))) / std::abs(highs_obj);
            gap               = gap / d;
            if (std::isnan(gap)) { gap = 1e-4; }
            if (std::signbit(gap)) { gap = 1e-4; }

            auto ip_result   = solver.run_optimization(matrixSparse, gap);
            highs_obj        = std::get<0>(ip_result);
            highs_obj_dual   = std::get<1>(ip_result);
            solution         = std::get<2>(ip_result);
            jobDuals         = std::get<3>(ip_result);
            auto originDuals = jobDuals;
            for (auto &dual : jobDuals) { dual = -dual; }
#endif

#ifdef STAB
            // auto matrixSparse = extractModelDataSparse(node);
            node->optimize();
            highs_obj = node->get(GRB_DoubleAttr_ObjVal);
            jobDuals  = getDuals(node);

            stab.update_stabilization_after_master_optim(jobDuals);

            misprice = true;
            while (misprice) {
                jobDuals = stab.getStabDualSolAdvanced(jobDuals);
                solution = extractSolution(node);
#endif

                bool integer = true;
                // Check integrality of the solution
                for (auto &sol : solution) {
                    // NOTE: 1e-1 is not enough
                    if (sol > 1e-2 && sol < 1 - 1e-2) {
                        integer = false;
                        break;
                    }
                }
                if (integer) {
                    if (highs_obj < integer_solution) {
                        print_info("Updating integer solution to {}\n", highs_obj);
                        integer_solution       = highs_obj;
                        bucket_graph.incumbent = integer_solution;
                    }
                }
                lag_gap          = integer_solution - (highs_obj + std::min(0.0, inner_obj));
                bucket_graph.gap = lag_gap;
                bucket_graph.augment_ng_memories(solution, allPaths, true, 5, 100, 16, N_SIZE);
                bucket_graph.relaxation = highs_obj;

#if defined(SRC3) || defined(SRC)
                // print cuts.size
                if (cuts->size() > 0) {
                    auto duals = node->get(GRB_DoubleAttr_Pi, SRCconstraints.data(), SRCconstraints.size());
                    cutDuals.assign(duals, duals + SRCconstraints.size());
                    // print cutDuals size
                    cuts->setDuals(cutDuals);
                }
#endif

                bucket_graph.setDuals(jobDuals);

                //////////////////////////////////////////////////////////////////////
                // CALLING BALDES
                //////////////////////////////////////////////////////////////////////
                paths = bucket_graph.solve();

                inner_obj = paths[0]->cost;
                stage     = bucket_graph.getStage();

                ss = bucket_graph.ss;

                //////////////////////////////////////////////////////////////////////

                // Adding cols
                auto colAdded = addColumn(node, paths, false);
// remove all cuts
#ifdef SRC
                if (bucket_graph.getStatus() == Status::Rollback) {
                    for (int i = SRCconstraints.size() - 1; i >= 0; --i) {
                        GRBConstr constr = SRCconstraints[i];
                        node->remove(constr);
                    }
                    SRCconstraints = std::vector<GRBConstr>();
                    node->update();
                    node->optimize();
                    cuts->reset();
                    // r1c.cutStorage = cuts;
                    matrix = extractModelDataSparse(node);
                }
#endif

#ifdef SCHRODINGER
                // Adding schrodinger paths
                auto sch_paths = bucket_graph.getSchrodinger();
                colAdded += addPath(node, sch_paths, true);
#endif

#ifdef RIH
                auto rih_paths = bucket_graph.get_rih_labels();
                colAdded += addColumn(node, rih_paths, true);

#endif

#ifdef STAB
                stab.update_stabilization_after_pricing_optim(matrix, jobDuals, lag_gap, paths);

                if (colAdded == 0) {
                    stab.update_stabilization_after_misprice();
                    if (stab.shouldExit()) { misprice = false; }
                } else {
                    misprice = false;
                }
            }

            if (bucket_graph.getStatus() == Status::Optimal && stab.shouldExit()) {
                print_info("Optimal solution found\n");
                break;
            }

            stab.update_stabilization_after_iter(jobDuals);
#endif

            auto cur_alpha = 0.0;
#ifdef STAB
            cur_alpha = stab.base_alpha;
#endif

            auto n_cuts = 0;
#ifdef SRC
            n_cuts = cuts->size();
#endif
            if (iter % 10 == 0)
                fmt::print("| It.: {:4} | Obj.: {:8.2f} | Price: {:9.2f} | Cuts: {:4} | Paths: {:4} | "
                           "Stage: {:1} | "
                           "Lag.: {:10.4f} | RCC: {:4} | alpha: {:4.2f} | \n",
                           iter, highs_obj, inner_obj, n_cuts, paths.size(), stage, lag_gap, rcc, cur_alpha);
        }
        auto end_timer        = std::chrono::high_resolution_clock::now();
        auto duration_ms      = std::chrono::duration_cast<std::chrono::milliseconds>(end_timer - start_timer).count();
        auto duration_seconds = duration_ms / 1000;
        auto duration_milliseconds = duration_ms % 1000;

        bucket_graph.print_statistics();

        node->optimize();
        auto relaxed_result = node->get(GRB_DoubleAttr_ObjVal);

        binarizeNode(node);
        node->optimize();
        auto ip_result = node->get(GRB_DoubleAttr_ObjVal);

        // get solution in which x > 0.5 and print the corresponding allPaths
        std::vector<int> sol;
        for (int i = 0; i < allPaths.size(); i++) {
            if (node->getVar(i).get(GRB_DoubleAttr_X) > 0.5) { sol.push_back(i); }
        }

        for (auto s : sol) {
            fmt::print("Path: ");
            for (auto j : allPaths[s].route) { fmt::print("{} ", j); }
            fmt::print("\n");
        }
        fmt::print("\n");

        // ANSI escape code for blue text
        constexpr auto blue  = "\033[34m";
        constexpr auto reset = "\033[0m";

        fmt::print("+----------------------+----------------+\n");
        fmt::print("| {:<14} | {}{:>20}{} |\n", "Bound", blue, relaxed_result, reset);
        fmt::print("| {:<14} | {}{:>20}{} |\n", "Incumbent", blue, ip_result, reset);
        fmt::print("| {:<14} | {}{:>16}.{:03}{} |\n", "VRP Duration", blue, duration_seconds, duration_milliseconds,
                   reset);
        fmt::print("+----------------------+----------------+\n");
    }
};
